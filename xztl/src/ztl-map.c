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
#include <xztl.h>
#include <xztl-mods.h>

#define MAP_BUF_PGS  8192 /* 256 MB per cache with 32KB page */
#define MAP_N_CACHES 1

#define MAP_ADDR_FLAG ((1 & AND64) << 63)

struct map_cache_entry {
    uint8_t              dirty;
    uint8_t             *buf;
    uint32_t             buf_sz;
    struct app_map_entry addr; /* Stores the address while pg is cached */
    struct map_md_addr  *md_entry;
    struct map_cache    *cache;
    LIST_ENTRY(map_cache_entry) f_entry;
    TAILQ_ENTRY(map_cache_entry) u_entry;
};

struct map_cache {
    struct map_cache_entry *pg_buf;
    LIST_HEAD(mb_free_l, map_cache_entry) mbf_head;
    TAILQ_HEAD(mb_used_l, map_cache_entry) mbu_head;
    pthread_spinlock_t mb_spin;
    pthread_mutex_t    mutex;
    uint32_t           nfree;
    uint32_t           nused;
    uint16_t           id;
};

static struct map_cache *map_caches;
static volatile uint8_t  cp_running;

/* The mapping strategy ensures the entry size matches with the NVM pg size */
static uint32_t map_pg_sz;
static uint64_t map_ent_per_pg;

static int map_nvm_read(struct map_cache_entry *ent) {
    return XZTL_OK;
}

static int map_evict_pg_cache(struct map_cache *cache, uint8_t is_checkpoint) {
    struct map_cache_entry *cache_ent;

    pthread_spin_lock(&cache->mb_spin);
    cache_ent = TAILQ_FIRST(&cache->mbu_head);
    if (!cache_ent) {
        pthread_spin_unlock(&cache->mb_spin);
        log_err("map_evict_pg_cache: cache_entis NULL.\n");
        return XZTL_ZTL_MAP_ERR;
    }

    TAILQ_REMOVE(&cache->mbu_head, cache_ent, u_entry);
    cache->nused--;
    pthread_spin_unlock(&cache->mb_spin);

    /* TODO: Evict the page if recovery is done at the ZTL */

    cache_ent->md_entry->addr = cache_ent->addr.addr;
    cache_ent->addr.addr      = 0;
    cache_ent->md_entry       = NULL;

    pthread_spin_lock(&cache->mb_spin);
    LIST_INSERT_HEAD(&cache->mbf_head, cache_ent, f_entry);
    cache->nfree++;
    pthread_spin_unlock(&cache->mb_spin);

    return XZTL_OK;
}

static int map_load_pg_cache(struct map_cache   *cache,
                             struct map_md_addr *md_entry, uint64_t first_id,
                             uint32_t pg_off) {
    struct map_cache_entry *cache_ent;
    struct app_map_entry   *map_ent;
    uint64_t                ent_id;

WAIT:
    if (LIST_EMPTY(&cache->mbf_head)) {
        if (cp_running) {
            usleep(200);
            goto WAIT;
        }

        pthread_mutex_lock(&cache->mutex);
        if (map_evict_pg_cache(cache, 0)) {
            log_err("map_load_pg_cache: map_evict_pg_cache err.\n");
            pthread_mutex_unlock(&cache->mutex);
            return XZTL_ZTL_MAP_ERR;
        }
        pthread_mutex_unlock(&cache->mutex);
    }

    pthread_spin_lock(&cache->mb_spin);
    cache_ent = LIST_FIRST(&cache->mbf_head);
    if (!cache_ent) {
        log_err("map_load_pg_cache: LIST_FIRST cache_entis NULL.\n");
        pthread_spin_unlock(&cache->mb_spin);
        return XZTL_ZTL_MAP_ERR;
    }

    LIST_REMOVE(cache_ent, f_entry);
    cache->nfree--;
    pthread_spin_unlock(&cache->mb_spin);

    cache_ent->md_entry = md_entry;

    /* If metadata entry PPA is zero, mapping page does not exist yet */
    if (!md_entry->addr) {
        for (ent_id = 0; ent_id < map_ent_per_pg; ent_id++) {
            map_ent =
                &((struct app_map_entry *)cache_ent->buf)[ent_id];  // NOLINT
            map_ent->addr = 0x0;
        }
        cache_ent->dirty = 1;
    } else {
        if (map_nvm_read(cache_ent)) {
            cache_ent->md_entry  = NULL;
            cache_ent->addr.addr = 0;

            pthread_spin_lock(&cache->mb_spin);
            LIST_INSERT_HEAD(&cache->mbf_head, cache_ent, f_entry);
            cache->nfree++;
            pthread_spin_unlock(&cache->mb_spin);
            log_err("map_load_pg_cache: map_nvm_read err.\n");

            return XZTL_ZTL_MAP_ERR;
        }

        /* Cache entry PPA is set after the read completes */
    }

    pthread_spin_lock(&cache->mb_spin);
    md_entry->addr = (uint64_t)cache_ent;
    md_entry->addr |= MAP_ADDR_FLAG;

    TAILQ_INSERT_TAIL(&cache->mbu_head, cache_ent, u_entry);
    cache->nused++;
    pthread_spin_unlock(&cache->mb_spin);

    ZDEBUG(ZDEBUG_MAP, "map_load_pg_cache: Page cache loaded. Offset [0x%lu]",
           (uint64_t)cache_ent->addr.g.offset);

    return XZTL_OK;
}

