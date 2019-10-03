#include <xapp.h>
#include <xapp-media.h>
#include <znd-media.h>
#include <libxnvme.h>
#include <time.h>
#include <unistd.h>
#include <libznd.h>
#include <pthread.h>

static struct znd_media zndmedia;

static void znd_media_async_cb (struct xnvme_ret *ret, void *cb_arg)
{
    struct xapp_io_mcmd *cmd;

    cmd = (struct xapp_io_mcmd *) cb_arg;
    cmd->status = xnvme_ret_cpl_status (ret);

    cmd->callback (cmd);
}

static int znd_media_submit_append_synch (struct xapp_io_mcmd *cmd)
{
    return 0;
}

static int znd_media_submit_append_asynch (struct xapp_io_mcmd *cmd)
{
    uint16_t zone_i = 0;
    uint64_t zlba;
    const void *dbuf;
    struct xnvme_async_ctx *async_ctx;
    struct xnvme_ret *xret;

    async_ctx = (struct xnvme_async_ctx *) cmd->async_ctx;
    xret      = (struct xnvme_ret *) &cmd->media_ctx;

    dbuf = (const void *) cmd->prp[zone_i];
    zlba = cmd->addr[zone_i].g.zone * zndmedia.devgeo->nsect;

    xret->async.ctx    = async_ctx;
    xret->async.cb     = znd_media_async_cb;
    xret->async.cb_arg = (void *) cmd;

    return xnvme_cmd_zone_append (zndmedia.dev,
				     zlba,
			             cmd->nlba[zone_i],
				     dbuf,
				     NULL,
				     XNVME_CMD_ASYNC,
				     xret);
}

static int znd_media_submit_io (struct xapp_io_mcmd *cmd)
{
    switch (cmd->opcode) {
	case XAPP_ZONE_APPEND:
	    return (cmd->synch) ? znd_media_submit_append_synch (cmd) :
				  znd_media_submit_append_asynch (cmd);
	default:
	    return ZND_INVALID_OPCODE;
    }
    return 0;
}

static inline int znd_media_zone_manage (struct xapp_zn_mcmd *cmd, uint8_t op)
{
    uint32_t lba;
    uint8_t zrms = 0; /* Host managed */
    struct xnvme_ret devret;
    int ret;

    lba = cmd->addr.g.zone * zndmedia.devgeo->nsect;

    ret = xnvme_cmd_zone_mgmt(zndmedia.dev, lba, op,
			      zrms, NULL, 0, &devret);

    return (ret) ? ZND_MEDIA_OPEN_ERR : XAPP_OK;
}

static int znd_media_zone_report (struct xapp_zn_mcmd *cmd)
{
    struct znd_report *rep;
    size_t limit;
    uint32_t lba;

    /* TODO: Reading everything until libxnvme gets fixed */
    lba = /*cmd->addr.g.zone * zndmedia.devgeo->nsect*/ 0;
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
	    return znd_media_zone_manage (cmd, XNVME_SPEC_ZONE_MGMT_CLOSE);
	case XAPP_ZONE_MGMT_FINISH:
	    return znd_media_zone_manage (cmd, XNVME_SPEC_ZONE_MGMT_FINISH);
	case XAPP_ZONE_MGMT_OPEN:
	    return znd_media_zone_manage (cmd, XNVME_SPEC_ZONE_MGMT_OPEN);
	case XAPP_ZONE_MGMT_RESET:
	    return znd_media_zone_manage (cmd, XNVME_SPEC_ZONE_MGMT_RESET);
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
    struct xapp_misc_cmd *cmd;
    struct xnvme_async_ctx *ctx;
    uint32_t processed;
    uint16_t limit;
    int *active_ptr;

    cmd        = (struct xapp_misc_cmd *) args;
    active_ptr = (int *) cmd->asynch.active_ptr;
    ctx        = *(struct xnvme_async_ctx **) cmd->asynch.ctx_ptr;

    /* Set poke limit (we should tune) */
    limit       = 4;
    *active_ptr = 1;

    while (*active_ptr) {
	/* TODO: Define polling time */
	usleep (10);
	znd_media_async_poke (ctx, &processed, limit);
    }

    return XAPP_OK;
}

static int znd_media_asynch_init (struct xapp_misc_cmd *cmd)
{
    struct xnvme_async_ctx *ctx;
    struct xnvme_async_ctx **ctx_ptr;
    pthread_t *comp_tid;
    int *active_ptr;

    ctx_ptr = (struct xnvme_async_ctx **) cmd->asynch.ctx_ptr;
    comp_tid = (pthread_t *) cmd->asynch.comp_tid_ptr;

    ctx = xnvme_async_init (zndmedia.dev, cmd->asynch.depth, 0);
    if (!ctx) {
	return ZND_MEDIA_ASYNCH_ERR;
    }

    active_ptr  = (int *) cmd->asynch.active_ptr;
    *active_ptr = 0;
    *ctx_ptr    = ctx;

    if (pthread_create (comp_tid, NULL, znd_media_asynch_comp_th, (void *) cmd)) {
	*ctx_ptr  = NULL;
	xnvme_async_term (zndmedia.dev, ctx);
	return ZND_MEDIA_ASYNCH_TH;
    }
    while (! (*active_ptr) ) {
	usleep (1);
    }

    return XAPP_OK;
}

static int znd_media_asynch_term (struct xnvme_async_ctx *ptr, pthread_t *comp)
{
    int ret;

    /* Join the completion thread (should be terminated by the caller) */
    pthread_join (*comp, NULL);

    ret = xnvme_async_term (zndmedia.dev, ptr);
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
	    return znd_media_asynch_term (
			    (struct xnvme_async_ctx *) cmd->asynch.ctx_ptr,
			    (pthread_t *) cmd->asynch.comp_tid_ptr);

	case XAPP_MISC_ASYNCH_POKE:
	    return znd_media_async_poke (
			    (struct xnvme_async_ctx *) cmd->asynch.ctx_ptr,
			    &cmd->asynch.count,
			    cmd->asynch.limit);

        case XAPP_MISC_ASYNCH_OUTS:
	    return znd_media_async_outs (
			    (struct xnvme_async_ctx *) cmd->asynch.ctx_ptr,
			    &cmd->asynch.count);

	case XAPP_MISC_ASYNCH_WAIT:
	    return znd_media_async_wait (
			    (struct xnvme_async_ctx *) cmd->asynch.ctx_ptr,
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

int znd_media_register (void)
{
    const struct xnvme_geo *devgeo;
    struct xnvme_dev *dev;
    struct xapp_media *m;

    dev = xnvme_dev_open ("pci://0000:00:02.0/2");
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
