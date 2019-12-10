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

#include <xapp.h>
#include <xapp-media.h>
#include <ztl-media.h>
#include <libxnvme.h>
#include <time.h>
#include <unistd.h>
#include <libznd.h>
#include <pthread.h>

static struct znd_media zndmedia;
extern char *dev_name;

static void znd_media_async_cb (struct xnvme_ret *ret, void *cb_arg)
{
    struct xapp_io_mcmd *cmd;

    cmd = (struct xapp_io_mcmd *) cb_arg;
    cmd->status = xnvme_ret_cpl_status (ret);

    if (cmd->opcode == XAPP_ZONE_APPEND && !cmd->status)
	cmd->paddr[0] = *(uint64_t *) &ret->cpl.cdw0;

    if (cmd->status)
	xapp_print_mcmd (cmd);

    cmd->callback (cmd);
}

static int znd_media_submit_read_synch (struct xapp_io_mcmd *cmd)
{
    struct xnvme_ret *xret;
    uint64_t slba;
    uint16_t sec_i = 0;

    xret = &cmd->media_ctx;
    xret->async.ctx    = NULL;
    xret->async.cb_arg = NULL;

    /* The read path is not group based. It uses only sectors */
    slba = cmd->addr[sec_i].g.sect;

    int ret;
    ret = xnvme_cmd_read (zndmedia.dev,
			    xnvme_dev_get_nsid (zndmedia.dev),
			    slba,
			    (uint16_t) cmd->nsec[sec_i] - 1,
			    (void *) cmd->prp[sec_i],
			    NULL,
			    0,
			    xret);

    if (ret)
	xapp_print_mcmd (cmd);

    return ret;
}

static int znd_media_submit_read_asynch (struct xapp_io_mcmd *cmd)
{
    uint16_t sec_i = 0;
    uint64_t slba;
    void *dbuf;
    struct xapp_mthread_ctx *tctx;
    struct xnvme_ret *xret;

    tctx = cmd->async_ctx;
    xret = &cmd->media_ctx;

    dbuf = (void *) cmd->prp[sec_i];

    /* The read path is not group based. It uses only sectors */
    slba = cmd->addr[sec_i].g.sect;

    xret->async.ctx    = tctx->asynch;
    xret->async.cb     = znd_media_async_cb;
    xret->async.cb_arg = (void *) cmd;

    return xnvme_cmd_read (zndmedia.dev,
			    xnvme_dev_get_nsid (zndmedia.dev),
			    slba,
			    (uint16_t) cmd->nsec[sec_i] - 1,
			    dbuf,
			    NULL,
			    XNVME_CMD_ASYNC,
			    xret);
}

static int znd_media_submit_append_synch (struct xapp_io_mcmd *cmd)
{
    return ZND_INVALID_OPCODE;
}

static int znd_media_submit_append_asynch (struct xapp_io_mcmd *cmd)
{
    uint16_t zone_i = 0;
    uint64_t zlba;
    const void *dbuf;
    struct xapp_mthread_ctx *tctx;
    struct xnvme_ret *xret;
    int ret;

    tctx      = cmd->async_ctx;
    xret      = &cmd->media_ctx;

    dbuf = (const void *) cmd->prp[zone_i];

    /* The write path separates zones into groups */
    zlba = (zndmedia.media.geo.zn_grp * cmd->addr[zone_i].g.grp +
	    cmd->addr[zone_i].g.zone) * zndmedia.devgeo->nsect;

    xret->async.ctx    = tctx->asynch;
    xret->async.cb     = znd_media_async_cb;
    xret->async.cb_arg = (void *) cmd;

    ret = znd_cmd_append (zndmedia.dev,
			  xnvme_dev_get_nsid (zndmedia.dev),
			  zlba,
			  (uint16_t) cmd->nsec[zone_i] - 1,
			  dbuf,
			  NULL,
			  XNVME_CMD_ASYNC,
			  xret);

    if (ret)
	xapp_print_mcmd (cmd);

    return ret;
}

static int znd_media_submit_io (struct xapp_io_mcmd *cmd)
{
    switch (cmd->opcode) {
	case XAPP_ZONE_APPEND:
	    return (cmd->synch) ? znd_media_submit_append_synch (cmd) :
				  znd_media_submit_append_asynch (cmd);
	case XAPP_CMD_READ:
	    return (cmd->synch) ? znd_media_submit_read_synch (cmd) :
				  znd_media_submit_read_asynch (cmd);
	default:
	    return ZND_INVALID_OPCODE;
    }
    return 0;
}

