/* xZTL: Zone Translation Layer User-space Library
 *
 * Copyright 2019 Samsung Electronics
 *
 * Written by Ivan L. Picoli <i.picoli@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include <libxnvme_spec.h>
#include <libxnvme_znd.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <xztl-ztl.h>
#include <xztl.h>
#include <ztl.h>
#include <ztl_metadata.h>

struct xztl_mthread_info mthread;

struct xnvme_node_mgmt_entry {
    struct app_group *    grp;
    struct ztl_pro_node * node;
    struct xztl_mp_entry *mp_entry;
    int32_t               op_code;
    STAILQ_ENTRY(xnvme_node_mgmt_entry) entry;
};

static pthread_spinlock_t xnvme_mgmt_spin;
static STAILQ_HEAD(xnvme_emu_head, xnvme_node_mgmt_entry) submit_head;

static void ztl_pro_grp_print_status(struct app_group *grp) {
    struct ztl_pro_node_grp *pro_node;
    struct ztl_pro_node *    node;
    struct ztl_pro_zone *    zone;
    uint32_t                 type_i;

    pro_node = (struct ztl_pro_node_grp *)grp->pro;
}

int ztl_pro_grp_get(struct app_group *grp, struct app_pro_addr *ctx,
                    uint32_t nsec, int32_t *node_id,
                    struct xztl_thread *tdinfo) {
    struct ztl_pro_node_grp *pro = NULL;
    pro                          = (struct ztl_pro_node_grp *)grp->pro;
    struct ztl_pro_node *node    = &pro->vnodes[*node_id];
    uint64_t sec_avlb, actual_sec;

    uint64_t nlevel     = nsec / (ZTL_PRO_ZONE_NUM_INNODE * ZTL_READ_SEC_MCMD);
    int32_t  remain_sec = nsec % (ZTL_PRO_ZONE_NUM_INNODE * ZTL_READ_SEC_MCMD);
    int      zn_i       = 0;
    ctx->naddr          = 0;
    bool isfull = false;
    for (zn_i = 0; zn_i < ZTL_PRO_ZONE_NUM_INNODE; zn_i++) {
        struct ztl_pro_zone *zone = node->vzones[zn_i];
        uint64_t sec_avlb = zone->zmd_entry->addr.g.sect + zone->capacity -
                            zone->zmd_entry->wptr_inflight;
        uint64_t actual_sec =
            nlevel * ZTL_READ_SEC_MCMD +
            (remain_sec >= ZTL_READ_SEC_MCMD ? ZTL_READ_SEC_MCMD : remain_sec);

        if (sec_avlb < actual_sec) {
            printf(
                "ztl-pro-grp: left sector is not enough sec_avlb[%u] "
                "actual_sec[%u] remain_sec[%d]",
                sec_avlb, actual_sec, remain_sec);
            goto NO_LEFT;
        }

        if (remain_sec >= ZTL_READ_SEC_MCMD) {
            remain_sec -= ZTL_READ_SEC_MCMD;
        } else {
            remain_sec = 0;
        }

        ctx->naddr++;
        ctx->addr[zn_i].addr   = zone->addr.addr;
        ctx->addr[zn_i].g.sect = zone->zmd_entry->wptr_inflight;
        ctx->nsec[zn_i]        = actual_sec;
        zone->zmd_entry->wptr_inflight += ctx->nsec[zn_i];

        sec_avlb = zone->zmd_entry->addr.g.sect + zone->capacity - zone->zmd_entry->wptr_inflight;
        if (!sec_avlb) {
            isfull = true;
        }
        ZDEBUG(ZDEBUG_PRO,
               "ztl-pro-grp  (get): (%d/%d/0x%lx/0x%lx/0x%lx) "
               " sp: %d, remain_sec: %d",
               zone->addr.g.grp, zone->addr.g.zone, (uint64_t)zone->addr.g.sect,
               zone->zmd_entry->wptr, zone->zmd_entry->wptr_inflight,
               ctx->nsec[zn_i], remain_sec);
        if (isfull) {
            node->status = XZTL_ZMD_NODE_FULL;
        }
    }
    return 0;

NO_LEFT:
    while (zn_i) {
        zn_i--;
        ztl_pro_grp_free(grp, ctx->addr[zn_i].g.zone, 0);
        ctx->naddr--;
        ctx->addr[zn_i].addr = 0;
        ctx->nsec[zn_i]      = 0;
    }

    log_erra("ztl-pro (get): No zones left. Group %d", grp->id);
    return -1;
}

void ztl_pro_grp_free(struct app_group *grp, uint32_t zone_i, uint32_t nsec) {
    struct ztl_pro_zone *    zone;
    struct ztl_pro_node_grp *pro;
    struct xztl_zn_mcmd      cmd;
    int                      ret;

    pro  = (struct ztl_pro_node_grp *)grp->pro;
    zone = &((struct ztl_pro_node_grp *) grp->pro)->vzones[zone_i-get_metadata_zone_num()];

    /* Move the write pointer */
    /* A single thread touches the write pointer, no lock needed */
    zone->zmd_entry->wptr += nsec;

    ZDEBUG(ZDEBUG_PRO, "ztl-pro-grp (free): (%d/%d/0x%lx/0x%lx/0x%lx) ",
           zone->addr.g.grp, zone->addr.g.zone, (uint64_t)zone->addr.g.sect,
           zone->zmd_entry->wptr, zone->zmd_entry->wptr_inflight);

    if (ZDEBUG_PRO_GRP)
        ztl_pro_grp_print_status(grp);
}

