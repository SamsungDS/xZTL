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

#define XZTL_CTX_NVME_DEPTH 512
#define XZTL_THREAD_CTX_NUM 64

static struct xztl_mthread_ctx *xzt_ctxs[XZTL_THREAD_CTX_NUM];
static pthread_spinlock_t       ctxs_spin;

struct xztl_mthread_ctx *xztl_ctx_media_init(uint32_t depth) {
    struct xztl_misc_cmd     cmd;
    struct xztl_mthread_ctx *tctx;
    int                      ret;

    tctx = malloc(sizeof(struct xztl_mthread_ctx));
    if (!tctx) {
        return NULL;
    }

    if (pthread_spin_init(&tctx->qpair_spin, 0)) {
        free(tctx);
        return NULL;
    }

    tctx->is_busy     = 0;
    tctx->comp_active = 1;

    /* Create asynchronous context via xnvme */
    cmd.opcode         = XZTL_MISC_ASYNCH_INIT;
    cmd.asynch.depth   = XZTL_CTX_NVME_DEPTH;
    cmd.asynch.ctx_ptr = tctx;

    ret = xztl_media_submit_misc(&cmd);
    if (ret || !tctx->queue) {
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
    tctx->comp_active = 0;

    cmd.opcode         = XZTL_MISC_ASYNCH_TERM;
    cmd.asynch.ctx_ptr = tctx;

    ret = xztl_media_submit_misc(&cmd);
    if (ret)
        return XZTL_MP_ASYNCH_ERR;

    pthread_spin_destroy(&tctx->qpair_spin);
    free(tctx);

    return XZTL_OK;
}

int xztl_init_thread_ctxs() {
    if (pthread_spin_init(&ctxs_spin, 0)) {
        return XZTL_MEM;
    }

    for (int i = 0; i < XZTL_THREAD_CTX_NUM; i++) {
        struct xztl_mthread_ctx *ctxTemp = NULL;
        ctxTemp = xztl_ctx_media_init(XZTL_CTX_NVME_DEPTH);
        if (ctxTemp == NULL) {
            printf("xztl_init_thread_ctxs init ctx %d failed\n", i);
            return XZTL_MEM;
        }

        xzt_ctxs[i] = ctxTemp;
    }
    return XZTL_OK;
}

int xztl_exit_thread_ctxs() {
    pthread_spin_lock(&ctxs_spin);
    for (int i = 0; i < XZTL_THREAD_CTX_NUM; i++) {
        int ret = xztl_ctx_media_exit(xzt_ctxs[i]);
        if (ret) {
            printf("xztl_exit_thread_ctxs free ctx %d failed\n", i);
        }
    }
    pthread_spin_unlock(&ctxs_spin);
    pthread_spin_destroy(&ctxs_spin);
    return XZTL_OK;
}

struct xztl_mthread_ctx *get_thread_ctx() {
    struct xztl_mthread_ctx *ctx = NULL;
    pthread_spin_lock(&ctxs_spin);
    for (int i = 0; i < XZTL_THREAD_CTX_NUM; i++) {
        if (xzt_ctxs[i] != NULL && !xzt_ctxs[i]->is_busy) {
            xzt_ctxs[i]->is_busy = 1;
            ctx = xzt_ctxs[i];
            break;
        }
    }
    pthread_spin_unlock(&ctxs_spin);
    return ctx;
}

void put_thread_ctx(struct xztl_mthread_ctx *ctx) {
    if (ctx == NULL) {
        return;
    }

    pthread_spin_lock(&ctxs_spin);
    ctx->is_busy = 0;
    pthread_spin_unlock(&ctxs_spin);
}
