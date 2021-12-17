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
#include <libxnvme.h>
#include <libxnvme_3p.h>
#include <libxnvme_adm.h>
#include <libxnvme_nvm.h>
#include <libxnvme_znd.h>
#include <libxnvmec.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>
#include <xztl-media.h>
#include <xztl.h>
#include <ztl-media.h>

#define XZTL_MAX_CALLBACK_THREAD 10

struct znd_media zndmedia;
static uint16_t  callback_thread_num;

inline struct znd_media *get_znd_media(void) {
    return &zndmedia;
}

extern char *dev_name;

static void znd_media_async_cb(struct xnvme_cmd_ctx *ctx, void *cb_arg) {
    struct xztl_io_mcmd *cmd;
    uint16_t             sec_i = 0;

    cmd         = (struct xztl_io_mcmd *)cb_arg;
    cmd->status = xnvme_cmd_ctx_cpl_status(ctx);

    if (!cmd->status && cmd->opcode == XZTL_ZONE_APPEND)
        cmd->paddr[sec_i] = *(uint64_t *)&ctx->cpl.cdw0;  // NOLINT

    if (cmd->opcode == XZTL_CMD_WRITE)
        cmd->paddr[sec_i] = cmd->addr[sec_i].g.sect;

    if (cmd->status) {
        xztl_print_mcmd(cmd);
        xnvme_cmd_ctx_pr(ctx, XNVME_PR_DEF);
    }

    cmd->callback(cmd);

    struct xztl_mthread_ctx *tctx = cmd->async_ctx;
    xnvme_queue_put_cmd_ctx(tctx->queue, cmd->media_ctx);
}

static int znd_media_submit_read_synch(struct xztl_io_mcmd *cmd) {
    uint64_t             slba;
    uint16_t             sec_i = 0;
    struct timespec      ts_s, ts_e;
    struct xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(zndmedia.dev);

    /* The read path is not group based. It uses only sectors */
    slba = cmd->addr[sec_i].g.sect;

    int ret;

    GET_MICROSECONDS(cmd->us_start, ts_s);
    ret = xnvme_nvm_read(&ctx, xnvme_dev_get_nsid(zndmedia.dev), slba,
                         (uint16_t)cmd->nsec[sec_i] - 1,
                         (void *)cmd->prp[sec_i], NULL); // NOLINT
    GET_MICROSECONDS(cmd->us_end, ts_e);

    /* WARNING: Uncommenting this line causes performance drop */
    // xztl_prometheus_add_read_latency (cmd->us_end - cmd->us_start);

    if (ret)
        xztl_print_mcmd(cmd);

    return ret;
}

static int znd_media_submit_read_asynch(struct xztl_io_mcmd *cmd) {
    uint16_t                 sec_i = 0;
    uint64_t                 slba;
    void *                   dbuf;
    struct xztl_mthread_ctx *tctx;
    struct xnvme_cmd_ctx *   xnvme_ctx;
    int                      ret;

    tctx      = cmd->async_ctx;
    xnvme_ctx = xnvme_queue_get_cmd_ctx(tctx->queue);

    dbuf = (void *)cmd->prp[sec_i];  // NOLINT

    /* The write path is not group based. It uses only sectors */
    slba = cmd->addr[sec_i].g.sect;

    xnvme_ctx->async.cb     = znd_media_async_cb;
    xnvme_ctx->async.cb_arg = (void *)cmd; // NOLINT
    xnvme_ctx->dev          = zndmedia.dev;

    cmd->media_ctx = xnvme_ctx;

    ret = xnvme_nvm_read(xnvme_ctx, xnvme_dev_get_nsid(zndmedia.dev), slba,
                         (uint16_t)cmd->nsec[sec_i] - 1, dbuf, NULL);
    if (ret)
        xztl_print_mcmd(cmd);

    return ret;
}

static int znd_media_submit_write_synch(struct xztl_io_mcmd *cmd) {
    return ZND_INVALID_OPCODE;
}

static int znd_media_submit_write_asynch(struct xztl_io_mcmd *cmd) {
    uint16_t                 sec_i = 0;
    uint64_t                 slba;
    void *                   dbuf;
    struct xztl_mthread_ctx *tctx;
    struct xnvme_cmd_ctx *   xnvme_ctx;
    int                      ret;

    tctx      = cmd->async_ctx;
    xnvme_ctx = xnvme_queue_get_cmd_ctx(tctx->queue);

    dbuf = (void *)cmd->prp[sec_i];  // NOLINT

    /* The write path is not group based. It uses only sectors */
    slba = cmd->addr[sec_i].g.sect;

    xnvme_ctx->async.cb     = znd_media_async_cb;
    xnvme_ctx->async.cb_arg = (void *)cmd; // NOLINT
    xnvme_ctx->dev          = zndmedia.dev;
    cmd->media_ctx          = xnvme_ctx;

    ret = xnvme_nvm_write(xnvme_ctx, xnvme_dev_get_nsid(zndmedia.dev), slba,
                          (uint16_t)cmd->nsec[sec_i] - 1, dbuf, NULL);

    if (ret) {
        xnvme_queue_put_cmd_ctx(tctx->queue, xnvme_ctx);
        xztl_print_mcmd(cmd);
    }

    return ret;
}

