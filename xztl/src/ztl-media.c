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
#include <string.h>
#include <libxnvme.h>
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
#include <xztl-stats.h>

#define XZTL_MAX_CALLBACK_THREAD 10

#define BE_PARAM                    "spdk"
#define MIN_OPT_PARAM_LEN         10

struct znd_media zndmedia;

const char* be_str[20] = {
    "",
    "thrpool",
    "libaio",
    "io_uring",
    "io_uring_cmd",
    "nvme"
};

static inline int is_blk_thrpool(struct znd_opt_info *opt_info) {
    if (opt_info->opt_dev_type == OPT_DEV_BLK &&
        opt_info->opt_async == OPT_BE_THRPOOL) {
        return 1;
    }
    return 0;
}

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
        log_erra("znd_media_async_cb: err status[%u] opaque [%p]\n",
                 cmd->status, cmd->opaque);
        xztl_print_mcmd(cmd);
        xnvme_cmd_ctx_pr(ctx, XNVME_PR_DEF);
    }

    cmd->callback(cmd);

    struct xztl_mthread_ctx *tctx = cmd->async_ctx;
    xnvme_queue_put_cmd_ctx(tctx->queue, cmd->media_ctx);
}

static int znd_media_submit_read_synch(struct xztl_io_mcmd *cmd) {
    uint64_t          slba;
    uint16_t          sec_i = 0;
    struct timespec   ts_s, ts_e;
    struct xnvme_dev *p_dev =
        (zndmedia.dev_read) ? zndmedia.dev_read : zndmedia.dev;

    struct xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(p_dev);

    /* The read path is not group based. It uses only sectors */
    slba = cmd->addr[sec_i].g.sect;

    int ret;

    GET_MICROSECONDS(cmd->us_start, ts_s);

    ret = xnvme_nvm_read(&ctx, xnvme_dev_get_nsid(p_dev), slba,
                         (uint16_t)cmd->nsec[sec_i] - 1,
                         (void *)cmd->prp[sec_i], NULL);

    GET_MICROSECONDS(cmd->us_end, ts_e);

    /* WARNING: Uncommenting this line causes performance drop */
    // xztl_prometheus_add_read_latency (cmd->us_end - cmd->us_start);

    if (ret) {
        log_erra("znd_media_submit_read_synch: err ret[%d] opaque [%p]\n", ret,
                 cmd->opaque);
        xztl_print_mcmd(cmd);
    }

    return ret;
}

static int znd_media_submit_read_asynch(struct xztl_io_mcmd *cmd) {
    uint16_t                 sec_i = 0;
    uint64_t                 slba;
    void                    *dbuf;
    struct xztl_mthread_ctx *tctx;
    struct xnvme_cmd_ctx    *xnvme_ctx;
    int                      ret;
    struct xnvme_dev        *p_dev =
        (zndmedia.dev_read) ? zndmedia.dev_read : zndmedia.dev;

    tctx      = cmd->async_ctx;
    xnvme_ctx = xnvme_queue_get_cmd_ctx(tctx->queue);

    dbuf = (void *)cmd->prp[sec_i];  // NOLINT

    /* The write path is not group based. It uses only sectors */
    slba = cmd->addr[sec_i].g.sect;

    xnvme_ctx->async.cb     = znd_media_async_cb;
    xnvme_ctx->async.cb_arg = (void *)cmd;

    xnvme_ctx->dev = p_dev;
    cmd->media_ctx = xnvme_ctx;

    ret = xnvme_nvm_read(xnvme_ctx, xnvme_dev_get_nsid(p_dev), slba,
                         (uint16_t)cmd->nsec[sec_i] - 1, dbuf, NULL);

    if (ret) {
        log_erra("znd_media_submit_read_asynch: err ret [%d] opaque [%p]\n",
                 ret, cmd->opaque);
        xnvme_queue_put_cmd_ctx(tctx->queue, xnvme_ctx);
        xztl_print_mcmd(cmd);
    }

    return ret;
}

static int znd_media_submit_write_synch(struct xztl_io_mcmd *cmd) {
    return ZND_INVALID_OPCODE;
}

