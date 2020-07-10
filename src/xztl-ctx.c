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

#include <stdlib.h>
#include <xztl.h>
#include <xztl-media.h>
#include <xztl-mempool.h>

#define XZTL_CTX_NVME_DEPTH  64

struct xztl_mthread_ctx *xztl_ctx_media_init (uint16_t tid,
						     uint32_t depth)
{
    struct xztl_misc_cmd cmd;
    struct xztl_mthread_ctx *tctx;
    int ret;

    ret = xztl_mempool_create (XZTL_MEMPOOL_MCMD, tid, depth + 2,
				sizeof (struct xztl_io_mcmd), NULL, NULL);
    if (ret)
	return NULL;

    tctx = malloc (sizeof (struct xztl_mthread_ctx));
    if (!tctx) {
	xztl_mempool_destroy (XZTL_MEMPOOL_MCMD, tid);
	return NULL;
    }

    if (pthread_spin_init (&tctx->qpair_spin, 0)) {
	free (tctx);
	xztl_mempool_destroy (XZTL_MEMPOOL_MCMD, tid);
	return NULL;
    }

    tctx->tid         = tid;
    tctx->comp_active = 1;

    /* Create asynchronous context via xnvme */
    cmd.opcode = XZTL_MISC_ASYNCH_INIT;
    cmd.asynch.depth   	    = XZTL_CTX_NVME_DEPTH;
    cmd.asynch.ctx_ptr      = tctx;

    ret = xztl_media_submit_misc (&cmd);
    if (ret || !tctx->asynch) {
	pthread_spin_destroy (&tctx->qpair_spin);
	free (tctx);
	xztl_mempool_destroy (XZTL_MEMPOOL_MCMD, tid);
	return NULL;
    }

    return tctx;
}

int xztl_ctx_media_exit (struct xztl_mthread_ctx *tctx)
{
    struct xztl_misc_cmd cmd;
    int ret;

    /* TODO: Check for oustanding commands and wait completion */

    /* Destroy asynchronous context via xnvme and
       stop the completion thread */
    tctx->comp_active  = 0;
    cmd.opcode         = XZTL_MISC_ASYNCH_TERM;
    cmd.asynch.ctx_ptr = tctx;

    ret = xztl_media_submit_misc (&cmd);
    if (ret)
	return XZTL_MP_ASYNCH_ERR;

    pthread_spin_destroy (&tctx->qpair_spin);
    xztl_mempool_destroy (XZTL_MEMPOOL_MCMD, tctx->tid);
    free (tctx);

    return XZTL_OK;
}