static int znd_media_submit_append_synch(struct xztl_io_mcmd *cmd) {
    return ZND_INVALID_OPCODE;
}

static int znd_media_submit_append_asynch(struct xztl_io_mcmd *cmd) {
    uint16_t                 zone_i = 0;
    uint64_t                 zlba;
    const void *             dbuf;
    struct xztl_mthread_ctx *tctx;
    struct xnvme_cmd_ctx *   xnvme_ctx;
    int                      ret;

    tctx      = cmd->async_ctx;
    xnvme_ctx = xnvme_queue_get_cmd_ctx(tctx->queue);

    dbuf = (const void *)cmd->prp[zone_i];

    /* The write path separates zones into groups */
    zlba = (zndmedia.media.geo.zn_grp * cmd->addr[zone_i].g.grp +
            cmd->addr[zone_i].g.zone) *
           zndmedia.devgeo->nsect;

    xnvme_ctx->async.cb     = znd_media_async_cb;
    xnvme_ctx->async.cb_arg = (void *)cmd;  // NOLINT
    cmd->media_ctx          = xnvme_ctx;

    ret = (!XZTL_WRITE_APPEND)
              ? xnvme_znd_append(xnvme_ctx, xnvme_dev_get_nsid(zndmedia.dev),
                                 zlba, (uint16_t)cmd->nsec[zone_i] - 1, dbuf,
                                 NULL)
              : -1;
    if (ret)
        xztl_print_mcmd(cmd);

    return ret;
}

static int znd_media_submit_io(struct xztl_io_mcmd *cmd) {
    switch (cmd->opcode) {
        case XZTL_ZONE_APPEND:
            return (cmd->synch) ? znd_media_submit_append_synch(cmd)
                                : znd_media_submit_append_asynch(cmd);
        case XZTL_CMD_READ:
            return (cmd->synch) ? znd_media_submit_read_synch(cmd)
                                : znd_media_submit_read_asynch(cmd);
        case XZTL_CMD_WRITE:
            return (cmd->synch) ? znd_media_submit_write_synch(cmd)
                                : znd_media_submit_write_asynch(cmd);
        default:
            return ZND_INVALID_OPCODE;
    }
    return 0;
}

static inline int znd_media_zone_manage(struct xztl_zn_mcmd *cmd, uint8_t op) {
    uint32_t             lba;
    struct xnvme_cmd_ctx xnvme_ctx = xnvme_cmd_ctx_from_dev(zndmedia.dev);
    int                  ret;
    bool                 select_all = false;

    lba = ((zndmedia.devgeo->nzone * cmd->addr.g.grp) + cmd->addr.g.zone) *
          zndmedia.devgeo->nsect;

    /* If this bit is set to '1', then the SLBA field shall be ignored.  */
    if (cmd->nzones > 1) {
        select_all = true;
    }
    xnvme_ctx.async.queue  = NULL;
    xnvme_ctx.async.cb_arg = NULL;
    ret = xnvme_znd_mgmt_send(&xnvme_ctx, xnvme_dev_get_nsid(zndmedia.dev), lba,
                              select_all, op, 0x0, NULL);
    cmd->status = (ret) ? xnvme_cmd_ctx_cpl_status(&xnvme_ctx) : XZTL_OK;
    // return (ret) ? op : XZTL_OK;
    return ret;
}

static int znd_media_zone_report(struct xztl_zn_mcmd *cmd) {
    struct xnvme_znd_report *rep;
    size_t                   limit;
    uint32_t                 lba;

    // Reading everything until not necessary anymore
    lba = 0;  // ((zndmedia.devgeo->nzone * cmd->addr.g.grp) + cmd->addr.g.zone)
              // * zndmedia.devgeo->nsect;
    limit = 0;  // cmd->nzones;
    rep   = xnvme_znd_report_from_dev(zndmedia.dev, lba, limit, 0);
    if (!rep)
        return ZND_MEDIA_REPORT_ERR;

    cmd->opaque = (void *)rep;  // NOLINT

    return XZTL_OK;
}

static int znd_media_zone_mgmt(struct xztl_zn_mcmd *cmd) {
    switch (cmd->opcode) {
        case XZTL_ZONE_MGMT_CLOSE:
            return znd_media_zone_manage(cmd,
                                         XNVME_SPEC_ZND_CMD_MGMT_SEND_CLOSE);
        case XZTL_ZONE_MGMT_FINISH:
            return znd_media_zone_manage(cmd,
                                         XNVME_SPEC_ZND_CMD_MGMT_SEND_FINISH);
        case XZTL_ZONE_MGMT_OPEN:
            return znd_media_zone_manage(cmd,
                                         XNVME_SPEC_ZND_CMD_MGMT_SEND_OPEN);
        case XZTL_ZONE_MGMT_RESET:
            xztl_stats_inc(XZTL_STATS_RESET_MCMD, 1);
            return znd_media_zone_manage(cmd,
                                         XNVME_SPEC_ZND_CMD_MGMT_SEND_RESET);
        case XZTL_ZONE_MGMT_REPORT:
            return znd_media_zone_report(cmd);
        default:
            return ZND_INVALID_OPCODE;
    }

    return XZTL_OK;
}