static inline int znd_media_zone_manage (struct xapp_zn_mcmd *cmd, uint8_t op)
{
    uint32_t lba;
    struct xnvme_ret devret;
    int ret;

    lba = ( (zndmedia.devgeo->nzone * cmd->addr.g.grp) +
	    cmd->addr.g.zone) * zndmedia.devgeo->nsect;

    devret.async.ctx    = NULL;
    devret.async.cb_arg = NULL;

    ret = znd_cmd_mgmt_send (zndmedia.dev,
			     xnvme_dev_get_nsid (zndmedia.dev),
			     lba,
			     op,
			     0,
			     NULL,
			     0,
			     &devret);

    cmd->status = (ret) ? xnvme_ret_cpl_status (&devret) : XAPP_OK;

    return (ret) ? op : XAPP_OK;
}

static int znd_media_zone_report (struct xapp_zn_mcmd *cmd)
{
    struct znd_report *rep;
    size_t limit;
    uint32_t lba;

    /* TODO: Reading everything until libxnvme gets fixed */
    lba = 0;/* ( (zndmedia.devgeo->nzone * cmd->addr.g.grp) +
	       cmd->addr.g.zone) * zndmedia.devgeo->nsect; */
    limit = /*cmd->nzones;*/ 0;
    rep = znd_report_from_dev (zndmedia.dev, lba, limit);
    if (!rep)
	return ZND_MEDIA_REPORT_ERR;

    cmd->opaque = (void *) rep;

    return XAPP_OK;
}

static int znd_media_zone_mgmt (struct xapp_zn_mcmd *cmd)
{
    switch (cmd->opcode) {
	case XAPP_ZONE_MGMT_CLOSE:
	    return znd_media_zone_manage (cmd, ZND_SEND_CLOSE);
	case XAPP_ZONE_MGMT_FINISH:
	    return znd_media_zone_manage (cmd, ZND_SEND_FINISH);
	case XAPP_ZONE_MGMT_OPEN:
	    return znd_media_zone_manage (cmd, ZND_SEND_OPEN);
	case XAPP_ZONE_MGMT_RESET:
	    return znd_media_zone_manage (cmd, ZND_SEND_RESET);
	case XAPP_ZONE_MGMT_REPORT:
	    return znd_media_zone_report (cmd);
	default:
	    return ZND_INVALID_OPCODE;
    }

    return XAPP_OK;
}

static void *znd_media_dma_alloc (size_t size, uint64_t *phys)
{
    return xnvme_buf_alloc (zndmedia.dev, size, phys);
}

static void znd_media_dma_free (void *ptr)
{
    xnvme_buf_free (zndmedia.dev, ptr);
}

static int znd_media_async_poke (struct xnvme_async_ctx *ctx,
				 uint32_t *c, uint16_t max)
{
    int ret;

    ret = xnvme_async_poke (zndmedia.dev, ctx, max);
    if (ret < 0)
	return ZND_MEDIA_POKE_ERR;

    *c = ret;

    return XAPP_OK;
}

static int znd_media_async_outs (struct xnvme_async_ctx *ctx, uint32_t *c)
{
    int ret;

    ret = xnvme_async_get_outstanding (ctx);
    if (ret < 0)
	return ZND_MEDIA_OUTS_ERR;

    *c = ret;

    return XAPP_OK;
}

static int znd_media_async_wait (struct xnvme_async_ctx *ctx, uint32_t *c)
{
    int ret;

    ret = xnvme_async_get_outstanding (ctx);
    if (ret)
	return ZND_MEDIA_WAIT_ERR;

    *c = ret;

    return XAPP_OK;
}

static void *znd_media_asynch_comp_th (void *args)
{
    struct xapp_misc_cmd    *cmd;
    struct xapp_mthread_ctx *tctx;
    uint32_t processed, outs;
    uint16_t limit;

    cmd        = (struct xapp_misc_cmd *) args;
    tctx       = cmd->asynch.ctx_ptr;

    /* Set poke limit (we should tune) */
    limit = 0;
    tctx->comp_active = 1;

    while (tctx->comp_active) {
	/* TODO: Define polling time */
	usleep (1);
	znd_media_async_poke (tctx->asynch, &processed, limit);

	if (!processed) {
	    /* Check outs in case of hanging controller */
	    znd_media_async_outs (tctx->asynch, &outs);
	}
    }

    return XAPP_OK;
}

