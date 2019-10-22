#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/queue.h>
#include <xapp.h>
#include <xapp-media.h>
#include <xapp-ztl.h>

extern struct core_struct core;

struct app_global __ztl;
static uint8_t gl_fn; /* Positive if function has been called */
uint16_t app_ngrps;

uint8_t app_modset_libztl[APP_MOD_COUNT] = {0,0,0,0,0,0,0,0,0};

inline struct app_global *ztl (void) {
    return &__ztl;
}

static int app_global_init (void)
{
    if (ztl()->pro->init_fn ()) {
        log_err ("[ztl: Provisioning NOT started.\n");
        return XAPP_ZTL_PROV_ERR;
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
