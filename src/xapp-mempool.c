#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <string.h>
#include <xapp.h>
#include <xapp-mempool.h>

static struct xapp_mempool xappmp;

static void xapp_mempool_free (struct xapp_mp_pool_i *pool)
{
    struct xapp_mp_entry *ent;

    while (!STAILQ_EMPTY (&pool->head)) {
	ent = STAILQ_FIRST (&pool->head);
	STAILQ_REMOVE_HEAD (&pool->head, entry);
	if (ent) {
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
    struct xapp_misc_cmd cmd;
    int ret;

    if (type > XAPPMP_TYPES || tid > XAPPMP_THREADS)
	 return XAPP_MP_OUTBOUNDS;

    pool = &xappmp.mp[type].pool[tid];

    if (!pool->active)
	return XAPP_OK;

    /* TODO: Should we check oustanding commands and wait? */

    /* Destroy asynchronous context via xnvme */
    cmd.opcode = XAPP_MISC_ASYNCH_TERM;
    cmd.asynch.depth   = pool->entries;
    cmd.asynch.ctx_ptr = (uint64_t) pool->asynch;

    ret = xapp_media_submit_misc (&cmd);
    if (ret)
	return XAPP_MP_ASYNCH_ERR;

    xapp_mempool_free (pool);
    pool->active = 0;

    return XAPP_OK;
}

int xapp_mempool_create (uint16_t type, uint16_t tid, uint16_t entries,
						     uint32_t ent_sz)
{
    struct xapp_mp_pool_i *pool;
    struct xapp_mp_entry *ent;
    struct xapp_misc_cmd cmd;
    void *opaque;
    int ent_i, ret;

    if (type > XAPPMP_TYPES || tid > XAPPMP_THREADS)
	return XAPP_MP_OUTBOUNDS;

    if (!entries || entries > XAPPMP_MAX_ENT ||
	!ent_sz || ent_sz > XAPPMP_MAX_ENT_SZ)
	return XAPP_MP_INVALID;

    pool = &xappmp.mp[type].pool[tid];

    if (pool->active)
	return XAPP_MP_ACTIVE;

    STAILQ_INIT (&pool->head);

    /* Allocate entries */
    for (ent_i = 0; ent_i < entries; ent_i++) {
	ent = malloc (sizeof (struct xapp_mp_entry));
	if (!ent)
	    goto MEMERR;

	opaque = malloc (sizeof (ent_sz));
	if (!opaque) {
	    free (ent);
	    goto MEMERR;
	}

	ent->opaque = opaque;

	STAILQ_INSERT_TAIL (&pool->head, ent, entry);
    }

    /* Create asynchronous context via xnvme */
    cmd.opcode = XAPP_MISC_ASYNCH_INIT;
    cmd.asynch.depth   = entries;
    cmd.asynch.ctx_ptr = (uint64_t) &pool->asynch;

    ret = xapp_media_submit_misc (&cmd);
    if (ret || !pool->asynch) {
	xapp_mempool_free (pool);
	return XAPP_MP_ASYNCH_ERR;
    }

    pool->active = 1;

    return XAPP_OK;

MEMERR:
    xapp_mempool_free (pool);

    return XAPP_MP_MEMERROR;
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