int ztl_pro_grp_node_finish(struct app_group *grp, struct ztl_pro_node *node) {
    struct ztl_pro_zone *zone;
    struct xztl_zn_mcmd cmd;
    int                 ret;


    for (int i = 0; i < ZTL_PRO_ZONE_NUM_INNODE; i++) {
        /* Explicit closes the zone */
        zone          = node->vzones[i];
        cmd.opcode    = XZTL_ZONE_MGMT_FINISH;
        cmd.addr.addr = zone->addr.addr;
        cmd.nzones    = 1;
        ret           = xztl_media_submit_zn(&cmd);

        if (ret || cmd.status) {
            log_erra("ztl-pro: Zone finish failure (%lld). status %d",
                     zone->addr.g.zone, cmd.status);
            xztl_atomic_int32_update(&node->nr_finish_err,
                                     node->nr_finish_err + 1);
            goto ERR;
        }
        zone->zmd_entry->wptr = zone->addr.g.sect + zone->capacity;
    }

ERR:
    return ret;
}

int ztl_pro_grp_submit_mgmt(struct app_group *grp, struct ztl_pro_node *node,
                            int32_t op_code) {
    struct xztl_mp_entry *mp_cmd;
    mp_cmd = xztl_mempool_get(XZTL_NODE_MGMT_ENTRY, 0);
    if (!mp_cmd) {
        log_err("ztl-wca: Mempool failed.");
        return -1;
    }

    struct xnvme_node_mgmt_entry *et =
        (struct xnvme_node_mgmt_entry *)mp_cmd->opaque;
    et->grp      = grp;
    et->node     = node;
    et->op_code  = op_code;
    et->mp_entry = mp_cmd;

    pthread_spin_lock(&xnvme_mgmt_spin);
    STAILQ_INSERT_TAIL(&submit_head, et, entry);
    pthread_spin_unlock(&xnvme_mgmt_spin);
    return 0;
}

static void *znd_pro_grp_process_mgmt(void *args) {
    mthread.comp_active = 1;
    struct xnvme_node_mgmt_entry *et;
    int                           ret;
    while (mthread.comp_active) {
        usleep(1);

    NEXT:
        if (!STAILQ_EMPTY(&submit_head)) {
            pthread_spin_lock(&xnvme_mgmt_spin);
            et = STAILQ_FIRST(&submit_head);
            if (!et) {
                pthread_spin_unlock(&xnvme_mgmt_spin);
                continue;
            }

            STAILQ_REMOVE_HEAD(&submit_head, entry);
            pthread_spin_unlock(&xnvme_mgmt_spin);

            if (et->op_code == ZTL_MGMG_FULL_ZONE) {
                ret = ztl_pro_grp_node_finish(et->grp, et->node);
            } else {
                ret = ztl_pro_grp_node_reset(et->grp, et->node);
            }

            if (ret) {
                printf("znd_pro_grp_process_mgmt ret[%d]\n", ret);
            }

            xztl_mempool_put(et->mp_entry, XZTL_NODE_MGMT_ENTRY, 0);
            goto NEXT;
        }
    }

    return XZTL_OK;
}

