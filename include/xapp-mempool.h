#ifndef XAPPMEMPOOL
#define XAPPMEMPOOL

#include <pthread.h>
#include <sys/queue.h>
#include <libxnvme.h>

#define XAPPMP_THREADS 		32
#define XAPPMP_TYPES   		2
#define XAPPMP_MAX_ENT 		1024
#define XAPPMP_MAX_ENT_SZ	(1024 * 1024)  /* 1 MB */

enum xapp_mp_types {
    XAPP_MEMPOOL_MCMD =	0x0,
    XAPP_ZTL_PRO_CTX  = 0x1
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
    uint16_t 		entries;
    pthread_spinlock_t	spin;
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

/* Insert and remove elements */
struct xapp_mp_entry *xapp_mempool_get (uint16_t type, uint16_t tid);
void xapp_mempool_put (struct xapp_mp_entry *ent, uint16_t type, uint16_t tid);

#endif /* XAPPMEMPOOL */
