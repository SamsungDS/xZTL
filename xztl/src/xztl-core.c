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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <xztl.h>
#include <xztl-mods.h>
#include <xztl-media.h>
#include <xztl-stats.h>

static struct xztl_core core;

void get_xztl_core(struct xztl_core **tcore) {
    *tcore = &core;
}

void xztl_print_mcmd(struct xztl_io_mcmd *cmd) {
    printf("\n");
    printf("opcode : %d\n", cmd->opcode);
    printf("synch  : %d\n", cmd->synch);
    printf("naddr  : %d\n", cmd->naddr);
    printf("status : %d\n", cmd->status);
    printf("nlba0  : %lu\n", cmd->nsec[0]);
    printf("addr[0]: (%d/%d/%d/%lx)\n", cmd->addr[0].g.grp,
           cmd->addr[0].g.punit, cmd->addr[0].g.zone,
           (uint64_t)cmd->addr[0].g.sect);
    printf("prp0   : 0x%lx\n", cmd->prp[0]);
    printf("callba : %s\n", (cmd->callback) ? "OK" : "NULL");
    printf("async_c: %p\n", (void *)cmd->async_ctx);  // NOLINT
    printf("opaque : %p\n", cmd->opaque);
}

static xztl_register_media_fn *media_fn = NULL;

void *xztl_media_dma_alloc(size_t bytes) {
    return core.media->dma_alloc(bytes);
}

void xztl_media_dma_free(void *ptr) {
    core.media->dma_free(ptr);
}

int xztl_media_submit_io(struct xztl_io_mcmd *cmd) {
    if (ZDEBUG_MEDIA_W && (cmd->opcode == XZTL_CMD_WRITE))
        xztl_print_mcmd(cmd);
    if (ZDEBUG_MEDIA_R && (cmd->opcode == XZTL_CMD_READ))
        xztl_print_mcmd(cmd);

    return core.media->submit_io(cmd);
}

int xztl_media_submit_zn(struct xztl_zn_mcmd *cmd) {
    return core.media->zone_fn(cmd);
}

int xztl_media_submit_misc(struct xztl_misc_cmd *cmd) {
    return core.media->cmd_exec(cmd);
}

int xztl_media_init(void) {
    if (!core.media)
        return XZTL_NOMEDIA;

    if (!core.media->init_fn)
        return XZTL_NOINIT;

    return core.media->init_fn();
}

int xztl_media_exit(void) {
    if (!core.media)
        return XZTL_NOMEDIA;

    if (!core.media->exit_fn)
        return XZTL_NOEXIT;

    return core.media->exit_fn();
}

static int xztl_media_check(struct xztl_media *media) {
    struct xztl_mgeo *g;

    /* Check function pointers */
    if (!media->init_fn)
        return XZTL_NOINIT;

    if (!media->exit_fn)
        return XZTL_NOEXIT;

    if (!media->submit_io)
        return XZTL_MEDIA_NOIO;

    if (!media->zone_fn)
        return XZTL_MEDIA_NOZONE;

    if (!media->dma_alloc)
        return XZTL_MEDIA_NOALLOC;

    if (!media->dma_free)
        return XZTL_MEDIA_NOFREE;

    /* Check the geometry */
    g = &media->geo;
    if (!g->ngrps || g->ngrps > XZTL_MEDIA_MAX_GRP || !g->pu_grp ||
        g->pu_grp > XZTL_MEDIA_MAX_PUGRP || !g->zn_pu ||
        g->zn_pu > XZTL_MEDIA_MAX_ZNPU || !g->sec_zn ||
        g->sec_zn > XZTL_MEDIA_MAX_SECZN || !g->nbytes ||
        g->nbytes > XZTL_MEDIA_MAX_SECSZ ||
        g->nbytes_oob > XZTL_MEDIA_MAX_OOBSZ)
        return XZTL_MEDIA_GEO;

    /* Fill up geometry fields */
    g->zn_grp     = g->pu_grp * g->zn_pu;
    g->zn_dev     = g->zn_grp * g->ngrps;
    g->sec_grp    = g->zn_grp * g->sec_zn;
    g->sec_pu     = g->zn_pu * g->sec_zn;
    g->sec_dev    = g->sec_grp * g->ngrps;
    g->nbytes_zn  = g->nbytes * g->sec_zn * 1UL;
    g->nbytes_grp = g->nbytes_zn * g->zn_grp;
    g->oob_grp    = g->sec_grp * g->nbytes_oob;
    g->oob_pu     = g->sec_pu * g->nbytes_oob;
    g->oob_zn     = g->sec_zn * g->nbytes_oob;

    return XZTL_OK;
}

int xztl_media_set(struct xztl_media *media) {
    int ret;

    ret = xztl_media_check(media);
    if (ret)
        return ret;

    core.media = malloc(sizeof(struct xztl_media));
    if (!core.media)
        return XZTL_MEM;

    memcpy(core.media, media, sizeof(struct xztl_media));

    return XZTL_OK;
}

void xztl_add_media(xztl_register_media_fn *fn) {
    media_fn = fn;
}

int xztl_exit(void) {
    int ret;

    xztl_stats_exit();
    ztl_exit();

    ret = xztl_media_exit();
    if (ret)
        log_err("xztl_exit: Could not exit media.");

    xztl_mempool_exit();

    if (core.media) {
        free(core.media);
        core.media = NULL;
    }

    log_info("xztl_exit: xZTL is closed succesfully.");

    xztl_stats_print_io_simple();
    return ret;
}

int xztl_init(const char *dev_name) {
    int ret;

    openlog("ztl", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL0);

    log_info("xztl_init: Starting xZTL...");

    if (!media_fn)
        return XZTL_NOMEDIA;

    ret = media_fn(dev_name);
    if (ret)
        return XZTL_MEDIA_ERROR | ret;

    ret = xztl_mempool_init();
    if (ret)
        return ret;

    ret = xztl_media_init();
    if (ret)
        goto MP;

    ret = ztl_init();
    if (ret)
        goto MEDIA;

    ret = xztl_stats_init();
    if (ret)
        goto ZTL;

    log_info("xztl_init: xZTL started successfully.");
    return XZTL_OK;

ZTL:
    ztl_exit();
MEDIA:

    xztl_media_exit();
MP:
    xztl_mempool_exit();
    return ret;
}
