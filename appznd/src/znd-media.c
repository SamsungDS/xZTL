#include <xapp.h>
#include <xapp-media.h>
#include <znd-media.h>
#include <libxnvme.h>
#include <libznd.h>

static struct znd_media zndmedia;

static int znd_media_submit_io (struct xapp_io_mcmd *cmd)
{
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
    return malloc (1024);
}

static void znd_media_dma_free (void *ptr)
{
    free (ptr);
}

static int znd_media_asynch_init (uint32_t depth, struct xnvme_async_ctx **ptr)
{
    struct xnvme_async_ctx *ctx;

    ctx = xnvme_async_init (zndmedia.dev, depth, 0);
    if (!ctx)
	return ZND_MEDIA_ASYNCH_ERR;

    *ptr = ctx;

    return XAPP_OK;
}

static int znd_media_asynch_term (struct xnvme_async_ctx *ptr)
{
    int ret;

    ret = xnvme_async_term (zndmedia.dev, ptr);
    if (ret)
	return ZND_MEDIA_ASYNCH_ERR;

    return XAPP_OK;
}

static int znd_media_cmd_exec (struct xapp_misc_cmd *cmd)
{
    switch (cmd->opcode) {

	case XAPP_MISC_INIT_ASYNCH_CTX:
	    return znd_media_asynch_init (cmd->asynch.depth,
			    (struct xnvme_async_ctx **) cmd->asynch.ctx_ptr);

	case XAPP_MISC_TERM_ASYNCH_CTX:
	    return znd_media_asynch_term (
			    (struct xnvme_async_ctx *) cmd->asynch.ctx_ptr);

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