static int map_init_cache(struct map_cache *cache) {
    uint32_t pg_i;

    cache->pg_buf = calloc(MAP_BUF_PGS, sizeof(struct map_cache_entry));
    if (!cache->pg_buf) {
        log_err("map_init_cache: Map cache initialization failed.\n");
        return XZTL_ZTL_MAP_ERR;
    }

    if (pthread_spin_init(&cache->mb_spin, 0)) {
        log_err("map_init_cache: pthread_spin_init cache->mb_spin failed.\n");
        goto FREE_BUF;
    }
    if (pthread_mutex_init(&cache->mutex, NULL)) {
        log_err("map_init_cache: pthread_mutex_init cache->mutex failed.\n");
        goto SPIN;
    }
    cache->mbf_head.lh_first = NULL;
    LIST_INIT(&cache->mbf_head);
    TAILQ_INIT(&cache->mbu_head);
    cache->nfree = 0;
    cache->nused = 0;

    for (pg_i = 0; pg_i < MAP_BUF_PGS; pg_i++) {
        cache->pg_buf[pg_i].dirty     = 0;
        cache->pg_buf[pg_i].buf_sz    = map_pg_sz;
        cache->pg_buf[pg_i].addr.addr = 0x0;
        cache->pg_buf[pg_i].md_entry  = NULL;
        cache->pg_buf[pg_i].cache     = cache;

        cache->pg_buf[pg_i].buf = calloc(1, map_pg_sz);
        if (!cache->pg_buf[pg_i].buf) {
            log_erra("map_init_cache: pg_buf pg_i [%u] buf is null.\n", pg_i);
            goto FREE_PGS;
        }

        LIST_INSERT_HEAD(&cache->mbf_head, &cache->pg_buf[pg_i], f_entry);
        cache->nfree++;
    }

    return XZTL_OK;

FREE_PGS:
    while (pg_i) {
        pg_i--;
        LIST_REMOVE(&cache->pg_buf[pg_i], f_entry);
        cache->nfree--;
        free(cache->pg_buf[pg_i].buf);
    }
    pthread_mutex_destroy(&cache->mutex);
SPIN:
    pthread_spin_destroy(&cache->mb_spin);
FREE_BUF:
    free(cache->pg_buf);
    return XZTL_ZTL_MAP_ERR;
}

static void map_flush_cache(struct map_cache *cache, uint8_t full) {
    /* TODO: Persist all pages */
}

static void map_flush_all_caches(void) {
    uint32_t cache_i = MAP_N_CACHES;

    while (cache_i) {
        cache_i--;
        map_flush_cache(&map_caches[cache_i], 0);
    }
}

