#include <stdlib.h>
#include <xapp.h>
#include <xapp-media.h>
#include <xapp-mempool.h>

struct xapp_mthread_ctx *xapp_ctx_media_init (uint16_t tid,
						     uint32_t depth)
{
    struct xapp_misc_cmd cmd;
    struct xapp_mthread_ctx *tctx;
    int ret;

    ret = xapp_mempool_create (XAPP_MEMPOOL_MCMD, tid, depth + 2,
				sizeof (struct xapp_io_mcmd), NULL, NULL);
    if (ret)
	return NULL;

    tctx = malloc (sizeof (struct xapp_mthread_ctx));
    if (!tctx) {
	xapp_mempool_destroy (XAPP_MEMPOOL_MCMD, tid);
	return NULL;
    }

    if (pthread_spin_init (&tctx->qpair_spin, 0)) {
	free (tctx);
	xapp_mempool_destroy (XAPP_MEMPOOL_MCMD, tid);
	return NULL;
    }

    tctx->tid         = tid;
    tctx->comp_active = 1;

    /* Create asynchronous context via xnvme */
    cmd.opcode = XAPP_MISC_ASYNCH_INIT;
    cmd.asynch.depth   	    = depth;
    cmd.asynch.ctx_ptr      = tctx;

    ret = xapp_media_submit_misc (&cmd);
    if (ret || !tctx->asynch) {
	pthread_spin_destroy (&tctx->qpair_spin);
	free (tctx);
	xapp_mempool_destroy (XAPP_MEMPOOL_MCMD, tid);
	return NULL;
    }

    return tctx;
}

int xapp_ctx_media_exit (struct xapp_mthread_ctx *tctx)
{
    struct xapp_misc_cmd cmd;
    int ret;

    /* TODO: Should we check oustanding commands and wait? YES */

    /* Destroy asynchronous context via xnvme */
    /* Stop the completion thread */
    tctx->comp_active  = 0;
    cmd.opcode         = XAPP_MISC_ASYNCH_TERM;
    cmd.asynch.ctx_ptr = tctx;

    ret = xapp_media_submit_misc (&cmd);
    if (ret)
	return XAPP_MP_ASYNCH_ERR;

    pthread_spin_destroy (&tctx->qpair_spin);
    xapp_mempool_destroy (XAPP_MEMPOOL_MCMD, tctx->tid);
    free (tctx);

    return XAPP_OK;
}