static int znd_media_asynch_init (struct xapp_misc_cmd *cmd)
{
    struct xapp_mthread_ctx *tctx;

    tctx = cmd->asynch.ctx_ptr;

    tctx->asynch = xnvme_async_init (zndmedia.dev, cmd->asynch.depth, 0);
    if (!tctx->asynch) {
	return ZND_MEDIA_ASYNCH_ERR;
    }

    tctx->comp_active = 0;

    if (pthread_create (&tctx->comp_tid,
			NULL,
			znd_media_asynch_comp_th,
			(void *) cmd)) {

	xnvme_async_term (zndmedia.dev, tctx->asynch);
	tctx->asynch = NULL;
	return ZND_MEDIA_ASYNCH_TH;
    }

    /* Wait for the thread to start */
    while (!tctx->comp_active) {
	usleep (1);
    }

    return XAPP_OK;
}

static int znd_media_asynch_term (struct xapp_misc_cmd *cmd)
{
    int ret;

    /* Join the completion thread (should be terminated by the caller) */
    pthread_join (cmd->asynch.ctx_ptr->comp_tid, NULL);

    ret = xnvme_async_term (zndmedia.dev, cmd->asynch.ctx_ptr->asynch);
    if (ret)
	return ZND_MEDIA_ASYNCH_ERR;

    return XAPP_OK;
}

static int znd_media_cmd_exec (struct xapp_misc_cmd *cmd)
{
    switch (cmd->opcode) {

	case XAPP_MISC_ASYNCH_INIT:
	    return znd_media_asynch_init (cmd);

	case XAPP_MISC_ASYNCH_TERM:
	    return znd_media_asynch_term (cmd);

	case XAPP_MISC_ASYNCH_POKE:
	    return znd_media_async_poke (
			    cmd->asynch.ctx_ptr->asynch,
			    &cmd->asynch.count,
			    cmd->asynch.limit);

        case XAPP_MISC_ASYNCH_OUTS:
	    return znd_media_async_outs (
			    cmd->asynch.ctx_ptr->asynch,
			    &cmd->asynch.count);

	case XAPP_MISC_ASYNCH_WAIT:
	    return znd_media_async_wait (
			    cmd->asynch.ctx_ptr->asynch,
			    &cmd->asynch.count);

	default:
	    return ZND_INVALID_OPCODE;
    }
}

static int znd_media_init (void)
{
    return XAPP_OK;
}

static int znd_media_exit (void)
{
    if (zndmedia.dev)
	xnvme_dev_close (zndmedia.dev);

    return XAPP_OK;
}

int znd_media_register (const char *dev_name)
{
    const struct xnvme_geo *devgeo;
    struct xnvme_dev *dev;
    struct xapp_media *m;

    dev = xnvme_dev_open (dev_name);
    if (!dev)
	return ZND_MEDIA_NODEVICE;

    devgeo = xnvme_dev_get_geo (dev);
    if (!devgeo) {
	xnvme_dev_close (dev);
	return ZND_MEDIA_NOGEO;
    }

    zndmedia.dev    = dev;
    zndmedia.devgeo = devgeo;
    m = &zndmedia.media;

    m->geo.ngrps  	 = devgeo->npugrp;
    m->geo.pu_grp 	 = devgeo->npunit;
    m->geo.zn_pu  	 = devgeo->nzone;
    m->geo.sec_zn 	 = devgeo->nsect;
    m->geo.nbytes 	 = devgeo->nbytes;
    m->geo.nbytes_oob    = devgeo->nbytes_oob;

    m->init_fn   = znd_media_init;
    m->exit_fn   = znd_media_exit;
    m->submit_io = znd_media_submit_io;
    m->zone_fn   = znd_media_zone_mgmt;
    m->dma_alloc = znd_media_dma_alloc;
    m->dma_free  = znd_media_dma_free;
    m->cmd_exec  = znd_media_cmd_exec;

    return xapp_media_set (m);
}