static void map_exit_cache(struct map_cache *cache) {
    struct map_cache_entry *ent;

    map_flush_cache(cache, 1);

    while (!(LIST_EMPTY(&cache->mbf_head))) {
        ent = LIST_FIRST(&cache->mbf_head);
        if (ent != NULL) {
            LIST_REMOVE(ent, f_entry);
            cache->nfree--;
            free(ent->buf);
        }
    }

    pthread_spin_destroy(&cache->mb_spin);
    pthread_mutex_destroy(&cache->mutex);
    free(cache->pg_buf);
}

static void map_exit_all_caches(void) {
    uint32_t cache_i = MAP_N_CACHES;

    while (cache_i) {
        cache_i--;
        map_exit_cache(&map_caches[cache_i]);
    }
}

static int map_init(void) {
    struct xztl_core *core;
    get_xztl_core(&core);
    uint32_t cache_i;
    map_caches = calloc(MAP_N_CACHES, sizeof(struct map_cache));
    if (!map_caches) {
        log_err("map_init: map_caches is NULL.\n");
        return XZTL_ZTL_MAP_ERR;
    }

    map_pg_sz      = (ZTL_MPE_PG_SEC * core->media->geo.nbytes);
    map_ent_per_pg = map_pg_sz / sizeof(struct app_map_entry);

    for (cache_i = 0; cache_i < MAP_N_CACHES; cache_i++) {
        if (map_init_cache(&map_caches[cache_i])) {
            log_erra("map_init_cache: cache_i cache_i [%u] buf is null.\n",
                     cache_i);
            goto EXIT_CACHES;
        }

        map_caches[cache_i].id = cache_i;
    }

    cp_running = 0;

    log_info("map_init: Global Mapping started.\n");

    return XZTL_OK;

EXIT_CACHES:
    free(map_caches);

    return XZTL_ZTL_MAP_ERR;
}

static void map_exit(void) {
    map_exit_all_caches();

    free(map_caches);

    log_info("map_exit: Global Mapping stopped.");
}

static struct map_cache_entry *map_get_cache_entry(uint64_t id) {
    uint32_t                cache_id, pg_off;
    uint64_t                first_pg_lba;
    struct map_md_addr     *md_ent;
    struct map_cache_entry *cache_ent = NULL;
    struct map_md_addr     *addr;

    cache_id = id % MAP_N_CACHES;
    pg_off   = id / map_ent_per_pg;

    ZDEBUG(ZDEBUG_MAP, "map_get_cache_entry: get cache. ID: [%lu], off [%d].",
           id, pg_off);

    md_ent = ztl()->mpe->get_fn(pg_off);
    if (!md_ent) {
        log_erra(
            "map_get_cache_entry: Map MD page out of bounds. ID: [%lu], off "
            "[%d]\n",
            id, pg_off);
        return NULL;
    }

    addr = (struct map_md_addr *)&md_ent->addr;

    /* If the ADDR flag is zero, the mapping page is not cached yet */
    /* There is a mutex per metadata page */
    pthread_mutex_lock(&ztl()->smap.entry_mutex[pg_off]);
    if (!addr->g.flag) {
        first_pg_lba = (id / map_ent_per_pg) * map_ent_per_pg;

        if (map_load_pg_cache(&map_caches[cache_id], md_ent, first_pg_lba,
                              pg_off)) {
            log_erra(
                "map_get_cache_entry: Mapping page not loaded cache [%d], "
                "pg_off [%d]\n",
                cache_id, pg_off);
            pthread_mutex_unlock(&ztl()->smap.entry_mutex[pg_off]);

            return NULL;
        }

    } else {
        cache_ent = (struct map_cache_entry *)((uint64_t)addr->g.addr);

        /* Keep cache entry as hot, in the tail of the queue */
        if (!cp_running) {
            pthread_spin_lock(&map_caches[cache_id].mb_spin);
            TAILQ_REMOVE(&map_caches[cache_id].mbu_head, cache_ent, u_entry);
            TAILQ_INSERT_TAIL(&map_caches[cache_id].mbu_head, cache_ent,
                              u_entry);
            pthread_spin_unlock(&map_caches[cache_id].mb_spin);
        }
    }
    pthread_mutex_unlock(&ztl()->smap.entry_mutex[pg_off]);

