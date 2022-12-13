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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xztl.h>
#include <xztl-pro.h>

// extern uint16_t app_ngrps;
static int ztl_zmd_create(struct app_group *grp) {
    uint64_t              zn_i;
    struct app_zmd_entry *zn;
    struct app_zmd       *zmd = &grp->zmd;
    struct xztl_mgeo     *g;
    struct xztl_core     *core;
    get_xztl_core(&core);
    g = &core->media->geo;

    for (zn_i = 0; zn_i < zmd->entries; zn_i++) {
        zn = ((struct app_zmd_entry *)zmd->tbl) + zn_i;
        memset(zn, 0x0, sizeof(struct app_zmd_entry));
        zn->addr.addr   = 0;
        zn->addr.g.grp  = grp->id;
        zn->addr.g.zone = zn_i;
        zn->addr.g.sect = (g->sec_grp * grp->id) + (g->sec_zn * zn_i);

        zn->flags |= XZTL_ZMD_AVLB;
        zn->level         = 0;
        zn->wptr_inflight = zn->wptr = zn->addr.g.sect;
    }

    return XZTL_OK;
}

static int ztl_zmd_load_report(struct app_group *grp) {
    struct xztl_core *core;
    get_xztl_core(&core);
    struct xztl_zn_mcmd cmd;
    int                 ret;

    cmd.opcode      = XZTL_ZONE_MGMT_REPORT;
    cmd.addr.g.grp  = grp->id;
    cmd.addr.g.zone = core->media->geo.zn_grp * grp->id * 1UL;
    cmd.nzones      = core->media->geo.zn_grp;

    ret = xztl_media_submit_zn(&cmd);
    if (!ret) {
        grp->zmd.report = (struct xnvme_znd_report *)cmd.opaque;
    }

    if (ret) {
        log_erra("ztl_zmd_load_report: zmd err status [%d]\n", cmd.status);
    }

    return ret;
}

static int ztl_zmd_load(struct app_group *grp) {
    if (ztl_zmd_load_report(grp))
        return XZTL_ZTL_ZMD_REP;

    /* Set byte for table creation */
    grp->zmd.byte.magic = APP_MAGIC;

    return XZTL_OK;
}

static int ztl_zmd_flush(struct app_group *grp) {
    return XZTL_OK;
}

static struct app_zmd_entry *ztl_zmd_get(struct app_group *grp, uint64_t zone,
                                         uint8_t by_offset) {
    struct app_zmd   *zmd = &grp->zmd;
    struct xztl_core *core;
    get_xztl_core(&core);

    if (!zmd->tbl)
        return NULL;

    if (by_offset)
        zone = zone / core->media->geo.sec_zn;

    return ((struct app_zmd_entry *)zmd->tbl) + zone;
}

static void ztl_zmd_mark(struct app_group *grp, uint64_t index) {
}

static void ztl_zmd_invalidate(struct app_group *grp, struct xztl_maddr *addr,
                               uint8_t full) {
}

static struct app_zmd_mod ztl_zmd = {.mod_id        = LIBZTL_ZMD,
                                     .name          = "LIBZTL-ZMD",
                                     .create_fn     = ztl_zmd_create,
                                     .flush_fn      = ztl_zmd_flush,
                                     .load_fn       = ztl_zmd_load,
                                     .get_fn        = ztl_zmd_get,
                                     .invalidate_fn = ztl_zmd_invalidate,
                                     .mark_fn       = ztl_zmd_mark};

void ztl_zmd_register(void) {
    ztl_mod_register(ZTLMOD_ZMD, LIBZTL_ZMD, &ztl_zmd);
}