static void *znd_media_dma_alloc(size_t size) {
    return xnvme_buf_alloc(zndmedia.dev, size);
}

static void znd_media_dma_free(void *ptr) {
    xnvme_buf_free(zndmedia.dev, ptr);
}

static int znd_media_async_poke(struct xnvme_queue *queue, uint32_t *c,
                                uint16_t max) {
    int ret;
    ret = xnvme_queue_poke(queue, max);
    if (ret < 0)
        return ZND_MEDIA_POKE_ERR;

    *c = ret;

    return XZTL_OK;
}

static int znd_media_async_outs(struct xnvme_queue *queue, uint32_t *c) {
    int ret;

    ret = xnvme_queue_get_outstanding(queue);
    if (ret < 0)
        return ZND_MEDIA_OUTS_ERR;

    *c = ret;

    return XZTL_OK;
}

static int znd_media_async_wait(struct xnvme_queue *queue, uint32_t *c) {
    int ret;

    ret = xnvme_queue_get_outstanding(queue);
    if (ret)
        return ZND_MEDIA_WAIT_ERR;

    *c = ret;

    return XZTL_OK;
}

static int znd_media_asynch_init(struct xztl_misc_cmd *cmd) {
    struct xztl_mthread_ctx *tctx;
    int                      ret;

    tctx = cmd->asynch.ctx_ptr;

    ret = xnvme_queue_init(zndmedia.dev, cmd->asynch.depth, 0, &tctx->queue);
    if (ret) {
        return ZND_MEDIA_ASYNCH_ERR;
    }

    return XZTL_OK;
}

static int znd_media_asynch_term(struct xztl_misc_cmd *cmd) {
    int ret;

    ret = xnvme_queue_term(cmd->asynch.ctx_ptr->queue);
    if (ret)
        return ZND_MEDIA_ASYNCH_ERR;

    return XZTL_OK;
}

static int znd_media_cmd_exec(struct xztl_misc_cmd *cmd) {
    switch (cmd->opcode) {
        case XZTL_MISC_ASYNCH_INIT:
            return znd_media_asynch_init(cmd);

        case XZTL_MISC_ASYNCH_TERM:
            return znd_media_asynch_term(cmd);

        case XZTL_MISC_ASYNCH_POKE:
            return znd_media_async_poke(cmd->asynch.ctx_ptr->queue,
                                        &cmd->asynch.count, cmd->asynch.limit);

        case XZTL_MISC_ASYNCH_OUTS:
            return znd_media_async_outs(cmd->asynch.ctx_ptr->queue,
                                        &cmd->asynch.count);

        case XZTL_MISC_ASYNCH_WAIT:
            return znd_media_async_wait(cmd->asynch.ctx_ptr->queue,
                                        &cmd->asynch.count);

        default:
            return ZND_INVALID_OPCODE;
    }
}

static int znd_media_init(void) {
    return XZTL_OK;
}

static int znd_media_exit(void) {
    if (zndmedia.dev)
        xnvme_dev_close(zndmedia.dev);

    return XZTL_OK;
}

int znd_media_register(const char *dev_name) {
    const struct xnvme_geo *devgeo;
    struct xnvme_dev *      dev;
    struct xztl_media *     m;
    // char async[15] = "io_uring_cmd";
    char async[15] = "thrpool";

    struct xnvme_opts opts = xnvme_opts_default();
    opts.async             = &async;

    dev = xnvme_dev_open(dev_name, &opts);
    if (!dev)
        return ZND_MEDIA_NODEVICE;

    devgeo = xnvme_dev_get_geo(dev);
    if (!devgeo) {
        xnvme_dev_close(dev);
        return ZND_MEDIA_NOGEO;
    }

    zndmedia.dev    = dev;
    zndmedia.devgeo = devgeo;
    m               = &zndmedia.media;

    m->geo.ngrps      = devgeo->npugrp;
    m->geo.pu_grp     = devgeo->npunit;
    m->geo.zn_pu      = devgeo->nzone;
    m->geo.sec_zn     = devgeo->nsect;
    m->geo.nbytes     = devgeo->nbytes;
    m->geo.nbytes_oob = devgeo->nbytes_oob;

    m->init_fn   = znd_media_init;
    m->exit_fn   = znd_media_exit;
    m->submit_io = znd_media_submit_io;
    m->zone_fn   = znd_media_zone_mgmt;
    m->dma_alloc = znd_media_dma_alloc;
    m->dma_free  = znd_media_dma_free;
    m->cmd_exec  = znd_media_cmd_exec;

    return xztl_media_set(m);
}