    /* At this point, the ADDR only points to the cache */
    if (cache_ent == NULL)
        cache_ent = (struct map_cache_entry *)((uint64_t)addr->g.addr);

    return cache_ent;
}

static int map_upsert_md(uint64_t index, uint64_t new_addr, uint64_t old_addr) {
    return XZTL_OK;
}

static int map_upsert(uint64_t id, uint64_t val, uint64_t *old,
                      uint64_t old_caller) {
    uint32_t                ent_off;
    struct app_map_entry   *map_ent;
    struct map_cache_entry *cache_ent;

    ent_off = id % map_ent_per_pg;
    if (ent_off >= map_ent_per_pg) {
        log_erra("map_upsert. Entry offset out of bounds. ID [%lu], off [%d]\n",
                 id, ent_off);
        return XZTL_ZTL_MAP_ERR;
    }

    ZDEBUG(ZDEBUG_MAP, "map_upsert: upsert. ID: [%lu], off [%d].", id, ent_off);

    cache_ent = map_get_cache_entry(id);
    if (!cache_ent) {
        log_err("map_upsert: cache_ent is NULL.\n");
        return XZTL_ZTL_MAP_ERR;
    }
    map_ent = &((struct app_map_entry *)cache_ent->buf)[ent_off];  // NOLINT

    /* Fill old ADDR pointer, caller may use to invalidate the addr for GC */
    *old = map_ent->addr;

    /* User writes have priority and always update the mapping by setting
       'old_caller' as 0. GC, for example, sets 'old_caller' with the old
       sector address. If other thread has updated it, keep the current value.*/
    if (old_caller && map_ent->addr != old_caller) {
        log_erra("map_upsert: map_ent->addr [%p] != old_caller [%p].\n",
                 (void *)map_ent->addr, (void *)old_caller);
        return XZTL_ZTL_MAP_ERR;
    }

    ATOMIC_SWAP(&map_ent->addr, map_ent->addr, val);

    ZDEBUG(ZDEBUG_MAP,
           "map_upsert: upsert succeed, ID: [%lu], val: [0x%lx/%d/%d]\n", id,
           (uint64_t)map_ent->g.offset, map_ent->g.nsec, map_ent->g.multi);

    return XZTL_OK;
}

static uint64_t map_read(uint64_t id) {
    struct map_cache_entry *cache_ent;
    struct app_map_entry   *map_ent;
    uint32_t                ent_off;
    uint64_t                ret;

    ent_off = id % map_ent_per_pg;
    if (ent_off >= map_ent_per_pg) {
        log_erra("ztl-map: read. Entry offset out of bounds. ID [%lu]", id);
        return AND64;
    }

    ZDEBUG(ZDEBUG_MAP, " map_read: ID: [%lu], off [%d].", id, ent_off);

    cache_ent = map_get_cache_entry(id);
    if (!cache_ent) {
        log_erra("map_read: cache_ent is NULL ID [%lu]\n", id);
        return AND64;
    }
    map_ent = &((struct app_map_entry *)cache_ent->buf)[ent_off];  // NOLINT

    ret = map_ent->g.offset;

    ZDEBUG(ZDEBUG_MAP, "  map_read: ID: [%lu], val [0x%lx/%d/%d]", id,
           (uint64_t)map_ent->g.offset, map_ent->g.nsec, map_ent->g.multi);

    return ret;
}

static struct app_map_mod libztl_map = {.mod_id       = ZTLMOD_MAP,
                                        .name         = "LIBZTL-MAP",
                                        .init_fn      = map_init,
                                        .exit_fn      = map_exit,
                                        .persist_fn   = map_flush_all_caches,
                                        .upsert_md_fn = map_upsert_md,
                                        .upsert_fn    = map_upsert,
                                        .read_fn      = map_read};

void ztl_map_register(void) {
    ztl_mod_register(ZTLMOD_MAP, LIBZTL_MAP, &libztl_map);
}
