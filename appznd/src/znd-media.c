#include <xapp.h>
#include <xapp-media.h>
#include <znd-media.h>
#include <libxnvme.h>

static struct zn_media znmedia;

static int zn_media_submit_io (struct xapp_io_mcmd *cmd)
{
    return 0;
}

static int zn_media_zone_mgmt (struct xapp_zn_mcmd *cmd)
{
    return 0;
}

static void *zn_media_dma_alloc (size_t size, uint64_t *phys)
{
    return malloc (1024);
}

static void zn_media_dma_free (void *ptr)
{
    free (ptr);
}

static int zn_media_init (void)
{
    return 0;
}

static int zn_media_exit (void)
{
    if (znmedia.dev)
	xnvme_dev_close (znmedia.dev);

    return 0;
}

int zn_media_register (void)
{
    const struct xnvme_geo *devgeo;
    struct xnvme_dev *dev;
    struct xapp_media *m;

    dev = xnvme_dev_open ("/dev/nvme0n2");
    if (!dev)
	return ZN_MEDIA_NODEVICE;

    devgeo = xnvme_dev_get_geo (dev);
    if (!devgeo) {
	xnvme_dev_close (dev);
	return ZN_MEDIA_NOGEO;
    }

    znmedia.dev    = dev;
    znmedia.devgeo = devgeo;
    m = &znmedia.media;

    m->geo.ngrps  	 = devgeo->npugrp;
    m->geo.pu_grp 	 = devgeo->npunit;
    m->geo.zn_pu  	 = devgeo->nzone;
    m->geo.sec_zn 	 = devgeo->nsect;
    m->geo.nbytes 	 = devgeo->nbytes;
    m->geo.nbytes_oob    = devgeo->nbytes_oob;

    m->init_fn   = zn_media_init;
    m->exit_fn   = zn_media_exit;
    m->submit_io = zn_media_submit_io;
    m->zone_fn   = zn_media_zone_mgmt;
    m->dma_alloc = zn_media_dma_alloc;
    m->dma_free  = zn_media_dma_free;

    return xapp_media_set (m);
}
