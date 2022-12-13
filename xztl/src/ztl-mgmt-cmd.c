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
#include <xztl-stats.h>
#include <xztl-pro.h>

#define MGMT_MAX_RETRY   3
#define ZTL_NODE_MGMT_SZ 384

struct xztl_mthread_info mthread;
struct xnvme_node_mgmt_entry {
    struct app_group     *grp;
    struct ztl_pro_node  *node;
    struct xztl_mp_entry *mp_entry;
    int32_t               op_code;
    STAILQ_ENTRY(xnvme_node_mgmt_entry) entry;
};

static pthread_spinlock_t xnvme_mgmt_spin;
static STAILQ_HEAD(xnvme_emu_head, xnvme_node_mgmt_entry) submit_head;

int ztl_pro_grp_reset_all_zones(struct app_group *grp) {
    struct xztl_zn_mcmd cmd;
    int                 ret;
    cmd.opcode = XZTL_ZONE_MGMT_RESET;
    cmd.nzones = grp->zmd.entries;

    ret = xztl_media_submit_zn(&cmd);
    if (ret || cmd.status) {
        log_erra(
            "ztl_pro_grp_reset_all_zones: All zone reset failure. ret [%d] "
            "status [%d]\n",
            ret, cmd.status);
    }
    return XZTL_OK;
}

static int ztl_mgmt_node_reset_zn(struct ztl_pro_zone *zone) {
    struct xztl_zn_mcmd   cmd;
    struct app_zmd_entry *zmde;
    int                   ret = 0;

    zmde          = zone->zmd_entry;
    cmd.opcode    = XZTL_ZONE_MGMT_RESET;
    cmd.addr.addr = zone->addr.addr;
    cmd.nzones    = 1;
    ret           = xztl_media_submit_zn(&cmd);

    if (ret || cmd.status) {
        log_erra(
            "ztl_pro_node_reset_zn: Zone: [%u] reset failure. status [%d]\n",
            zone->addr.g.zone, cmd.status);
        goto ERR;
    }

    ATOMIC_SWAP(&zmde->wptr, zmde->wptr, zone->addr.g.sect);
    ATOMIC_SWAP(&zmde->wptr_inflight, zmde->wptr_inflight, zone->addr.g.sect);
ERR:
    return ret;
}

static int ztl_mgmt_node_reset(struct app_group    *grp,
                               struct ztl_pro_node *node) {
    struct ztl_pro_zone     *zone;
    struct ztl_pro_node_grp *node_grp = grp->pro;
    int                      ret;

    for (int i = 0; i < ZTL_PRO_ZONE_NUM_INNODE; i++) {
        zone = node->vzones[i];
        ret  = ztl_mgmt_node_reset_zn(zone);
        if (ret) {
            log_erra("ztl_pro_grp_node_reset: Zone [%u] reset failure\n",
                     zone->addr.g.zone);
            ATOMIC_ADD(&node->nr_reset_err, 1);
            goto ERR;
        }
    }

    node->status                 = XZTL_ZMD_NODE_FREE;
    node->optimal_write_sec_left = ZTL_PRO_OPT_SEC_NUM_INNODE;
    node->optimal_write_sec_used = 0;
    node->nr_valid               = 0;
    node->level                  = -1;
    pthread_spin_lock(&node_grp->spin);
    TAILQ_REMOVE(&node_grp->used_head, node, fentry);
    TAILQ_INSERT_TAIL(&node_grp->free_head, node, fentry);
    pthread_spin_unlock(&node_grp->spin);

    ATOMIC_ADD(&node_grp->nfree, 1);

ERR:
    return ret;
}

void ztl_mgmt_clear_invalid_node(struct app_group *grp) {
    struct ztl_pro_node_grp *pro = (struct ztl_pro_node_grp *)grp->pro;
    for (int node_id = 0; node_id < pro->nnodes; node_id++) {
        struct ztl_pro_node *node = &pro->vnodes[node_id];
        if (node->nr_valid == 0 && node->status != XZTL_ZMD_NODE_FREE) {
            ztl_mgmt_node_reset(grp, node);
            log_infoa("clear node[%d] optimal_write_sec_used[%lu]\n", node_id,
                      node->optimal_write_sec_used);
        }
    }
}

