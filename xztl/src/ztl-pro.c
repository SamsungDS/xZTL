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

#include <stdlib.h>
#include <sys/queue.h>
#include <xztl-mempool.h>
#include <xztl.h>
#include <xztl-pro.h>
#include <xztl-metadata.h>

extern uint16_t    app_ngrps;
struct app_group **glist;
static uint16_t    cur_grp[ZTL_PRO_TYPES];

void ztl_pro_free(struct app_pro_addr *ctx) {
    uint32_t i;

    for (i = 0; i < ctx->naddr; i++)
        ztl_pro_grp_free(ctx->grp, ctx->addr[i].g.zone, ctx->nsec[i]);

    /* We assume a single group for now */
    app_grp_ctx_sub(ctx->grp);
}

int ztl_pro_new(uint32_t num, int32_t node_id, struct app_pro_addr *ctx,
                uint32_t start) {
    struct app_group *grp;
    int               ret;

    ZDEBUG(ZDEBUG_PRO, "ztl_pro_new: num [%d], start [%d], node_id [%d]", num,
           start, node_id);

    /* For now, we consider a single group */
    grp        = glist[0];
    ctx->naddr = 0;

    ret = ztl_pro_grp_get(grp, ctx, num, node_id, start);
    if (ret) {
        log_erra("ztl_pro_new: Get group zone failed. node_id [%d]\n", node_id);
        return XZTL_ZTL_PROV_ERR;
    }

    ctx->grp = grp;
    app_grp_ctx_add(grp);

    return ret;
}

int ztl_pro_get_node(struct ztl_queue_pool *q) {
    struct ztl_pro_node_grp *pro = (struct ztl_pro_node_grp *)(glist[0]->pro);
    return ztl_pro_grp_get_node(q, pro);
}

void ztl_pro_exit(void) {
    int ret;

    ret = ztl()->groups.get_list_fn(glist, app_ngrps);
    if (ret != app_ngrps)
        log_erra("ztl_pro_exit: Groups mismatch [%d,%d].", ret, app_ngrps);

    while (ret) {
        ret--;
        ztl_pro_grp_exit(glist[ret]);
    }
    xztl_mempool_destroy(XZTL_NODE_MGMT_ENTRY, 0);

    free(glist);
    log_info("ztl-pro: Global provisioning stopped.");
}

int ztl_pro_init(void) {
    int ret, grp_i = 0;

    glist = calloc(app_ngrps, sizeof(struct app_group *));
    if (!glist) {
        log_err("ztl_pro_init: glist is NULL.\n");
        return XZTL_ZTL_GROUP_ERR;
    }

    ret = ztl()->groups.get_list_fn(glist, app_ngrps);
    if (ret != app_ngrps) {
        log_erra("ztl_pro_init: get_list_fn ret [%d] app_ngrps [%d] failed\n",
                 ret, app_ngrps);
        goto FREE;
    }
    if (ztl_metadata_init(glist[0]))
        goto EXIT;
    for (grp_i = 0; grp_i < app_ngrps; grp_i++) {
        if (ztl_pro_grp_node_init(glist[grp_i])) {
            log_erra("ztl_pro_init: ztl_pro_grp_node_init failed grp_i [%d]\n",
                     grp_i);
            goto EXIT;
        }
    }

    memset(cur_grp, 0x0, sizeof(uint16_t) * ZTL_PRO_TYPES);
    log_info("ztl_pro_init: Global provisioning started.");

    return XZTL_OK;

EXIT:
    while (grp_i) {
        grp_i--;
        ztl_pro_grp_exit(glist[grp_i]);
    }

FREE:
    free(glist);
    return XZTL_ZTL_GROUP_ERR;
}

static struct app_pro_mod ztl_pro = {.mod_id      = LIBZTL_PRO,
                                     .name        = "LIBZTL-PRO",
                                     .init_fn     = ztl_pro_init,
                                     .exit_fn     = ztl_pro_exit,
                                     .new_fn      = ztl_pro_new,
                                     .free_fn     = ztl_pro_free,
                                     .get_node_fn = ztl_pro_get_node};

void ztl_pro_register(void) {
    ztl_mod_register(ZTLMOD_PRO, LIBZTL_PRO, &ztl_pro);
}
