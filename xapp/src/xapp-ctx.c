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

#include <stdlib.h>
#include <xapp.h>
#include <xapp-media.h>
#include <xapp-mempool.h>

#define XAPP_CTX_NVME_DEPTH  256

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
    cmd.asynch.depth   	    = XAPP_CTX_NVME_DEPTH;
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
