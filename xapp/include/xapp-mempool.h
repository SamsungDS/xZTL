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

#ifndef XAPPMEMPOOL
#define XAPPMEMPOOL

#include <pthread.h>
#include <sys/queue.h>
#include <libxnvme.h>

#define XAPPMP_THREADS 		64
#define XAPPMP_TYPES   		4
#define XAPPMP_MAX_ENT 		(65536 + 2)
#define XAPPMP_MAX_ENT_SZ	(1024 * 1024)  /* 1 MB */

typedef void *(xapp_mp_alloc)(size_t size);
typedef void  (xapp_mp_free)(void *ptr);

enum xapp_mp_types {
    XAPP_MEMPOOL_MCMD   = 0x0,
    XAPP_ZTL_PRO_CTX    = 0x1,
    ZROCKS_MEMORY       = 0x2,
    XAPP_PROMETHEUS_LAT = 0x3
};

enum xapp_mp_status {
    XAPP_MP_OUTBOUNDS  = 0x1,
    XAPP_MP_INVALID    = 0x2,
    XAPP_MP_ACTIVE     = 0x3,
    XAPP_MP_MEMERROR   = 0x4,
    XAPP_MP_ASYNCH_ERR = 0x5
};

struct xapp_mp_entry {
    void 	*opaque;
    uint16_t 	 tid;
    uint32_t 	 entry_id;
    STAILQ_ENTRY(xapp_mp_entry) entry;
};

struct xapp_mp_pool_i {
    uint8_t 		active;
    volatile uint16_t 	in_count;
    uint16_t 		out_count;
    uint32_t 		entries;
    pthread_spinlock_t	spin;
    xapp_mp_alloc      *alloc_fn;
    xapp_mp_free       *free_fn;
    STAILQ_HEAD (mp_head, xapp_mp_entry) head;
};

struct xapp_mp_pool {
    struct xapp_mp_pool_i pool[XAPPMP_THREADS];
};

struct xapp_mempool {
    struct xapp_mp_pool mp[XAPPMP_TYPES];
};

/* Mempool related functions */
int xapp_mempool_init (void);
int xapp_mempool_exit (void);
int xapp_mempool_create (uint32_t type, uint16_t tid, uint32_t entries,
		    uint32_t ent_sz, xapp_mp_alloc *alloc, xapp_mp_free *free);
int xapp_mempool_destroy (uint32_t type, uint16_t tid);
struct xapp_mp_entry *xapp_mempool_get (uint32_t type, uint16_t tid);
void xapp_mempool_put (struct xapp_mp_entry *ent, uint32_t type, uint16_t tid);
int  xapp_mempool_left (uint32_t type, uint16_t tid);

#endif /* XAPPMEMPOOL */