char ztl_pro_grp_is_node_full(struct app_group *grp, uint32_t nodeid) {
    struct ztl_pro_node_grp *pro = (struct ztl_pro_node_grp *) grp->pro;
    return (pro->vnodes[nodeid].status == XZTL_ZMD_NODE_FULL);
}

int ztl_pro_grp_finish_zn(struct app_group *grp, uint32_t zid, uint8_t type) {
    struct ztl_pro_zone *    zone;
    struct ztl_pro_node_grp *pro;
    struct app_zmd_entry *   zmde;
    // struct xztl_zn_mcmd cmd;

    pro  = (struct ztl_pro_node_grp *)grp->pro;
    zone = &((struct ztl_pro_node_grp *) grp->pro)->vzones[zid-get_metadata_zone_num()];
    zmde = zone->zmd_entry;

    /* Zone is already empty */
    if (!(zmde->flags & XZTL_ZMD_USED))
        return 0;

    /* Zone is already finished */
    return (zmde->wptr == zone->addr.g.sect + zone->capacity) ? 0 : 1;
}

int ztl_pro_grp_put_zone(struct app_group *grp, uint32_t zone_i) {
    struct ztl_pro_zone *    zone;
    struct ztl_pro_node_grp *pro;
    struct app_zmd_entry *   zmde;
    struct xztl_core *       core;
    get_xztl_core(&core);
    pro  = (struct ztl_pro_node_grp *)grp->pro;
    zone = &pro->vzones[zone_i - get_metadata_zone_num()];
    zmde = zone->zmd_entry;

    ZDEBUG(ZDEBUG_PRO,
           "ztl-pro-grp  (put): (%d/%d/0x%lx/0x%lx) "
           "pieces %d, deletes: %d",
           zone->addr.g.grp, zone->addr.g.zone, (uint64_t)zone->addr.g.sect,
           zone->zmd_entry->wptr, zone->zmd_entry->npieces,
           zone->zmd_entry->ndeletes);

    if (!(zmde->flags & XZTL_ZMD_AVLB)) {
        log_infoa("ztl-pro (put): Cannot PUT an invalid zone (%d/%d)", grp->id,
                  zone_i);
        return -1;
    }

    if (zmde->flags & XZTL_ZMD_RSVD) {
        log_infoa("ztl-pro (put): Zone is RESERVED (%d/%d)", grp->id, zone_i);
        return -2;
    }

    if (!(zmde->flags & XZTL_ZMD_USED)) {
        log_infoa("ztl-pro (put): Zone is already EMPTY (%d/%d)", grp->id,
                  zone_i);
        return -3;
    }

    if (zmde->flags & XZTL_ZMD_OPEN) {
        log_infoa("ztl-pro (put): Zone is still OPEN (%d/%d)", grp->id, zone_i);
        return -4;
    }

    xztl_atomic_int16_update(&zmde->flags, zmde->flags ^ XZTL_ZMD_USED);
    xztl_stats_inc(XZTL_STATS_RECYCLED_ZONES, 1);
    xztl_stats_inc(XZTL_STATS_RECYCLED_BYTES,
                   zone->capacity * core->media->geo.nbytes);

    if (ZDEBUG_PRO_GRP)
        ztl_pro_grp_print_status(grp);

    return 0;
}

static void ztl_pro_grp_zones_free(struct app_group *grp) {
    // struct ztl_pro_zone *zone;
    struct ztl_pro_node_grp *pro;
    struct ztl_pro_node *    vnode;
    uint8_t                  ptype;

    pro = (struct ztl_pro_node_grp *)grp->pro;
    free(pro->vnodes);
    free(pro->vzones);
}

