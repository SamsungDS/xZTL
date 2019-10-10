#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xapp.h>

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
    }

    return 0;
}

static int ztl_zmd_load (struct app_group *grp)
{
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

void ztl_zmd_register (void) {
    ztl_mod_register (ZTLMOD_ZMD, LIBZTL_ZMD, &ztl_zmd);
}