static int znd_media_submit_write_asynch(struct xztl_io_mcmd *cmd) {
    uint16_t                 sec_i = 0;
    uint64_t                 slba;
    void                    *dbuf;
    struct xztl_mthread_ctx *tctx;
    struct xnvme_cmd_ctx    *xnvme_ctx;
    int                      ret;

    tctx      = cmd->async_ctx;
    xnvme_ctx = xnvme_queue_get_cmd_ctx(tctx->queue);

    dbuf = (void *)cmd->prp[sec_i];  // NOLINT

    /* The write path is not group based. It uses only sectors */
    slba = cmd->addr[sec_i].g.sect;

    xnvme_ctx->async.cb     = znd_media_async_cb;
    xnvme_ctx->async.cb_arg = (void *)cmd;  // NOLINT
    xnvme_ctx->dev          = zndmedia.dev;
    cmd->media_ctx          = xnvme_ctx;

    ret = xnvme_nvm_write(xnvme_ctx, xnvme_dev_get_nsid(zndmedia.dev), slba,
                          (uint16_t)cmd->nsec[sec_i] - 1, dbuf, NULL);

    if (ret) {
        log_erra(
            "znd_media_submit_write_asynch: xnvme_nvm_write err ret [%d]  "
            "opaque [%p]\n",
            ret, cmd->opaque);
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
    const void              *dbuf;
    struct xztl_mthread_ctx *tctx;
    struct xnvme_cmd_ctx    *xnvme_ctx;
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
    if (ret) {
        log_erra(
            "znd_media_submit_append_asynch: xnvme_znd_append err ret [%d] "
            "opaque [%p]\n",
            ret, cmd->opaque);
        xztl_print_mcmd(cmd);
    }

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
    return XZTL_OK;
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
    if (ret) {
        log_erra("znd_media_zone_manage: err ret [%d] cmd->addr.g.zone [%u]\n",
                 ret, cmd->addr.g.zone);
    }
    // return (ret) ? op : XZTL_OK;
    return ret;
}

static int znd_media_zone_report(struct xztl_zn_mcmd *cmd) {
    struct xnvme_znd_report *rep;
    size_t                   limit;
    uint32_t                 lba;
    struct xnvme_dev        *p_dev =
        (zndmedia.dev_read) ? zndmedia.dev_read : zndmedia.dev;

    // Reading everything until not necessary anymore
    lba = 0;  // ((zndmedia.devgeo->nzone * cmd->addr.g.grp) + cmd->addr.g.zone)
              // * zndmedia.devgeo->nsect;
    limit = 0;  // cmd->nzones;

	rep   = xnvme_znd_report_from_dev(p_dev, lba, limit, 0);

    if (!rep) {
        log_err("znd_media_zone_report: rep is NULL \n");
        return ZND_MEDIA_REPORT_ERR;
    }

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
    if (ret < 0) {
        log_erra("znd_media_async_poke: c [%u] max [%u]\n", *c, max);
        return ZND_MEDIA_POKE_ERR;
    }
    *c = ret;

    return XZTL_OK;
}

static int znd_media_async_outs(struct xnvme_queue *queue, uint32_t *c) {
    int ret;

    ret = xnvme_queue_get_outstanding(queue);
    if (ret < 0) {
        log_erra("znd_media_async_outs: c [%u]\n", *c);
        return ZND_MEDIA_OUTS_ERR;
    }

    *c = ret;

    return XZTL_OK;
}

static int znd_media_async_wait(struct xnvme_queue *queue, uint32_t *c) {
    int ret;

    ret = xnvme_queue_get_outstanding(queue);
    if (ret) {
        log_erra("znd_media_async_wait: c [%u]\n", *c);
        return ZND_MEDIA_WAIT_ERR;
    }

    *c = ret;

    return XZTL_OK;
}

static int znd_media_asynch_init(struct xztl_misc_cmd *cmd) {
    struct xztl_mthread_ctx *tctx;
    int                      ret;

    tctx = cmd->asynch.ctx_ptr;

    ret = xnvme_queue_init(zndmedia.dev, cmd->asynch.depth, 0, &tctx->queue);
    if (ret) {
        log_erra("znd_media_asynch_init: error depth [%u]\n",
                 cmd->asynch.depth);
        return ZND_MEDIA_ASYNCH_ERR;
    }

    return XZTL_OK;
}

static int znd_media_asynch_term(struct xztl_misc_cmd *cmd) {
    int ret;

    ret = xnvme_queue_term(cmd->asynch.ctx_ptr->queue);
    if (ret) {
        log_erra("znd_media_asynch_term: error ret [%u]\n", ret);
        return ZND_MEDIA_ASYNCH_ERR;
    }
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
    if (zndmedia.dev_read)
        xnvme_dev_close(zndmedia.dev_read);

    return XZTL_OK;
}

int znd_opt_parse(const char *dev_name,  struct znd_opt_info* opt_info) {
    char dev_info[MAX_BUF_LEN] = {0};
    char be_info[MAX_BUF_LEN] = {0};
    char opt_end_name[MAX_BUF_LEN] = {0};

    if (strlen(dev_name) < MIN_OPT_PARAM_LEN ||
        strlen(dev_name) > MAX_STR_LEN) {
        log_erra("znd_opt_parse: err param '%s'", dev_name);
        return ZND_MEDIA_NODEVICE;
    }

    if (dev_name[0] == '/') {
        sscanf(dev_name, "%64[^\?]%64s", dev_info, be_info);

        strncpy(opt_info->opt_dev_name, dev_info, strlen(dev_info) + 1);

        if (dev_name[6] == 'g') {
            opt_info->opt_dev_type = OPT_DEV_CHAR;
        } else if (dev_name[6] == 'v') {
            opt_info->opt_dev_type = OPT_DEV_BLK;
        } else {
            log_erra("znd_opt_parse: err param '%s'\n", dev_name);
            return ZND_MEDIA_NODEVICE;
        }

        if (!strcmp(be_info, "")) {
            opt_info->opt_async = OPT_BE_THRPOOL;
        } else {
            sscanf(be_info, "\?be=%64s", opt_end_name);

            int i = OPT_BE_THRPOOL;
            for (; i <= OPT_BE_IOURING_CMD; i++) {
                if (!strcmp(opt_end_name, be_str[i])) {
                    opt_info->opt_async = i;
                    break;
                }
            }
            if (i > OPT_BE_IOURING_CMD) {
                log_erra("znd_opt_parse: err backend name '%s'", opt_end_name);
                return ZND_MEDIA_NODEVICE;
            }
        }
    } else if (dev_name[0] == 'p') {
        sscanf(dev_name, "%64[^\?]%64s", dev_info, be_info);

        opt_info->opt_dev_type = OPT_DEV_SPDK;
        opt_info->opt_async    = OPT_BE_SPDK;
        sscanf(dev_info, "pci:%64s", opt_info->opt_dev_name);
        sscanf(be_info, "\?nsid=%64d", &opt_info->opt_nsid);
    } else {
        log_erra("znd_opt_parse : err param '%s'", dev_name);
        return ZND_MEDIA_NODEVICE;
    }

    log_infoa("znd_opt_parse: async=%s, nsid=%d", be_str[opt_info->opt_async],
              opt_info->opt_nsid);

    return XZTL_OK;
}

void znd_opt_assign(struct xnvme_opts *opts, struct xnvme_opts *opts_read,
                    struct znd_opt_info *opt_info) {
    opts->async  = be_str[opt_info->opt_async];
    opts->direct = 1;
    if (opt_info->opt_async == OPT_BE_SPDK) {
        opts->be   = BE_PARAM;
        opts->nsid = opt_info->opt_nsid;
        return;
    }
    if (is_blk_thrpool(opt_info)) {
        opts->rdwr   = 0;
        opts->wronly = 1;

        opts_read->async  = be_str[opt_info->opt_async];
        opts_read->direct = 0;
        opts_read->rdwr   = 0;
        opts_read->rdonly = 1;
    }
}

void znd_media_set_ctx_iodepth(struct znd_opt_info* opt_info, struct znd_media* zndmedia) {
    zndmedia->read_ctx_num = XZTL_READ_RS_NUM;
    zndmedia->io_depth = XZTL_CTX_NVME_DEPTH;

    if (opt_info->opt_async == OPT_BE_SPDK || opt_info->opt_async == OPT_BE_LIBAIO) {
        uint32_t cpu_num = sysconf(_SC_NPROCESSORS_CONF);
        zndmedia->read_ctx_num = cpu_num - 2 > 0 ? (cpu_num - 2) : 1;
        if (opt_info->opt_async == OPT_BE_LIBAIO) {
            zndmedia->io_depth = XZTL_CTX_NVME_LIBAIO_DEPTH;
        }
    }

    log_infoa("znd_media_set_ctx_iodepth opt[%d] read_ctx_num[%u] io_depth[%u]",
        opt_info->opt_async, zndmedia->read_ctx_num, zndmedia->io_depth);
}

int znd_media_register(const char *dev_name) {
    const struct xnvme_geo *devgeo;
    struct xnvme_dev       *dev      = NULL;
    struct xnvme_dev       *dev_read = NULL;
    struct xztl_media      *m;
    struct xnvme_opts       opts      = xnvme_opts_default();
    struct xnvme_opts       opts_read = xnvme_opts_default();
    int                     ret       = 0;

    struct znd_opt_info opt_info;
    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    ret = znd_opt_parse(dev_name, &opt_info);
    if (ret) {
        log_erra("znd_media_register: znd_opt_parse ret %d \n", ret);
        return ret;
    }

    znd_opt_assign(&opts, &opts_read, &opt_info);

    dev = xnvme_dev_open(opt_info.opt_dev_name, &opts);
    if (!dev) {
        log_erra("znd_media_register: err  xnvme_dev_open dev \n");
        return ZND_MEDIA_NODEVICE;
    }

    if (is_blk_thrpool(&opt_info)) {
        dev_read = xnvme_dev_open(opt_info.opt_dev_name, &opts_read);
        if (!dev_read) {
            log_err("znd_media_register: err  xnvme_dev_open dev_read \n");
            return ZND_MEDIA_NODEVICE;
        }
    }

    // int direct = 0x4000;
    // int fd_direct  = open(dev_name, O_RDONLY  | O_LARGEFILE);
    devgeo = xnvme_dev_get_geo(dev);
    if (!devgeo) {
        xnvme_dev_close(dev);
        log_erra("znd_media_register: !devgeo\n");
        return ZND_MEDIA_NOGEO;
    }

    znd_media_set_ctx_iodepth(&opt_info, &zndmedia);
    zndmedia.dev      = dev;
    zndmedia.dev_read = dev_read;
    zndmedia.devgeo   = devgeo;
    m                 = &zndmedia.media;

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

