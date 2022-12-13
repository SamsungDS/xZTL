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
#include <xztl-media.h>
#include <xztl-mempool.h>
#include <xztl.h>

static pthread_spinlock_t ctxs_spin;

struct xztl_mthread_ctx *xztl_ctx_media_init(uint32_t depth) {
    struct xztl_misc_cmd     cmd;
    struct xztl_mthread_ctx *tctx;
    int                      ret;

    tctx = malloc(sizeof(struct xztl_mthread_ctx));
    if (!tctx) {
        log_err("xztl_ctx_media_init: tctx is NULL.\n");
        return NULL;
    }

    if (pthread_spin_init(&tctx->qpair_spin, 0)) {
        log_err("xztl_ctx_media_init: pthread_spin_init failed\n");
        free(tctx);
        return NULL;
    }

    /* Create asynchronous context via xnvme */
    cmd.opcode         = XZTL_MISC_ASYNCH_INIT;
    cmd.asynch.depth   = depth;
    cmd.asynch.ctx_ptr = tctx;

    ret = xztl_media_submit_misc(&cmd);
    if (ret || !tctx->queue) {
        log_erra(
            "xztl_ctx_media_init: xztl_media_submit_misc ret [%d] tctx->queue "
            "[%p]",
            ret, (void *)tctx->queue);
        pthread_spin_destroy(&tctx->qpair_spin);
        free(tctx);
        return NULL;
    }

    return tctx;
}

int xztl_ctx_media_exit(struct xztl_mthread_ctx *tctx) {
    if (tctx == NULL) {
        return XZTL_OK;
    }
    struct xztl_misc_cmd cmd;
    int                  ret;

    /* TODO: Check for oustanding commands and wait completion */

    /* Destroy asynchronous context via xnvme and
       stop the completion thread */

    cmd.opcode         = XZTL_MISC_ASYNCH_TERM;
    cmd.asynch.ctx_ptr = tctx;

    ret = xztl_media_submit_misc(&cmd);
    if (ret) {
        log_erra("xztl_ctx_media_exit: xztl_media_submit_misc ret [%d]\n", ret);
        return XZTL_MP_ASYNCH_ERR;
    }
    pthread_spin_destroy(&tctx->qpair_spin);
    free(tctx);

    return XZTL_OK;
}
