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

#include <sched.h>
#include <unistd.h>
#include <xztl-media.h>
#include <xztl-ztl.h>
#include <xztl.h>
#include <ztl.h>

#define XZTL_CTX_NVME_DEPTH 128

struct xztl_thread xtd[ZTL_TH_NUM];

extern struct app_group **glist;

static int ztl_thd_allocNode_for_thd(struct xztl_thread *tdinfo) {
    struct ztl_pro_node_grp *pro = (struct ztl_pro_node_grp *)(glist[0]->pro);

    uint32_t snodeidx = 0;
    uint32_t cnt      = 0;

    pthread_spin_lock(&pro->spin);

    for (int i = 0; i < pro->totalnode && cnt < ZTL_ALLOC_NODE_NUM; i++) {
        if (XZTL_ZMD_NODE_FREE == pro->vnodes[i].status &&
            pro->vnodes[i].zone_num == ZTL_PRO_ZONE_NUM_INNODE) {
            pro->vnodes[i].status = XZTL_ZMD_NODE_USED;
            STAILQ_INSERT_TAIL(&(tdinfo->free_head), &(pro->vnodes[i]), fentry);
            cnt++;
            tdinfo->nfree++;
        }
    }
    pthread_spin_unlock(&pro->spin);

    return cnt;
}

static uint32_t ztl_thd_getNodeId(struct xztl_thread *tdinfo) {
    struct ztl_pro_node *node = STAILQ_FIRST(&tdinfo->free_head);
    int                  cnt  = 0;

    if (!node) {
        cnt = ztl_thd_allocNode_for_thd(tdinfo);
        if (cnt == 0) {
            log_err("ztl_thd_getNodeId: No available node resource.\n");
            return XZTL_ZTL_WCA_ERR;
        } else {
            node = STAILQ_FIRST(&tdinfo->free_head);
            if (!node) {
                log_err("ztl_thd_getNodeId: Invalid node.\n");
                return XZTL_ZTL_WCA_ERR;
            }
        }
    }

    STAILQ_REMOVE_HEAD(&tdinfo->free_head, fentry);
    tdinfo->nfree--;

    return node->id;
}

static struct xztl_thread *ztl_thd_getXtd(int tid) {
    return &xtd[tid];
}

static int ztl_thd_get_resource(void) {
    int tid, rettid = -1;

    for (tid = 0; tid < ZTL_TH_NUM; tid++) {
        if (!xtd[tid].usedflag) {
            xtd[tid].usedflag = true;
            rettid            = tid;
            break;
        }
    }
    return rettid;
}

static void ztl_thd_put_resource(int tid) {
    xtd[tid].usedflag = false;
}

static int _ztl_thd_init(struct xztl_thread *td) {
    int mcmd_id;

    td->usedflag = false;

    for (mcmd_id = 0; mcmd_id < ZTL_TH_RC_NUM; mcmd_id++) {
        td->prp[mcmd_id]  = xztl_media_dma_alloc(64 * 1024);
        td->mcmd[mcmd_id] = aligned_alloc(64, sizeof(struct xztl_io_mcmd));
    }

    td->prov = xztl_media_dma_alloc(sizeof(struct app_pro_addr));
    if (!td->prov) {
        log_err(
            "_ztl_thd_init: Thread resource (data buffer) allocation error.");
        return XZTL_ZTL_WCA_ERR;
    }

    struct app_pro_addr *prov = (struct app_pro_addr *)td->prov;
    prov->grp                 = glist[0];

    td->tctx = xztl_ctx_media_init(XZTL_CTX_NVME_DEPTH);
    if (!td->tctx) {
        log_err("_ztl_thd_init: Thread resource (tctx) allocation error.");
        return XZTL_ZTL_WCA_ERR;
    }

    STAILQ_INIT(&td->free_head);
    return XZTL_OK;
}

static int ztl_thd_init(void) {
    int tid, ret;

    for (tid = 0; tid < ZTL_TH_NUM; tid++) {
        xtd[tid].tid = tid;
        ret          = _ztl_thd_init(&xtd[tid]);
        if (!ret) {
        } else {
            log_erra("ztl_thd_init: thread [%d] created failed.", tid);
            break;
        }
    }

    return ret;
}

static void ztl_thd_exit(void) {
    int                 tid, ret;
    struct xztl_thread *td = NULL;
    int                 mcmd_id;

    for (tid = 0; tid < ZTL_TH_NUM; tid++) {
        td = &xtd[tid];
        xztl_ctx_media_exit(td->tctx);
        xztl_media_dma_free(td->prov);

        for (mcmd_id = 0; mcmd_id < ZTL_TH_RC_NUM; mcmd_id++) {
            xztl_media_dma_free(td->prp[mcmd_id]);
            free(td->mcmd[mcmd_id]);
        }
    }

    log_info("ztl_thd_exit: ztl-thd stopped.\n");
}

static struct app_thd_mod libztl_thd = {.mod_id     = LIBZTL_THD,
                                        .name       = "LIBZTL-THD",
                                        .init_fn    = ztl_thd_init,
                                        .exit_fn    = ztl_thd_exit,
                                        .get_fn     = ztl_thd_get_resource,
                                        .put_fn     = ztl_thd_put_resource,
                                        .get_nid_fn = ztl_thd_getNodeId,
                                        .get_xtd_fn = ztl_thd_getXtd};

void ztl_thd_register(void) {
    ztl_mod_register(ZTLMOD_THD, LIBZTL_THD, &libztl_thd);
}
