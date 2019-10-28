#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/queue.h>
#include <xapp.h>
#include <xapp-media.h>
#include <xapp-ztl.h>

extern struct xapp_core core;

struct app_global __ztl;
static uint8_t gl_fn; /* Positive if function has been called */
uint16_t app_ngrps;

uint8_t app_modset_libztl[APP_MOD_COUNT] = {0,0,0,0,0,0,0,0,0};

inline struct app_global *ztl (void) {
    return &__ztl;
}

static int app_init_map_lock (struct app_mpe *mpe)
{
    uint32_t ent_i;

    mpe->entry_mutex = malloc (sizeof(pthread_mutex_t) * mpe->entries);
    if (!mpe->entry_mutex)
	return -1;

    for (ent_i = 0; ent_i < mpe->entries; ent_i++) {
	if (pthread_mutex_init (&mpe->entry_mutex[ent_i], NULL))
	    goto MUTEX;
    }

    return 0;

MUTEX:
    while (ent_i) {
	ent_i--;
	pthread_mutex_destroy (&mpe->entry_mutex[ent_i]);
    }
    free (mpe->entry_mutex);
    return -1;
}

static void app_exit_map_lock (struct app_mpe *mpe)
{
    uint32_t ent_i;

    ent_i = mpe->entries;

    while (ent_i) {
	ent_i--;
	pthread_mutex_destroy (&mpe->entry_mutex[ent_i]);
    }
    free (mpe->entry_mutex);
}

static int app_mpe_init (void)
{
    struct app_mpe *mpe;
    struct xapp_mgeo *g;
    int ret;

    mpe = &ztl()->smap;
    g   = &core.media->geo;

    mpe->entry_sz   = sizeof (struct app_map_entry);
    mpe->ent_per_pg = (ZTL_MPE_PG_SEC * g->nbytes) / mpe->entry_sz;
    mpe->entries    = ZTL_MPE_CPGS;

    mpe->tbl = calloc (mpe->entry_sz, ZTL_MPE_CPGS);
    if (!mpe->tbl)
	return -1;

    mpe->byte.magic = 0;

    ret = ztl()->mpe->load_fn ();
    if (ret)
	goto FREE;

    if (app_init_map_lock (mpe))
	goto FREE;

    /* Create and flush mpe table if it does not exist */
    if (mpe->byte.magic == APP_MAGIC) {
	ret = ztl()->mpe->create_fn ();
	if (ret)
	    goto LOCK;
    }

    /* TODO: Setup tiny table */

    log_info ("ztl: Persistent Mapping started.");

    return XAPP_OK;

LOCK:
    app_exit_map_lock (mpe);
FREE:
    free (mpe->tbl);
    log_err ("ztl: Persistent Mapping startup failed.");

    return -1;
}

static void app_mpe_exit (void)
{
    app_exit_map_lock (&ztl()->smap);

    free (ztl()->smap.tbl);

    log_info ("ztl: Persistent Mapping stopped.");
}

static int app_global_init (void)
{
    if (ztl()->pro->init_fn ()) {
        log_err ("[ztl: Provisioning NOT started.\n");
        return XAPP_ZTL_PROV_ERR;
    }

    if (app_mpe_init()) {
        log_err ("[ztl: Persistent mapping NOT started.\n");
        return XAPP_ZTL_MPE_ERR;
    }

    if (ztl()->map->init_fn ()) {
        log_err ("[ztl: Mapping NOT started.\n");
        return XAPP_ZTL_MAP_ERR;
    }

    return XAPP_OK;
}

static void app_global_exit (void)
{
    /* Create a clean shutdown checkpoint */
    /*if (ztl()->recovery->running) {
        if (ztl()->recovery->checkpoint_fn ())
            log_err ("[ox-app: Checkpoint has failed]");

        ztl()->recovery->exit_fn ();
    }*/

    ztl()->map->exit_fn ();
    app_mpe_exit ();
    ztl()->pro->exit_fn ();
}

int ztl_mod_set (uint8_t *modset)
{
    int mod_i;
    void *mod;

    for (mod_i = 0; mod_i < APP_MOD_COUNT; mod_i++)
        if (modset[mod_i] >= APP_FN_SLOTS)
            return -1;

    for (mod_i = 0; mod_i < APP_MOD_COUNT; mod_i++) {

        /* Set pointer if module ID is positive */
        if (modset[mod_i]) {
            mod = ztl()->mod_list[mod_i][modset[mod_i]];
            if (!mod)
                continue;

            switch (mod_i) {
                case ZTLMOD_ZMD:
                    ztl()->zmd = (struct app_zmd_mod *) mod;
                    break;
                case ZTLMOD_PRO:
                    ztl()->pro = (struct app_pro_mod *) mod;
                    break;
                case ZTLMOD_MPE:
                    ztl()->mpe = (struct app_mpe_mod *) mod;
                    break;
		case ZTLMOD_MAP:
		    ztl()->map = (struct app_map_mod *) mod;
            }

            log_infoa ("ztl: Module set. "
                    "type: %d, id: %d, ptr: %p\n", mod_i, modset[mod_i], mod);
        }
    }
    return 0;
}

int ztl_mod_register (uint8_t modtype, uint8_t modid, void *mod)
{
    if (modid >= APP_FN_SLOTS || modtype >= APP_MOD_COUNT || !mod) {
        log_erra ("ztl (mod_register): Module NOT registered. "
                           "type: %d, id: %d, ptr: %p\n", modtype, modid, mod);
        return -1;
    }

    ztl()->mod_list[modtype][modid] = mod;
    app_modset_libztl[modtype] = modid;

    log_infoa ("ztl: Module registered. "
                           "type: %d, id: %d, ptr: %p\n", modtype, modid, mod);

    return 0;
}

void ztl_exit (void)
{
    log_info ("ztl: Closing...");

    app_global_exit ();
    ztl()->groups.exit_fn ();

    log_info ("ztl: Closed successfully.");
}

int ztl_init (void)
{
    int ret, ngrps;

    gl_fn = 0;
    app_ngrps = 0;

    ztl_grp_register ();

    log_info ("ztl: Starting...");

    if (ztl_mod_set (app_modset_libztl))
        return -1;

    ngrps = ztl()->groups.init_fn ();
    if (ngrps <= 0)
	return XAPP_ZTL_GROUP_ERR;

    app_ngrps = ngrps;

    ret = app_global_init ();
    if (ret) {
	ztl()->groups.exit_fn ();
	return ret;
    }

    log_info ("ztl: Started successfully.");

    return XAPP_OK;
}
