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
#include <xztl.h>
#include <xztl-mods.h>
#include <xztl-pro.h>
#include <xztl-stats.h>
#include <xztl-metadata.h>

static struct ztl_pro_node_grp *_ztl_pro_grp_new_pro(int32_t  node_num,
                                                     uint32_t entries) {
    struct ztl_pro_node_grp *pro;

    pro = calloc(1, sizeof(struct ztl_pro_node_grp));
    if (!pro) {
        log_err("Mem wrong: Calloc ztl_pro_node_grp error!\n");
        return NULL;
    }

    pro->nnodes = node_num;
    pro->vnodes = calloc(node_num, sizeof(struct ztl_pro_node));  // node资源池
    if (!pro->vnodes) {
        log_err("Mem wrong: Calloc ztl_pro_node error!\n");
        goto free_pro;
    }

    pro->nzones = entries;
    pro->vzones = calloc(entries, sizeof(struct ztl_pro_zone));  // zone资源池
    if (!pro->vzones) {
        log_err("Mem wrong: Calloc ztl_pro_zone error!\n");
        goto free_nodes;
    }

    if (pthread_spin_init(&pro->spin, 0)) {
        log_err("ztl_pro_grp_node_init: pthread_spin_init failed\n");
        goto free_all;
    }

    TAILQ_INIT(&pro->free_head);
    TAILQ_INIT(&pro->used_head);

    pro->nfree = 0;
    return pro;

free_all:
    free(pro->vzones);
free_nodes:
    free(pro->vnodes);
free_pro:
    free(pro);

    return NULL;
}

static void _ztl_pro_grp_destroy_pro(struct ztl_pro_node_grp *pro) {
    pthread_spin_destroy(&pro->spin);

    free(pro->vzones);
    free(pro->vnodes);
}

int ztl_pro_grp_get(struct app_group *grp, struct app_pro_addr *ctx,
                    uint32_t num, int32_t node_id, uint32_t start) {
    struct ztl_pro_node_grp *pro  = (struct ztl_pro_node_grp *)grp->pro;
    struct ztl_pro_node     *node = &pro->vnodes[node_id];
    struct ztl_pro_zone     *zone;

    uint64_t num_zn   = num / ZTL_PRO_ZONE_NUM_INNODE;
    int      addition = num % ZTL_PRO_ZONE_NUM_INNODE;
    int      zn_i     = start % ZTL_PRO_ZONE_NUM_INNODE;

    while (num) {
        int i                        = (addition == 0) ? 0 : 1;
        zone                         = node->vzones[zn_i];
        ctx->addr[ctx->naddr].addr   = zone->addr.addr;
        ctx->addr[ctx->naddr].g.sect = zone->zmd_entry->wptr_inflight;
        ctx->nsec[ctx->naddr]        = (num_zn + i) * ZTL_IO_SEC_MCMD;
        zone->zmd_entry->wptr_inflight += ctx->nsec[ctx->naddr];

        ZDEBUG(ZDEBUG_PRO,
               "ztl_pro_grp_get: [%u/%u/0x%lx] "
               "wp[%lu] sp [%lu], remain_sec [%u]",
               zone->addr.g.grp, zone->addr.g.zone, (uint64_t)zone->addr.g.sect,
               zone->zmd_entry->wptr, zone->zmd_entry->wptr_inflight,
               ctx->nsec[ctx->naddr]);

        zn_i = (zn_i + 1) % ZTL_PRO_ZONE_NUM_INNODE;
        num -= (num_zn + i);
        ctx->naddr++;

        if (addition)
            addition--;
    }

    return XZTL_OK;
}

void ztl_pro_grp_free(struct app_group *grp, uint32_t zone_i, uint32_t nsec) {
    struct ztl_pro_zone     *zone;
    struct ztl_pro_node_grp *pro;

    pro  = (struct ztl_pro_node_grp *)grp->pro;
    int     metadata_zone_num = get_metadata_zone_num();
    zone = &(pro->vzones[zone_i - metadata_zone_num]);

    /* Move the write pointer */
    /* A single thread touches the write pointer, no lock needed */
    zone->zmd_entry->wptr += nsec;

    ZDEBUG(ZDEBUG_PRO, "ztl_pro_grp_free: [%d/%d/0x%lx/0x%lx/0x%lx] ",
           zone->addr.g.grp, zone->addr.g.zone, (uint64_t)zone->addr.g.sect,
           zone->zmd_entry->wptr, zone->zmd_entry->wptr_inflight);
}

