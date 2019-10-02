#ifndef XAPPMEMPOOL
#define XAPPMEMPOOL

#include <sys/queue.h>
#include <libxnvme.h>

#define XAPPMP_THREADS 		64
#define XAPPMP_TYPES   		2
#define XAPPMP_MAX_ENT 		128
#define XAPPMP_MAX_ENT_SZ	(1024 * 1024)  /* 1 MB */

enum xapp_mp_types {
    XAPP_MEMPOOL_MCMD =	0x1
};

enum xapp_mp_status {
    XAPP_MP_OUTBOUNDS  = 0x1,
    XAPP_MP_INVALID    = 0x2,
    XAPP_MP_ACTIVE     = 0x3,
    XAPP_MP_MEMERROR   = 0x4,
    XAPP_MP_ASYNCH_ERR = 0x5
};

struct xapp_mp_entry {
    void *opaque;
    STAILQ_ENTRY(xapp_mp_entry) entry;
};

struct xapp_mp_pool_i {
    uint8_t active;
    uint16_t entries;
    struct xnvme_asynch_ctx *asynch;
    STAILQ_HEAD (mp_head, xapp_mp_entry) head;
};

struct xapp_mp_pool {
    struct xapp_mp_pool_i pool[XAPPMP_THREADS];
};

struct xapp_mempool {
    struct xapp_mp_pool mp[XAPPMP_TYPES];
};

/* Initialize and close the mempool module */
int xapp_mempool_init (void);
int xapp_mempool_exit (void);

/* Create and destroy memory pools */
int xapp_mempool_create (uint16_t type, uint16_t tid, uint16_t entries,
						      uint32_t ent_sz);
int xapp_mempool_destroy (uint16_t type, uint16_t tid);

#endif /* XAPPMEMPOOL */
