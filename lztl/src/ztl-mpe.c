#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xapp.h>
#include <xapp-ztl.h>
#include <lztl.h>

extern uint16_t app_ngrps;
extern struct xapp_core core;

uint8_t app_map_new;
static struct app_mpe *smap;

static int ztl_mpe_create (void)
{
    int i;
    struct app_map_entry *ent;

    for (i = 0; i < smap->entries; i++) {
	ent = ((struct app_map_entry *) smap->tbl) + i;
	memset (ent, 0x0, sizeof (struct app_map_entry));
    }

    app_map_new = 1;

    return 0;
}

static int ztl_mpe_load (void)
{
    smap = &ztl()->smap;

    /* Set byte for table creation */
    smap->byte.magic = APP_MAGIC;

    return 0;
}

static int ztl_mpe_flush (void)
{
    return 0;
}

static struct app_map_entry *ztl_mpe_get (uint32_t index)
{
    /* TODO: If index > n_entries, increase size of table */

    return ((struct app_map_entry *) smap->tbl) + index;
}

static void ztl_mpe_mark (uint32_t index)
{

}

static struct app_mpe_mod ztl_mpe = {
    .mod_id         = LIBZTL_ZMD,
    .name           = "LIBZTL-ZMD",
    .create_fn      = ztl_mpe_create,
    .flush_fn       = ztl_mpe_flush,
    .load_fn        = ztl_mpe_load,
    .get_fn         = ztl_mpe_get,
    .mark_fn        = ztl_mpe_mark
};

void ztl_mpe_register (void)
{
    ztl_mod_register (ZTLMOD_MPE, LIBZTL_MPE, &ztl_mpe);
}