int ztl_pro_grp_get_node(struct ztl_queue_pool   *q,
                         struct ztl_pro_node_grp *pro) {
    if (!q->node || q->node->optimal_write_sec_left == 0) {
        pthread_spin_lock(&pro->spin);
        q->node = TAILQ_FIRST(&pro->free_head);
        if (!q->node) {
            log_erra("_ztl_io_get_node: error! No more nodes!");
            pthread_spin_unlock(&pro->spin);
            return XZTL_ZTL_PROV_ERR;
        }
        TAILQ_REMOVE(&pro->free_head, q->node, fentry);
        TAILQ_INSERT_TAIL(&pro->used_head, q->node, fentry);
        pthread_spin_unlock(&pro->spin);
        ATOMIC_SUB(&pro->nfree, 1);
    }
    return XZTL_OK;
}

int ztl_pro_grp_node_init(struct app_group *grp) {
    struct xnvme_spec_znd_descr *zinfo;
    struct xnvme_znd_report     *rep;
    struct ztl_pro_zone         *zone;
    struct app_zmd_entry        *zmde;
    struct ztl_pro_node_grp     *pro;

    struct xztl_core *core;
    get_xztl_core(&core);

    int zone_i, node_i, zone_num_in_node;

    int     metadata_zone_num = get_metadata_zone_num();
    int32_t node_num =
        (grp->zmd.entries - metadata_zone_num) / ZTL_PRO_ZONE_NUM_INNODE;

    pro = _ztl_pro_grp_new_pro(node_num, grp->zmd.entries);
    if (!pro)
        return XZTL_ZTL_PROV_ERR;

    grp->pro = pro;
    rep      = grp->zmd.report;

    node_i           = 0;
    zone_num_in_node = 0;

    int full_count = 0;
    int sec_num    = 0;
    for (zone_i = metadata_zone_num; zone_i < grp->zmd.entries; zone_i++) {
        if (zone_num_in_node == ZTL_PRO_ZONE_NUM_INNODE) {
            pro->vnodes[node_i].id       = node_i;
            pro->vnodes[node_i].nr_valid = 0;
            pro->vnodes[node_i].level = -1;

            if (full_count == ZTL_PRO_ZONE_NUM_INNODE) {
                pro->vnodes[node_i].status                 = XZTL_ZMD_NODE_FULL;
                pro->vnodes[node_i].optimal_write_sec_left = 0;
                pro->vnodes[node_i].optimal_write_sec_used =
                    ZTL_PRO_OPT_SEC_NUM_INNODE;
                TAILQ_INSERT_TAIL(&pro->used_head, &pro->vnodes[node_i],
                                  fentry);
            } else if (sec_num == 0) {
                pro->vnodes[node_i].status = XZTL_ZMD_NODE_FREE;
                pro->vnodes[node_i].optimal_write_sec_left =
                    ZTL_PRO_OPT_SEC_NUM_INNODE;
                pro->vnodes[node_i].optimal_write_sec_used = 0;
                TAILQ_INSERT_TAIL(&pro->free_head, &pro->vnodes[node_i],
                                  fentry);
                ATOMIC_ADD(&pro->nfree, 1);
            } else {
                pro->vnodes[node_i].status = XZTL_ZMD_NODE_USED;
                pro->vnodes[node_i].optimal_write_sec_left =
                    (ZTL_PRO_OPT_SEC_NUM_INNODE - sec_num / ZTL_IO_SEC_MCMD);
                pro->vnodes[node_i].optimal_write_sec_used =
                    sec_num / ZTL_IO_SEC_MCMD;
                TAILQ_INSERT_TAIL(&pro->used_head, &pro->vnodes[node_i],
                                  fentry);
            }

            // printf("node[%d] status [%d] cmd_used[%d] cmd_left[%d]
            // full_count[%d]\n", node_i, pro->vnodes[node_i].status,
            // pro->vnodes[node_i].optimal_write_sec_used,
            // pro->vnodes[node_i].optimal_write_sec_left, full_count);
            node_i++;
            zone_num_in_node = 0;
            sec_num          = 0;
            // printf("node[%d] zone[%d] status[%d]\n", node_i, zone_i,
            // pro->vnodes[node_i].status);

            /* 剩余zone不够一个node，直接退出 */
            if (grp->zmd.entries - zone_i + 1 < ZTL_PRO_ZONE_NUM_INNODE) {
                log_infoa("ztl_pro_grp_node_init: Left %d zones\n",
                          grp->zmd.entries - zone_i + 1);
                break;
            }

            full_count = 0;
        }

        zmde = ztl()->zmd->get_fn(grp, zone_i, 0);

        if (zmde->addr.g.zone != zone_i || zmde->addr.g.grp != grp->id) {
            log_erra(
                "ztl_pro_grp_node_init: zmd entry address does not match "
                "[%d/%d] [%d/%d]",
                zmde->addr.g.grp, zmde->addr.g.zone, grp->id, zone_i);
            continue;
        }

        if ((zmde->flags & XZTL_ZMD_RSVD) || !(zmde->flags & XZTL_ZMD_AVLB)) {
            log_infoa("ztl_pro_grp_node_init: flags [%x]\n", zmde->flags);
            continue;
        }

        /* We are getting the full report here */
        zinfo = XNVME_ZND_REPORT_DESCR(
            rep, grp->id * core->media->geo.zn_grp + zone_i);

        zone            = &pro->vzones[zone_i - metadata_zone_num];
        zone->addr.addr = zmde->addr.addr;
        zone->capacity  = zinfo->zcap;
        zone->state     = zinfo->zs;
        zone->zmd_entry = zmde;
        zone->lock      = 0;
        pro->vnodes[node_i].vzones[zone_num_in_node++] = zone;

        switch (zinfo->zs) {
            case XNVME_SPEC_ZND_STATE_EMPTY:

                if ((zmde->flags & XZTL_ZMD_USED) ||
                    (zmde->flags & XZTL_ZMD_OPEN)) {
                    log_erra(
                        "ztl_pro_grp_node_init: device reported EMPTY zone, "
                        "but ZMD flag"
                        " does not match [%d/%d] [%x]",
                        grp->id, zone_i, zmde->flags);
                    continue;
                }

                ZDEBUG(ZDEBUG_PRO_GRP,
                       " ztl_pro_grp_node_init: [%d/%d] empty\n",
                       zmde->addr.g.grp, zmde->addr.g.zone);
                break;
            case XNVME_SPEC_ZND_STATE_EOPEN:
            case XNVME_SPEC_ZND_STATE_IOPEN:
            case XNVME_SPEC_ZND_STATE_CLOSED:
            case XNVME_SPEC_ZND_STATE_FULL:
                /* 默认每次启动zone顺序一致，因此被写过的zone也是在一个node中 */
                if (zinfo->zs == XNVME_SPEC_ZND_STATE_FULL) {
                    full_count++;
                    sec_num += zinfo->zcap;
                } else {
                    sec_num += zinfo->wp - zone->addr.g.sect;
                }
                // pro->vnodes[node_i].status = XZTL_ZMD_NODE_USED;
                ZDEBUG(ZDEBUG_PRO_GRP,
                       " ztl_pro_grp_node_init: ZINFO NOT CORRECT [%d/%d] , "
                       "status [%d]\n",
                       zmde->addr.g.grp, zmde->addr.g.zone, zinfo->zs);
                break;

            default:
                log_infoa(
                    "ztl_pro_grp_node_init: Unknown zone condition. zone [%d], "
                    "zs [%d]",
                    grp->id * core->media->geo.zn_grp + zone_i, zinfo->zs);
        }

        zmde->wptr = zmde->wptr_inflight = zinfo->wp;
    }

    log_infoa("ztl_pro_grp_node_init: Started. Group [%d].", grp->id);
    return XZTL_OK;
}

void ztl_pro_grp_exit(struct app_group *grp) {
    uint64_t cmd_entry_used[ZROCKS_LEVEL_NUM] = {0};
    uint64_t cmd_entry_valid[ZROCKS_LEVEL_NUM] = {0};
    struct ztl_pro_node_grp *pro = (struct ztl_pro_node_grp *)grp->pro;
    for (uint32_t i = 0; i < pro->nnodes; i++) {
        struct ztl_pro_node *node = &pro->vnodes[i];
        if (node->level >= 0) {
            cmd_entry_used[node->level] += node->optimal_write_sec_used;
            cmd_entry_valid[node->level] += node->nr_valid;
        }
    }

    for (int i = 0; i < ZROCKS_LEVEL_NUM; i++) {
        xztl_stats_node_inc(i, XZTL_CMD_ENTRY_USED, cmd_entry_used[i]);
        xztl_stats_node_inc(i, XZTL_CMD_ENTRY_VALID, cmd_entry_valid[i]);
    }
    _ztl_pro_grp_destroy_pro(pro);
    free(grp->pro);

    log_infoa("ztl_pro_grp_exit: Stopped. Group [%d].", grp->id);
}
