#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <string.h>
#include <xapp.h>
#include <xapp-mempool.h>
#include <pthread.h>
#include <stdbool.h>

/* Comment this macro for standard spinlock implementation */
#define MP_LOCKFREE

static struct xapp_mempool xappmp;

static void xapp_mempool_free (struct xapp_mp_pool_i *pool)
{
    struct xapp_mp_entry *ent;

    while (!STAILQ_EMPTY (&pool->head)) {
	ent = STAILQ_FIRST (&pool->head);
	if (ent) {
	    STAILQ_REMOVE_HEAD (&pool->head, entry);
	    if (ent->opaque)
		free (ent->opaque);
	    free (ent);
	}
    }
}

/* This function does not free entries that are out of the pool */
int xapp_mempool_destroy (uint16_t type, uint16_t tid)
{
    struct xapp_mp_pool_i *pool;

    if (type > XAPPMP_TYPES || tid > XAPPMP_THREADS)
	 return XAPP_MP_OUTBOUNDS;

    pool = &xappmp.mp[type].pool[tid];

    if (!pool->active)
	return XAPP_OK;

    pool->active = 0;
    xapp_mempool_free (pool);
    pthread_spin_destroy (&pool->spin);

    return XAPP_OK;
}

int xapp_mempool_create (uint16_t type, uint16_t tid, uint16_t entries,
							uint32_t ent_sz)
{
    struct xapp_mp_pool_i *pool;
    struct xapp_mp_entry *ent;
    void *opaque;
    uint32_t ent_i;

    if (type > XAPPMP_TYPES || tid > XAPPMP_THREADS)
	return XAPP_MP_OUTBOUNDS;

    if (!entries || entries > XAPPMP_MAX_ENT ||
	!ent_sz || ent_sz > XAPPMP_MAX_ENT_SZ)
	return XAPP_MP_INVALID;

    pool = &xappmp.mp[type].pool[tid];

    if (pool->active)
	return XAPP_MP_ACTIVE;

    if (pthread_spin_init (&pool->spin, 0))
	return XAPP_MP_MEMERROR;

    STAILQ_INIT (&pool->head);

    /* Allocate entries */
    for (ent_i = 0; ent_i < entries; ent_i++) {
	ent = aligned_alloc (64, sizeof (struct xapp_mp_entry));
	if (!ent)
	    goto MEMERR;

	opaque = aligned_alloc (64, ent_sz);
	if (!opaque) {
	    free (ent);
	    goto MEMERR;
	}

	ent->tid      = tid;
	ent->entry_id = ent_i;
	ent->opaque   = opaque;

	STAILQ_INSERT_TAIL (&pool->head, ent, entry);
    }

    pool->entries = entries;
    pool->in_count = pool->out_count = 0;
    pool->active = 1;

    return XAPP_OK;

MEMERR:
    xapp_mempool_free (pool);
    pthread_spin_destroy (&pool->spin);

    return XAPP_MP_MEMERROR;
}

/* Only 1 thread is allowed to remove but a concurrent thread may insert */
struct xapp_mp_entry *xapp_mempool_get (uint16_t type, uint16_t tid)
{
    struct xapp_mp_pool_i *pool;
    struct xapp_mp_entry *ent;
    uint16_t tmp, old;

    pool = &xappmp.mp[type].pool[tid];
#ifdef MP_LOCKFREE
    /* This guarantees that INSERT and REMOVE are not concurrent */
    /* TODO: Make a timeout */
    while (pool->entries - pool->out_count <= 2) {
	usleep (1000);
	tmp = pool->in_count;
	pool->out_count -= tmp;
	do {
	    old = pool->in_count;
	} while (!__sync_bool_compare_and_swap(&pool->in_count, old, old - tmp));
    }
#else

RETRY:
    pthread_spin_lock (&pool->spin);
#endif /* MP_LOCKFREE */

    ent = STAILQ_FIRST (&pool->head);

#ifndef MP_LOCKFREE
    if (!ent) {
	pthread_spin_unlock (&pool->spin);
	usleep (1);
	goto RETRY;
    }
#endif /* MP_LOCKFREE */

    STAILQ_REMOVE_HEAD (&pool->head, entry);
    pool->out_count++;

#ifndef MP_LOCKFREE
    pthread_spin_unlock (&pool->spin);
#endif /* MP_LOCKFREE */

    return ent;
}

/* Only 1 thread is allowed to insert but a concurrent thread may remove */
void xapp_mempool_put (struct xapp_mp_entry *ent, uint16_t type, uint16_t tid)
{
    struct xapp_mp_pool_i *pool;
    uint16_t old;

    pool = &xappmp.mp[type].pool[tid];

#ifndef MP_LOCKFREE
    pthread_spin_lock (&pool->spin);
#endif /* MP_LOCKFREE */

    STAILQ_INSERT_TAIL (&pool->head, ent, entry);

#ifndef MP_LOCKFREE
    pthread_spin_unlock (&pool->spin);
#else
    /* This guarantees that INSERT and REMOVE are not concurrent */
    do {

	old = pool->in_count;
    } while (!__sync_bool_compare_and_swap(&pool->in_count, old, old + 1));
#endif /* MP_LOCKFREE */
}

int xapp_mempool_exit (void)
{
    uint16_t type_i, tid;

    for (type_i = 0; type_i < XAPPMP_TYPES; type_i++) {
	for (tid = 0; tid < XAPPMP_THREADS; tid++) {
	    xapp_mempool_destroy (type_i, tid);
	}
    }

    return XAPP_OK;
}

int xapp_mempool_init (void)
{
    memset (&xappmp, 0x0, sizeof (struct xapp_mempool));
    return XAPP_OK;
}