int ztl_pro_grp_node_reset(struct app_group *grp, struct ztl_pro_node *node) {
    struct ztl_pro_zone *    zone;
    struct ztl_pro_node_grp *node_grp = grp->pro;
    int                      ret;

    for (int i = 0; i < ZTL_PRO_ZONE_NUM_INNODE; i++) {
        zone = node->vzones[i];
        ret  = ztl_pro_node_reset_zn(zone);
        if (ret) {
            xztl_atomic_int32_update(&node->nr_reset_err,
                                     node->nr_reset_err + 1);
            goto ERR;
        }
    }

    node_grp->vnodes[node->id].status = XZTL_ZMD_NODE_FREE;

ERR:
    return ret;
}

int ztl_pro_node_reset_zn(struct ztl_pro_zone *zone) {
    struct xztl_zn_mcmd   cmd;
    struct app_zmd_entry *zmde;
    int                   ret = 0;

    zmde          = zone->zmd_entry;
    cmd.opcode    = XZTL_ZONE_MGMT_RESET;
    cmd.addr.addr = zone->addr.addr;
    cmd.nzones    = 1;
    ret           = xztl_media_submit_zn(&cmd);

    if (ret || cmd.status) {
        log_erra("ztl_pro_node_reset_zn: Zone: %lu reset failure. status %d\n",
                 zone->addr.g.zone, cmd.status);
        goto ERR;
    }

    xztl_atomic_int64_update(&zmde->wptr, zone->addr.g.sect);
    xztl_atomic_int64_update(&zmde->wptr_inflight, zone->addr.g.sect);
ERR:
    return ret;
}

int ztl_pro_grp_reset_all_zones(struct app_group *grp) {
    struct xztl_zn_mcmd cmd;
    int                 ret;
    cmd.opcode = XZTL_ZONE_MGMT_RESET;
    cmd.nzones = grp->zmd.entries;

    ret = xztl_media_submit_zn(&cmd);
    if (ret || cmd.status) {
        log_erra("ztl-pro:All zone reset failure . status %d", cmd.status);
    }
    return 0;
}