static int ztl_mgmt_node_finish(struct app_group    *grp,
                                struct ztl_pro_node *node) {
    struct ztl_pro_zone *zone;
    struct xztl_zn_mcmd  cmd;
    int                  ret;

    for (int i = 0; i < ZTL_PRO_ZONE_NUM_INNODE; i++) {
        /* Explicit closes the zone */
        zone          = node->vzones[i];
        cmd.opcode    = XZTL_ZONE_MGMT_FINISH;
        cmd.addr.addr = zone->addr.addr;
        cmd.nzones    = 1;
        ret           = xztl_media_submit_zn(&cmd);

        if (ret || cmd.status) {
            log_erra(
                "ztl_pro_grp_node_finish: Zone finish failure [%u] status "
                "[%d]\n",
                zone->addr.g.zone, cmd.status);
            ATOMIC_ADD(&node->nr_finish_err, 1);
            goto ERR;
        }
        zone->zmd_entry->wptr = zone->addr.g.sect + zone->capacity;
    }

ERR:
    return ret;
}

static int ztl_mgmt_submit_reset_finish(struct app_group    *grp,
                                        struct ztl_pro_node *node,
                                        int32_t              op_code) {
    struct xztl_mp_entry *mp_cmd;
    mp_cmd = xztl_mempool_get(XZTL_NODE_MGMT_ENTRY, 0);
    if (!mp_cmd) {
        log_err("ztl_pro_grp_submit_mgmt: Mempool failed.\n");
        return XZTL_ZTL_PROV_GRP_ERR;
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
    return XZTL_OK;
}

static void *ztl_mgmt_thd_process(void *args) {
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
            int retry = 0;
        MGMT_FAIL:
            if (et->op_code == ZTL_MGMG_FULL_ZONE) {
                ret = ztl_mgmt_node_finish(et->grp, et->node);
            } else {
                ret = ztl_mgmt_node_reset(et->grp, et->node);
            }

            if (ret) {
                xztl_stats_inc(XZTL_STATS_MGMT_FAIL, 1);
                log_erra("znd_pro_grp_process_mgmt: ret [%d]\n", ret);
                retry++;
                if (retry < MGMT_MAX_RETRY) {
                    goto MGMT_FAIL;
                }
            }

            xztl_mempool_put(et->mp_entry, XZTL_NODE_MGMT_ENTRY, 0);
            goto NEXT;
        }
    }

    return XZTL_OK;
}

static int ztl_mgmt_init() {
    STAILQ_INIT(&submit_head);
    if (pthread_spin_init(&xnvme_mgmt_spin, 0)) {
        return XZTL_ZTL_PROV_GRP_ERR;
    }

    int ret =
        xztl_mempool_create(XZTL_NODE_MGMT_ENTRY, 0, ZTL_NODE_MGMT_SZ,
                            sizeof(struct xnvme_node_mgmt_entry), NULL, NULL);
    if (ret) {
        log_err("create node_entry failed\n");
    }

    pthread_create(&mthread.comp_tid, NULL, ztl_mgmt_thd_process, NULL);
    return ret;
}

static void ztl_mgmt_exit() {
    while (!STAILQ_EMPTY(&submit_head)) {
        usleep(1);
    }

    mthread.comp_active = 0;
    pthread_join(mthread.comp_tid, NULL);
    pthread_spin_destroy(&xnvme_mgmt_spin);
}

static struct app_mgmt_mod ztl_mgmt = {
    .mod_id  = LIBZTL_MGMT,
    .name    = "LIBZTL-MGMT",
    .init_fn = ztl_mgmt_init,
    .exit_fn = ztl_mgmt_exit,
    // .open_fn        = ztl_mgmt_open,
    // .close_fn       = ztl_mgmt_close,
    .reset_fn  = ztl_mgmt_submit_reset_finish,
    .finish_fn = ztl_mgmt_submit_reset_finish,
    .clear_fn  = ztl_mgmt_clear_invalid_node};

void ztl_mgmt_register(void) {
    ztl_mod_register(ZTLMOD_MGMT, LIBZTL_MGMT, &ztl_mgmt);
}
