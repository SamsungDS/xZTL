/* libztl: User-space Zone Translation Layer Library
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
#include <stdio.h>
#include <string.h>
#include <xapp.h>
#include <xapp-ztl.h>

extern uint16_t app_ngrps;
extern struct xapp_core core;

static int ztl_zmd_create (struct app_group *grp)
{
    uint64_t zn_i;
    struct app_zmd_entry *zn;
    struct app_zmd *zmd = &grp->zmd;
    struct xapp_mgeo *g;

    g = &core.media->geo;

    for (zn_i = 0; zn_i < zmd->entries; zn_i++) {
        zn = ((struct app_zmd_entry *) zmd->tbl) + zn_i;
        memset (zn, 0x0, sizeof (struct app_zmd_entry));
        zn->addr.addr   = 0;
        zn->addr.g.grp  = grp->id;
        zn->addr.g.zone = zn_i;
	zn->addr.g.sect = (g->sec_grp * grp->id) + (g->sec_zn * zn_i);

	zn->flags |= XAPP_ZMD_AVLB;
	zn->level = 0;
    }

    return 0;
}

static int ztl_zmd_load_report (struct app_group *grp)
{
    struct xapp_zn_mcmd cmd;
    int ret;

    cmd.opcode = XAPP_ZONE_MGMT_REPORT;
    cmd.addr.g.grp  = grp->id;
    cmd.addr.g.zone = core.media->geo.zn_grp * grp->id;
    cmd.nzones = core.media->geo.zn_grp;

    ret = xapp_media_submit_zn (&cmd);
    if (!ret) {
	grp->zmd.report = (struct znd_report *) cmd.opaque;
    }

    return ret;
}

static int ztl_zmd_load (struct app_group *grp)
{
    if (ztl_zmd_load_report (grp))
	return XAPP_ZTL_ZMD_REP;

    /* Set byte for table creation */
    grp->zmd.byte.magic = APP_MAGIC;

    return 0;
}

static int ztl_zmd_flush (struct app_group *grp)
{
    return 0;
}

static struct app_zmd_entry *ztl_zmd_get (struct app_group *grp, uint32_t zone)
{
    struct app_zmd *zmd = &grp->zmd;

    if (!zmd->tbl)
        return NULL;

    return ((struct app_zmd_entry *) zmd->tbl) + zone;
}

static void ztl_zmd_mark (struct app_group *grp, uint64_t index)
{

}

static void ztl_zmd_invalidate (struct app_group *grp,
                                        struct xapp_maddr *addr, uint8_t full)
{

}

static struct app_zmd_mod ztl_zmd = {
    .mod_id         = LIBZTL_ZMD,
    .name           = "LIBZTL-ZMD",
    .create_fn      = ztl_zmd_create,
    .flush_fn       = ztl_zmd_flush,
    .load_fn        = ztl_zmd_load,
    .get_fn         = ztl_zmd_get,
    .invalidate_fn  = ztl_zmd_invalidate,
    .mark_fn        = ztl_zmd_mark
};

void ztl_zmd_register (void)
{
    ztl_mod_register (ZTLMOD_ZMD, LIBZTL_ZMD, &ztl_zmd);
}
