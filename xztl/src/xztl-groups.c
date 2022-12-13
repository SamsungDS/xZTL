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

#include <libxnvme.h>
#include <libxnvme_znd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <xztl-media.h>
#include <xztl.h>
#include <xztl-pro.h>

static LIST_HEAD(app_grp,
                 app_group) app_grp_head = LIST_HEAD_INITIALIZER(app_grp_head);

static struct app_group *groups_get(uint16_t grp_id) {
    struct app_group *grp;

    LIST_FOREACH(grp, &app_grp_head, entry) {
        if (grp->id == grp_id)
            return grp;
    }

    return NULL;
}

static int groups_get_list(struct app_group **list, uint16_t ngrp) {
    int               n = 0;
    int               i = ngrp - 1;
    struct app_group *grp;

    LIST_FOREACH(grp, &app_grp_head, entry) {
        if (i < 0)
            break;
        list[i] = grp;
        i--;
        n++;
    }

    return n;
}

static void groups_zmd_exit(void) {
    struct app_group *grp;

    LIST_FOREACH(grp, &app_grp_head, entry) {
        log_infoa("groups_zmd_exit: Zone MD stopped. Grp [%d]", grp->id);
        xnvme_buf_virt_free(grp->zmd.report);
        free(grp->zmd.tbl);
    }
}

static int groups_zmd_init(struct app_group *grp) {
    struct app_zmd   *zmd;
    struct xztl_mgeo *g;
    int               ret;
    struct xztl_core *core;
    get_xztl_core(&core);
    zmd = &grp->zmd;
    g   = &core->media->geo;

    zmd->entry_sz   = sizeof(struct app_zmd_entry);
    zmd->ent_per_pg = g->nbytes / zmd->entry_sz;
    zmd->entries    = g->zn_grp;

    zmd->tbl = calloc(g->zn_grp, zmd->entry_sz);
    if (!zmd->tbl)
        return XZTL_ZTL_GROUP_ERR;

    zmd->byte.magic = 0;

    ret = ztl()->zmd->load_fn(grp);
    if (ret) {
        log_erra("groups_zmd_init: err report [%d]\n", ret);
        goto FREE;
    }

    /* Create and flush zmd table if it does not exist */
    if (zmd->byte.magic == APP_MAGIC) {
        ret = ztl()->zmd->create_fn(grp);
        if (ret) {
            log_erra("groups_zmd_init: create_fn err [%d]\n", ret);
            goto FREE_REP;
        }
    }

    /* TODO: Setup tiny table */
    log_infoa("groups_zmd_init: Zone MD started. Grp [%d]", grp->id);

    return XZTL_OK;

FREE_REP:
    xnvme_buf_virt_free(zmd->report);
FREE:
    log_erra("groups_zmd_init: Zone MD startup failed. Grp [%d]", grp->id);
    free(zmd->tbl);
    return XZTL_ZTL_GROUP_ERR;
}

static void groups_free(void) {
    struct app_group *grp;
    while (!LIST_EMPTY(&app_grp_head)) {
        grp = LIST_FIRST(&app_grp_head);
        LIST_REMOVE(grp, entry);
        free(grp);
    }
}

static void groups_exit(void) {
    groups_zmd_exit();
    groups_free();

    log_info("ztl-groups: Closed successfully.");
}

static int groups_init(void) {
    struct app_group *grp;
    struct xztl_core *core;
    get_xztl_core(&core);
    uint16_t grp_i;

    for (grp_i = 0; grp_i < core->media->geo.ngrps; grp_i++) {
        grp = calloc(1, sizeof(struct app_group));
        if (!grp) {
            log_err("groups_init: Memory allocation failed\n");
            return XZTL_ZTL_GROUP_ERR;
        }

        grp->id = grp_i;

        /* Initialize zone metadata */
        if (groups_zmd_init(grp)) {
            log_err("groups_init: groups_zmd_init failed\n");
            goto FREE;
        }

        /* Enable group */
        app_grp_switch_on(grp);

        LIST_INSERT_HEAD(&app_grp_head, grp, entry);
    }

    log_infoa("ztl-groups: [%d] groups started. ", grp_i);

    return grp_i;

FREE:
    groups_free();
    return XZTL_ZTL_GROUP_ERR;
}

void ztl_grp_register(void) {
    ztl()->groups.init_fn     = groups_init;
    ztl()->groups.exit_fn     = groups_exit;
    ztl()->groups.get_fn      = groups_get;
    ztl()->groups.get_list_fn = groups_get_list;

    LIST_INIT(&app_grp_head);
}
