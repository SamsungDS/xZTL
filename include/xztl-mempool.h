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

#ifndef XZTLMEMPOOL
#define XZTLMEMPOOL

#include <pthread.h>
#include <sys/queue.h>
#include <libxnvme.h>

#define XZTLMP_THREADS 		64
#define XZTLMP_TYPES   		4
#define XZTLMP_MAX_ENT 		(65536 + 2)
#define XZTLMP_MAX_ENT_SZ	(1024 * 1024)  /* 1 MB */

typedef void *(xztl_mp_alloc)(size_t size);
typedef void  (xztl_mp_free)(void *ptr);

enum xztl_mp_types {
    XZTL_MEMPOOL_MCMD   = 0x0,
    XZTL_ZTL_PRO_CTX    = 0x1,
    ZROCKS_MEMORY       = 0x2,
    XZTL_PROMETHEUS_LAT = 0x3
};

enum xztl_mp_status {
    XZTL_MP_OUTBOUNDS  = 0x1,
    XZTL_MP_INVALID    = 0x2,
    XZTL_MP_ACTIVE     = 0x3,
    XZTL_MP_MEMERROR   = 0x4,
    XZTL_MP_ASYNCH_ERR = 0x5
};

struct xztl_mp_entry {
    void 	*opaque;
    uint16_t 	 tid;
    uint32_t 	 entry_id;
    STAILQ_ENTRY(xztl_mp_entry) entry;
};

struct xztl_mp_pool_i {
    uint8_t 		active;
    volatile uint16_t 	in_count;
    uint16_t 		out_count;
    uint32_t 		entries;
    pthread_spinlock_t	spin;
    xztl_mp_alloc      *alloc_fn;
    xztl_mp_free       *free_fn;
    STAILQ_HEAD (mp_head, xztl_mp_entry) head;
};

struct xztl_mp_pool {
    struct xztl_mp_pool_i pool[XZTLMP_THREADS];
};

struct xztl_mempool {
    struct xztl_mp_pool mp[XZTLMP_TYPES];
};

/* Mempool related functions */
int xztl_mempool_init (void);
int xztl_mempool_exit (void);
int xztl_mempool_create (uint32_t type, uint16_t tid, uint32_t entries,
		    uint32_t ent_sz, xztl_mp_alloc *alloc, xztl_mp_free *free);
int xztl_mempool_destroy (uint32_t type, uint16_t tid);
struct xztl_mp_entry *xztl_mempool_get (uint32_t type, uint16_t tid);
void xztl_mempool_put (struct xztl_mp_entry *ent, uint32_t type, uint16_t tid);
int  xztl_mempool_left (uint32_t type, uint16_t tid);

#endif /* XZTLMEMPOOL */
