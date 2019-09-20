#include <xapp.h>
#include <xapp-media.h>
#include <znd-media.h>
#include <libxnvme.h>

static struct znd_media zndmedia;

static int znd_media_submit_io (struct xapp_io_mcmd *cmd)
{
    return 0;
}

static int znd_media_zone_mgmt (struct xapp_zn_mcmd *cmd)
{
    return 0;
}

static void *znd_media_dma_alloc (size_t size, uint64_t *phys)
{
    return malloc (1024);
}

static void znd_media_dma_free (void *ptr)
{
    free (ptr);
}

static int znd_media_init (void)
{
    return 0;
}

static int znd_media_exit (void)
{
    if (zndmedia.dev)
	xnvme_dev_close (zndmedia.dev);

    return 0;
}

int znd_media_register (void)
{
    const struct xnvme_geo *devgeo;
    struct xnvme_dev *dev;
    struct xapp_media *m;

    dev = xnvme_dev_open ("/dev/nvme0n2");
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

    return xapp_media_set (m);
}
