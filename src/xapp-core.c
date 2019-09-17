#include <stdlib.h>
#include <string.h>
#include <xapp.h>
#include <xapp-media.h>

static struct xapp_core core = {NULL};

static xapp_register_fn *media_fn = NULL;

int xapp_media_init (void)
{
    if (!core.media)
	return XAPP_NOMEDIA;

    if (!core.media->init_fn)
	return XAPP_NOINIT;

    return core.media->init_fn ();
}

int xapp_media_exit (void)
{
    if (!core.media)
	return XAPP_NOMEDIA;

    if (!core.media->exit_fn)
	return XAPP_NOEXIT;

    return core.media->exit_fn ();
}

static int xapp_media_check (struct xapp_media *media)
{
    struct xapp_mgeo *g;

    /* Check function pointers */
    if (!media->init_fn)
	return XAPP_NOINIT;

    if (!media->init_fn)
	return XAPP_NOEXIT;

    if (!media->submit_io)
	return XAPP_MEDIA_NOIO;

    if (!media->zone_fn)
	return XAPP_MEDIA_NOZONE;

    if (!media->dma_alloc)
	return XAPP_MEDIA_NOALLOC;

    if (!media->dma_free)
	return XAPP_MEDIA_NOFREE;

    /* Check the geometry */
    g = &media->geo;
    if (!g->ngrps  || g->ngrps   > XAPP_MEDIA_MAX_GRP   ||
	!g->pu_grp || g->pu_grp  > XAPP_MEDIA_MAX_PUGRP ||
	!g->zn_pu  || g->zn_pu   > XAPP_MEDIA_MAX_ZNPU  ||
	!g->sec_zn || g->sec_zn  > XAPP_MEDIA_MAX_SECZN ||
        !g->nbytes || g->nbytes  > XAPP_MEDIA_MAX_SECSZ ||
		      g->nbytes_oob  > XAPP_MEDIA_MAX_OOBSZ )
	return XAPP_MEDIA_GEO;

    /* Fill up geometry fields */
    g->zn_grp  = g->pu_grp  * g->zn_pu;
    g->sec_grp = g->zn_grp  * g->sec_zn;
    g->sec_pu  = g->zn_pu   * g->sec_zn;
    g->oob_grp = g->sec_grp * g->nbytes_oob;
    g->oob_pu  = g->sec_pu  * g->nbytes_oob;
    g->oob_zn  = g->sec_zn  * g->nbytes_oob;

    return XAPP_OK;
}

int xapp_media_set (struct xapp_media *media)
{
    int ret;

    ret = xapp_media_check (media);
    if (ret)
	return ret;

    core.media = malloc (sizeof (struct xapp_media));
    if (!core.media)
	return XAPP_MEM;

    memcpy (core.media, media, sizeof (struct xapp_media));

    return XAPP_OK;
}

void xapp_add_media (xapp_register_fn *fn)
{
    media_fn = fn;
}

int xapp_exit (void)
{
    if (core.media) {
	free (core.media);
	core.media = NULL;
    }

    return xapp_media_exit ();
}

int xapp_init (void)
{
    int ret;

    if (!media_fn)
	return XAPP_NOMEDIA;

    ret = media_fn ();
    if (ret)
	return XAPP_MEDIA_ERROR | ret;

    ret = xapp_media_init ();
    if (ret)
	return ret;

    return XAPP_OK;
}