int ztl_pro_grp_node_init(struct app_group *grp) {
    struct xnvme_spec_znd_descr *zinfo;
    struct xnvme_znd_report *    rep;
    struct ztl_pro_zone *        zone;
    struct app_zmd_entry *       zmde;
    struct ztl_pro_node_grp *    pro;
    struct xztl_core *           core;
    get_xztl_core(&core);
    uint8_t ptype;

    int ntype, zone_i, node_i, zone_num_in_node;

    pro = calloc(1, sizeof(struct ztl_pro_node_grp));
    if (!pro)
        return XZTL_ZTL_PROV_ERR;

    int metadata_zone_num = get_metadata_zone_num();

    int32_t node_num = grp->zmd.entries / ZTL_PRO_ZONE_NUM_INNODE;
    pro->vnodes      = calloc(node_num, sizeof(struct ztl_pro_node));
    if (!pro->vnodes) {
        free(pro);
        return XZTL_ZTL_PROV_ERR;
    }
    pro->vzones = calloc(grp->zmd.entries, sizeof(struct ztl_pro_zone));
    if (!pro->vzones) {
        // vnodes have been claimed
        free(pro->vnodes);
        free(pro);
        return XZTL_ZTL_PROV_ERR;
    }

    if (pthread_spin_init(&pro->spin, 0)) {
        free(pro->vnodes);
        free(pro->vzones);
        return XZTL_ZTL_PROV_ERR;
    }

    grp->pro = pro;
    rep      = grp->zmd.report;

    node_i           = 0;
    zone_num_in_node = 0;
    for (zone_i = metadata_zone_num; zone_i < grp->zmd.entries; zone_i++) {
        if (zone_num_in_node == ZTL_PRO_ZONE_NUM_INNODE) {
            pro->vnodes[node_i].id = node_i;
            pro->vnodes[node_i].zone_num = zone_num_in_node;
            pro->totalnode++;
            node_i++;
            zone_num_in_node = 0;
        }

        /* We are getting the full report here */
        zinfo = XNVME_ZND_REPORT_DESCR(
            rep, grp->id * core->media->geo.zn_grp + zone_i);

        zone = &pro->vzones[zone_i - metadata_zone_num];

        zmde = ztl()->zmd->get_fn(grp, zone_i, 0);

        if (zmde->addr.g.zone != zone_i || zmde->addr.g.grp != grp->id)
            log_erra("ztl-pro: zmd entry address does not match (%d/%d)(%d/%d)",
                     zmde->addr.g.grp, zmde->addr.g.zone, grp->id, zone_i);

        if ((zmde->flags & XZTL_ZMD_RSVD) || !(zmde->flags & XZTL_ZMD_AVLB)) {
            printf("flags: %x\n", zmde->flags);
            continue;
        }

        zone->addr.addr = zmde->addr.addr;
        zone->capacity  = zinfo->zcap;
        zone->state     = zinfo->zs;
        zone->zmd_entry = zmde;
        zone->lock      = 0;
        // zone->grp = grp;
        pro->vnodes[node_i].vzones[zone_num_in_node++] = zone;

        switch (zinfo->zs) {
            case XNVME_SPEC_ZND_STATE_EMPTY:

                if ((zmde->flags & XZTL_ZMD_USED) ||
                    (zmde->flags & XZTL_ZMD_OPEN)) {
                    log_erra(
                        "ztl-pro: device reported EMPTY zone, but ZMD flag"
                        " does not match (%d/%d)(%x)",
                        grp->id, zone_i, zmde->flags);
                    continue;
                }

                zmde->npieces  = 0;
                zmde->ndeletes = 0;
                if (pro->vnodes[node_i].status != XZTL_ZMD_NODE_FREE &&
                    pro->vnodes[node_i].status != XZTL_ZMD_NODE_USED) {
                    pro->vnodes[node_i].status = XZTL_ZMD_NODE_FREE;
                }

                ZDEBUG(ZDEBUG_PRO_GRP, " ZINFO: (%d/%d) empty\n",
                       zmde->addr.g.grp, zmde->addr.g.zone);
                break;
            case XNVME_SPEC_ZND_STATE_EOPEN:
            case XNVME_SPEC_ZND_STATE_IOPEN:
            case XNVME_SPEC_ZND_STATE_CLOSED:
            case XNVME_SPEC_ZND_STATE_FULL:
                pro->vnodes[node_i].status = XZTL_ZMD_NODE_USED;
                ZDEBUG(ZDEBUG_PRO_GRP,
                       " ZINFO NOT CORRECT : (%d/%d) , status : %d\n",
                       zmde->addr.g.grp, zmde->addr.g.zone, zinfo->zs);
                break;

            default:
                log_infoa("ztl-pro: Unknown zone condition. zone %d, zs: %d",
                          grp->id * core->media->geo.zn_grp + zone_i,
                          zinfo->zs);
        }

        zmde->wptr = zmde->wptr_inflight = zinfo->wp;
    }

    xztl_mempool_create(XZTL_NODE_MGMT_ENTRY, 0, 128,
                        sizeof(struct xnvme_node_mgmt_entry), NULL, NULL);
    STAILQ_INIT(&submit_head);
    if (pthread_spin_init(&xnvme_mgmt_spin, 0)) {
        return 1;
    }

    pthread_create(&mthread.comp_tid, NULL, znd_pro_grp_process_mgmt, NULL);

    log_infoa("ztl-pro: Started. Group %d.", grp->id);
    return 0;
}

void ztl_pro_grp_exit(struct app_group *grp) {
    struct ztl_pro_node_grp *pro;

    pro = (struct ztl_pro_node_grp *)grp->pro;

    pthread_spin_destroy(&pro->spin);
    ztl_pro_grp_zones_free(grp);
    free(grp->pro);

    log_infoa("ztl-pro: Stopped. Group %d.", grp->id);
}
