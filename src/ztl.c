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

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <xztl-media.h>
#include <xztl-ztl.h>
#include <xztl.h>
#include <ztl_metadata.h>

struct app_global __ztl;
static uint8_t    gl_fn; /* Positive if function has been called */

uint16_t app_ngrps;

static uint8_t app_modset_libztl[APP_MOD_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

inline struct app_global *ztl(void) {
    return &__ztl;
}

static int app_init_map_lock(struct app_mpe *mpe) {
    uint32_t ent_i;

    mpe->entry_mutex = malloc(sizeof(pthread_mutex_t) * mpe->entries);
    if (!mpe->entry_mutex)
        return -1;

    for (ent_i = 0; ent_i < mpe->entries; ent_i++) {
        if (pthread_mutex_init(&mpe->entry_mutex[ent_i], NULL))
            goto MUTEX;
    }
    return 0;

MUTEX:
    while (ent_i) {
        ent_i--;
        pthread_mutex_destroy(&mpe->entry_mutex[ent_i]);
    }
    free(mpe->entry_mutex);
    return -1;
}

static void app_exit_map_lock(struct app_mpe *mpe) {
    uint32_t ent_i;

    ent_i = mpe->entries;

    while (ent_i) {
        ent_i--;
        pthread_mutex_destroy(&mpe->entry_mutex[ent_i]);
    }
    free(mpe->entry_mutex);
}

static int app_mpe_init(void) {
    struct app_mpe *  mpe;
    struct xztl_mgeo *g;
    struct xztl_core *core;
    int               ret;
    get_xztl_core(&core);
    mpe = &ztl()->smap;
    g   = &core->media->geo;

    mpe->entry_sz   = sizeof(struct app_map_entry);
    mpe->ent_per_pg = (ZTL_MPE_PG_SEC * g->nbytes) / mpe->entry_sz;
    mpe->entries    = ZTL_MPE_CPGS;

    mpe->tbl = calloc(ZTL_MPE_CPGS, mpe->entry_sz);
    if (!mpe->tbl)
        return -1;

    mpe->byte.magic = APP_MAGIC;

    if (app_init_map_lock(mpe))
        goto FREE;

    /* Create and flush mpe table if it does not exist */
    if (mpe->byte.magic == APP_MAGIC) {
        ret = ztl()->mpe->create_fn();
        if (ret)
            goto LOCK;
    }

    /* TODO: Setup tiny table if we implement recovery at the ZTL */

    log_info("ztl-mpe: Persistent Mapping started.");

    return XZTL_OK;

LOCK:
    app_exit_map_lock(mpe);
FREE:
    free(mpe->tbl);
    log_err("ztl-mpe: Persistent Mapping startup failed.");

    return -1;
}

static void app_mpe_exit(void) {
    app_exit_map_lock(&ztl()->smap);

    free(ztl()->smap.tbl);
}

static int app_global_init(void) {
    int ret;

    ret = ztl()->pro->init_fn();
    if (ret) {
        log_erra("[ztl: Provisioning NOT started. ret: 0x%x\n", ret);
        return XZTL_ZTL_PROV_ERR;
    }

    ret = app_mpe_init();
    if (ret) {
        log_err("[ztl: Persistent mapping NOT started.\n");
        ret = XZTL_ZTL_MPE_ERR;
        goto PRO;
    }

    ret = ztl()->map->init_fn();
    if (ret) {
        log_err("[ztl: Mapping NOT started.\n");
        ret = XZTL_ZTL_MAP_ERR;
        goto MPE;
    }

    ret = ztl()->wca->init_fn();
    if (ret) {
        log_err("[ztl: Write-cache NOT started.\n");
        ret = XZTL_ZTL_WCA_ERR;
        goto MAP;
    }

    return XZTL_OK;

MAP:
    ztl()->map->exit_fn();
MPE:
    app_mpe_exit();
PRO:
    ztl()->pro->exit_fn();
    return ret;
}

static void app_global_exit(void) {
    /* Uncomment if we implement recovery at the ZTL.
     * Create a clean shutdown checkpoint in case of ZTL recovery

    if (ztl()->recovery->running) {
        if (ztl()->recovery->checkpoint_fn ())
            log_err ("[ox-app: Checkpoint has failed]");

        ztl()->recovery->exit_fn ();
    }*/

    ztl()->wca->exit_fn();
    ztl()->map->exit_fn();
    app_mpe_exit();
    ztl()->pro->exit_fn();
}

int ztl_mod_set(uint8_t *modset) {
    int   mod_i;
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
                    ztl()->zmd = (struct app_zmd_mod *)mod;
                    break;
                case ZTLMOD_PRO:
                    ztl()->pro = (struct app_pro_mod *)mod;
                    break;
                case ZTLMOD_MPE:
                    ztl()->mpe = (struct app_mpe_mod *)mod;
                    break;
                case ZTLMOD_MAP:
                    ztl()->map = (struct app_map_mod *)mod;
                    break;
                case ZTLMOD_WCA:
                    ztl()->wca = (struct app_wca_mod *)mod;
                    break;
                default:
                    log_erra("ztl: Invalid module ID: %d", mod_i);
            }
            log_infoa(
                "ztl: Module set. "
                "type: %d, id: %d, ptr: %p\n",
                mod_i, modset[mod_i], mod);
        }
    }
    return 0;
}

int ztl_mod_register(uint8_t modtype, uint8_t modid, void *mod) {
    if (modid >= APP_FN_SLOTS || modtype >= APP_MOD_COUNT || !mod) {
        log_erra(
            "ztl (mod_register): Module NOT registered. "
            "type: %d, id: %d, ptr: %p\n",
            modtype, modid, mod);
        return -1;
    }

    ztl()->mod_list[modtype][modid] = mod;
    app_modset_libztl[modtype]      = modid;

    log_infoa(
        "ztl: Module registered. "
        "type: %d, id: %d, ptr: %p\n",
        modtype, modid, mod);

    return 0;
}

void ztl_exit(void) {
    /*
    log_info("ztl: Closing...");

    app_global_exit();
    ztl()->groups.exit_fn();
    */
    log_info("ztl: Closed successfully.");
}

int ztl_init(void) {
    int ret, ngrps;

    gl_fn     = 0;
    app_ngrps = 0;

    ztl_grp_register();

    log_info("ztl: Starting...");

    if (ztl_mod_set(app_modset_libztl))
        return -1;

    ngrps = ztl()->groups.init_fn();
    if (ngrps <= 0)
        return XZTL_ZTL_GROUP_ERR;

    app_ngrps = ngrps;

    ret = app_global_init();
    if (ret) {
        ztl()->groups.exit_fn();
        return ret;
    }

    log_info("ztl: Started successfully.");

    return XZTL_OK;
}
