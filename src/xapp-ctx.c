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

    ret = xapp_mempool_create (XAPP_MEMPOOL_MCMD, tid, depth,
					sizeof (struct xapp_io_mcmd));
    if (ret)
	return NULL;

    tctx = malloc (sizeof (struct xapp_mthread_ctx));
    if (!tctx) {
	xapp_mempool_destroy (XAPP_MEMPOOL_MCMD, tid);
	return NULL;
    }

    tctx->comp_active = 1;

    /* Create asynchronous context via xnvme */
    cmd.opcode = XAPP_MISC_ASYNCH_INIT;
    cmd.asynch.depth   	    = depth;
    cmd.asynch.active_ptr   = (void *) &tctx->comp_active;
    cmd.asynch.ctx_ptr      = (void *) &tctx->asynch;
    cmd.asynch.comp_tid_ptr = (void *) &tctx->comp_tid;

    ret = xapp_media_submit_misc (&cmd);
    if (ret || !tctx->asynch) {
	free (tctx);
	xapp_mempool_destroy (XAPP_MEMPOOL_MCMD, tid);
	return NULL;
    }

    tctx->tid = tid;

    return tctx;
}

int xapp_ctx_media_exit (struct xapp_mthread_ctx *tctx)
{
    struct xapp_misc_cmd cmd;
    int ret;

    /* TODO: Should we check oustanding commands and wait? YES */

    /* Destroy asynchronous context via xnvme */
    /* Stop the completion thread */
    tctx->comp_active = 0;
    cmd.opcode = XAPP_MISC_ASYNCH_TERM;
    cmd.asynch.ctx_ptr      = (void *) tctx->asynch;
    cmd.asynch.comp_tid_ptr = (void *) &tctx->comp_tid;

    ret = xapp_media_submit_misc (&cmd);
    if (ret)
	return XAPP_MP_ASYNCH_ERR;

    xapp_mempool_destroy (XAPP_MEMPOOL_MCMD, tctx->tid);
    free (tctx);

    return XAPP_OK;
}
