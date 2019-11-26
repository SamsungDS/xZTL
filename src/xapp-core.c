#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <xapp.h>
#include <xapp-media.h>
#include <xapp-ztl.h>

struct xapp_core core = {NULL};

void xapp_atomic_int8_update (uint8_t *ptr, uint8_t value)
{
    uint8_t old;

    do {
	old = *ptr;
    } while (!__sync_bool_compare_and_swap (ptr, old, value));
}

void xapp_atomic_int16_update (uint16_t *ptr, uint16_t value)
{
    uint16_t old;

    do {
	old = *ptr;
    } while (!__sync_bool_compare_and_swap (ptr, old, value));
}

void xapp_atomic_int32_update (uint32_t *ptr, uint32_t value)
{
    uint32_t old;

    do {
	old = *ptr;
    } while (!__sync_bool_compare_and_swap (ptr, old, value));
}

void xapp_atomic_int64_update (uint64_t *ptr, uint64_t value)
{
    uint64_t old;

    do {
	old = *ptr;
    } while (!__sync_bool_compare_and_swap (ptr, old, value));
}

void xapp_print_mcmd (struct xapp_io_mcmd *cmd)
{
    printf ("\n");
    printf ("opcode : %d\n", cmd->opcode);
    printf ("synch  : %d\n", cmd->synch);
    printf ("naddr  : %d\n", cmd->naddr);
    printf ("status : %d\n", cmd->status);
    printf ("nlba0  : %lu\n", cmd->nsec[0]);
    printf ("addr[0]: (%d/%d/%d/%lx)\n", cmd->addr[0].g.grp,
				         cmd->addr[0].g.punit,
			                 cmd->addr[0].g.zone,
			      (uint64_t) cmd->addr[0].g.sect);
    printf ("prp0   : 0x%lx\n", cmd->prp[0]);
    printf ("callba : %s\n", (cmd->callback) ? "OK" : "NULL");
    printf ("async_c: %p\n", (void *) cmd->async_ctx);
    printf ("opaque : %p\n", cmd->opaque);
}

static xapp_register_media_fn *media_fn = NULL;

void *xapp_media_dma_alloc (size_t bytes, uint64_t *phys)
{
    return core.media->dma_alloc (bytes, phys);
}

void xapp_media_dma_free (void *ptr)
{
    core.media->dma_free (ptr);
}

int xapp_media_submit_io (struct xapp_io_mcmd *cmd)
{
    // xapp_print_mcmd (cmd);
    return core.media->submit_io (cmd);
}

int xapp_media_submit_zn (struct xapp_zn_mcmd *cmd)
{
    return core.media->zone_fn (cmd);
}

int xapp_media_submit_misc (struct xapp_misc_cmd *cmd)
{
    return core.media->cmd_exec (cmd);
}

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
    g->zn_grp     = g->pu_grp    * g->zn_pu;
    g->zn_dev     = g->zn_grp    * g->ngrps;
    g->sec_grp    = g->zn_grp    * g->sec_zn;
    g->sec_pu     = g->zn_pu     * g->sec_zn;
    g->sec_dev    = g->sec_grp   * g->ngrps;
    g->nbytes_zn  = g->nbytes    * g->sec_zn;
    g->nbytes_grp = g->nbytes_zn * g->zn_grp;
    g->oob_grp    = g->sec_grp   * g->nbytes_oob;
    g->oob_pu     = g->sec_pu    * g->nbytes_oob;
    g->oob_zn     = g->sec_zn    * g->nbytes_oob;

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

void xapp_add_media (xapp_register_media_fn *fn)
{
    media_fn = fn;
}

int xapp_exit (void)
{
    int ret;

    xapp_mempool_exit ();
    ztl_exit ();

    ret = xapp_media_exit ();
    if (ret)
	log_err ("core: Could not exit media.");

    if (core.media) {
	free (core.media);
	core.media = NULL;
    }

    log_info ("core: libztl is closed succesfully.");

    return ret;
}

int xapp_init (const char *dev_name)
{
    int ret;

    openlog("ztl" , LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL0);

    log_info ("core: Starting libztl...");

    if (!media_fn)
	return XAPP_NOMEDIA;

    ret = media_fn (dev_name);
    if (ret)
	return XAPP_MEDIA_ERROR | ret;

    ret = xapp_mempool_init ();
    if (ret)
	return ret;

    ret = xapp_media_init ();
    if (ret)
	goto MP;

    ret = ztl_init ();
    if (ret)
	goto MEDIA;

    log_info ("core: libztl started successfully.");

    return XAPP_OK;

MEDIA:
    xapp_media_exit ();
MP:
    xapp_mempool_exit ();
    return ret;
}
