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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <unistd.h>
#include <xztl-mempool.h>
#include <xztl.h>

static struct xztl_mempool xztlmp;

static void xztl_mempool_free(struct xztl_mp_pool_i *pool) {
    struct xztl_mp_entry *ent;

    while (!STAILQ_EMPTY(&pool->head)) {
        ent = STAILQ_FIRST(&pool->head);
        if (ent) {
            STAILQ_REMOVE_HEAD(&pool->head, entry);
            if (ent->opaque) {
                if (!pool->alloc_fn || !pool->free_fn) {
                    free(ent->opaque);
                } else {
                    pool->free_fn(ent->opaque);
                }
            }
            free(ent);
        }
    }
}

/* This function does not free entries that are out of the pool */
int xztl_mempool_destroy(uint32_t type, uint16_t tid) {
    struct xztl_mp_pool_i *pool;

    if (type >= XZTLMP_TYPES || tid >= XZTLMP_THREADS) {
        log_erra("xztl_mempool_destroy: err type [%u] tid [%u]\n", type, tid);
        return XZTL_MP_OUTBOUNDS;
    }

    pool = &xztlmp.mp[type].pool[tid];

    if (!pool->active)
        return XZTL_OK;

    pool->active = 0;
    xztl_mempool_free(pool);
    pthread_spin_destroy(&pool->spin);
    pool->alloc_fn = NULL;
    pool->free_fn  = NULL;

    return XZTL_OK;
}

int xztl_mempool_create(uint32_t type, uint16_t tid, uint32_t entries,
                        uint32_t ent_sz, xztl_mp_alloc *alloc,
                        xztl_mp_free *free) {
    struct xztl_mp_pool_i *pool;
    struct xztl_mp_entry  *ent;
    void                  *opaque;
    uint32_t               ent_i;

    if (type >= XZTLMP_TYPES || tid >= XZTLMP_THREADS) {
        log_erra("xztl_mempool_create: err type [%u] tid [%u]\n", type, tid);
        return XZTL_MP_OUTBOUNDS;
    }

    if (!entries || entries > XZTLMP_MAX_ENT || !ent_sz ||
        ent_sz > XZTLMP_MAX_ENT_SZ) {
        log_erra("xztl_mempool_create: err entries [%u] ent_sz [%u]\n", entries,
                 ent_sz);
        return XZTL_MP_INVALID;
    }

    pool = &xztlmp.mp[type].pool[tid];

    if (pool->active) {
        log_erra("xztl_mempool_create: err pool->active [%u]\n", pool->active);
        return XZTL_MP_ACTIVE;
    }
    if (pthread_spin_init(&pool->spin, 0)) {
        log_err("xztl_mempool_create: err pthread_spin_init failed.\n");
        return XZTL_MP_MEMERROR;
    }

    STAILQ_INIT(&pool->head);
    pool->alloc_fn                   = alloc;
    pool->free_fn                    = free;

    /* Allocate entries */
    for (ent_i = 0; ent_i < entries; ent_i++) {
        ent = aligned_alloc(64, sizeof(struct xztl_mp_entry));
        if (!ent) {
            log_err("xztl_mempool_create: ent is NULL\n");
            goto MEMERR;
        }
        if (!alloc || !free)
            opaque = aligned_alloc(64, ent_sz);
        else
            opaque = alloc(ent_sz);

        if (!opaque) {
            log_err("xztl_mempool_create: opaque is NULL\n");
            if (free)
                free(ent);
            goto MEMERR;
        }

        ent->tid      = tid;
        ent->entry_id = ent_i;
        ent->opaque   = opaque;

        STAILQ_INSERT_TAIL(&pool->head, ent, entry);
    }

    pool->entries  = entries;
    pool->in_count = pool->out_count = 0;
    pool->active                     = 1;

    ZDEBUG(ZDEBUG_MP,
           "xztl_mempool_create: type [%d], tid [%d], ents [%d], "
           "ent_sz [%d]\n",
           type, tid, entries, ent_sz);

    return XZTL_OK;

MEMERR:
    xztl_mempool_free(pool);
    pthread_spin_destroy(&pool->spin);

    return XZTL_MP_MEMERROR;
}

int xztl_mempool_left(uint32_t type, uint16_t tid) {
    struct xztl_mp_pool_i *pool;

    pool = &xztlmp.mp[type].pool[tid];

    return pool->entries - pool->out_count + pool->in_count;
}

/* Only 1 thread is allowed to remove but a concurrent thread may insert */
struct xztl_mp_entry *xztl_mempool_get(uint32_t type, uint16_t tid) {
    struct xztl_mp_pool_i *pool;
    struct xztl_mp_entry  *ent;
    ZDEBUG(ZDEBUG_MP, "xztl_mempool_get: type [%d], tid [%d]", type, tid);

    pool = &xztlmp.mp[type].pool[tid];
#ifdef MP_LOCKFREE
    uint16_t tmp, old;

    /* This guarantees that INSERT and REMOVE are not concurrent */
    /* TODO: Make a timeout */

    while (pool->entries - pool->out_count <= 2) {
        tmp = pool->in_count;
        pool->out_count -= tmp;
        do {
            old = pool->in_count;
        } while (
            !__sync_bool_compare_and_swap(&pool->in_count, old, old - tmp));
    }
#else

RETRY:
    pthread_spin_lock(&pool->spin);
#endif /* MP_LOCKFREE */

    ent = STAILQ_FIRST(&pool->head);

#ifndef MP_LOCKFREE
    if (!ent) {
        log_err("xztl_mempool_get: ent is NULL\n");
        pthread_spin_unlock(&pool->spin);
        usleep(1);
        goto RETRY;
    }
#endif /* MP_LOCKFREE */

    STAILQ_REMOVE_HEAD(&pool->head, entry);
    pool->out_count++;

#ifndef MP_LOCKFREE
    pthread_spin_unlock(&pool->spin);
#endif /* MP_LOCKFREE */

    return ent;
}

/* Only 1 thread is allowed to insert but a concurrent thread may remove */
void xztl_mempool_put(struct xztl_mp_entry *ent, uint32_t type, uint16_t tid) {
    struct xztl_mp_pool_i *pool;

    ZDEBUG(ZDEBUG_MP, "xztl_mempool_put: type [%d], tid [%d]", type, tid);

    pool = &xztlmp.mp[type].pool[tid];

#ifndef MP_LOCKFREE
    pthread_spin_lock(&pool->spin);
#endif /* MP_LOCKFREE */

    STAILQ_INSERT_TAIL(&pool->head, ent, entry);

#ifndef MP_LOCKFREE
    pthread_spin_unlock(&pool->spin);
#else
    uint16_t old;
    /* This guarantees that INSERT and REMOVE are not concurrent */
    do {
        old = pool->in_count;
    } while (!__sync_bool_compare_and_swap(&pool->in_count, old, old + 1));
#endif /* MP_LOCKFREE */
}

int xztl_mempool_exit(void) {
    uint16_t type_i, tid;

    for (type_i = 0; type_i < XZTLMP_TYPES; type_i++) {
        for (tid = 0; tid < XZTLMP_THREADS; tid++) {
            xztl_mempool_destroy(type_i, tid);
        }
    }
    return XZTL_OK;
}

int xztl_mempool_init(void) {
    memset(&xztlmp, 0x0, sizeof(struct xztl_mempool));
    return XZTL_OK;
}
