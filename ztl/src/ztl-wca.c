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
#include <xapp-ztl.h>
#include <lztl.h>
#include <unistd.h>

#define ZTL_MCMD_ENTS	 XAPP_IO_MAX_MCMD
#define ZTL_WCA_SEC_MCMD 64

extern struct xapp_core core;

STAILQ_HEAD (uc_head, xapp_io_ucmd)  ucmd_head;
static pthread_spinlock_t     	     ucmd_spin;
static struct xapp_mthread_ctx      *tctx;
static pthread_t		     wca_thread;
static uint8_t 			     wca_running;

/* This function checks if the media offsets are sequential.
 * If not, we return a negative value. For now we do not support
 * multi-piece mapping in ZTL-managed mapping */
static int ztl_wca_check_offset_seq (struct xapp_io_ucmd *ucmd)
{
    uint32_t off_i;

    for (off_i = 1; off_i < ucmd->nmcmd; off_i++) {
	if (ucmd->moffset[off_i] !=
	    ucmd->moffset[off_i - 1] + ucmd->msec[off_i - 1]) {

	    return -1;

	}
    }

    return 0;
}

/* This function prepares a multi-piece mapping to return to the user.
 * Each entry contains the offset and size, and the full list represents
 * the entire buffer. */
static void ztl_wca_reorg_ucmd_off (struct xapp_io_ucmd *ucmd)
{
    uint32_t off_i, curr, first_off, size;

    curr      = 0;
    first_off = 0;
    size      = 0;

    for (off_i = 1; off_i < ucmd->nmcmd; off_i++) {

	size += ucmd->msec[off_i - 1];

	/* If offset of sector is not sequential to the previousi one */
	if ( (ucmd->moffset[off_i] !=
	      ucmd->moffset[off_i - 1] + ucmd->msec[off_i - 1]) ||

	/* Or zone is not the same as the previous one */
	     (ucmd->mcmd[off_i]->addr[0].g.zone !=
	      ucmd->mcmd[off_i -1]->addr[0].g.zone) ) {

	    /* Close the piece and set first offset + size */
	    ucmd->moffset[curr] = ucmd->moffset[first_off];
	    ucmd->msec[curr]    = size;

	    first_off = off_i;
	    size = 0;
	    curr++;

	    /* If this is the last sector, we need to add it to the list */
	    if (off_i == ucmd->nmcmd - 1) {
		size += ucmd->msec[first_off];
		ucmd->moffset[curr] = ucmd->moffset[first_off];
		ucmd->msec[curr]    = size;
		curr++;
	    }

	/* If this is the last sector and belongs to the previous piece */
	} else if (off_i == ucmd->nmcmd - 1) {

	    /* Merge sector to the previous piece */
	    size += ucmd->msec[off_i];
	    ucmd->moffset[curr] = ucmd->moffset[first_off];
	    ucmd->msec[curr]    = size;
	    curr++;

	}
    }

    ucmd->noffs = (ucmd->nmcmd > 1) ? curr : 1;
}

static void ztl_wca_callback_mcmd (void *arg)
{
    struct xapp_io_ucmd  *ucmd;
    struct xapp_io_mcmd  *mcmd;
    struct app_map_entry map;
    struct app_zmd_entry *zmd;
    uint64_t old;
    int ret, off_i;

    mcmd = (struct xapp_io_mcmd *) arg;
    ucmd = (struct xapp_io_ucmd *) mcmd->opaque;

    if (mcmd->status) {
	ucmd->status = mcmd->status;
    } else {
	ucmd->moffset[mcmd->sequence] = mcmd->paddr[0];
    }

    xapp_atomic_int16_update (&ucmd->ncb, ucmd->ncb + 1);

    ZDEBUG (ZDEBUG_WCA, "ztl-wca: Callback. (ID %lu, S %d/%d, C %d, WOFF 0x%lx). St: %d",
						    ucmd->id,
						    mcmd->sequence,
						    ucmd->nmcmd,
						    ucmd->ncb,
						    ucmd->moffset[mcmd->sequence],
						    mcmd->status);

    xapp_mempool_put (mcmd->mp_cmd, XAPP_MEMPOOL_MCMD, ZTL_PRO_TUSER);

    if (ucmd->ncb == ucmd->nmcmd) {

	ucmd->noffs = 0;

	/* Update mapping if managed by the ZTL */
	if (!ucmd->status && !ucmd->app_md) {

	    /* Check if media offsets are sequential within the zone
	     * For ZTL-managed mapping, we do not support multi-piece entries */
	    if (!ztl_wca_check_offset_seq (ucmd)) {
		map.addr     = 0;
		map.g.offset = ucmd->moffset[0];
		map.g.nsec   = ucmd->msec[0];
		map.g.multi  = 0;
		ret = ztl()->map->upsert_fn (ucmd->id, map.addr, &old, 0);
		if (ret)
		    ucmd->status = XAPP_ZTL_MAP_ERR;
	    } else {
		ucmd->status = XAPP_ZTL_APPEND_ERR;
	    }
	}

	/* If command is successfull, reorganize media offsets for multi-piece
	 * mapping used by the user application */
	if (!ucmd->status)
	    ztl_wca_reorg_ucmd_off (ucmd);


	for (off_i = 0; off_i < ucmd->noffs; off_i++) {
	    zmd = ztl()->zmd->get_fn (ucmd->prov->grp, ucmd->moffset[off_i], 1);
	    xapp_atomic_int32_update (&zmd->npieces, zmd->npieces + 1);

	    if (ZDEBUG_WCA) {
		log_infoa ("ztl-wca: off_id %d, moff %lu, nsec %d. "
			"ZN(%d) pieces: %d\n", off_i, ucmd->moffset[off_i],
			ucmd->msec[off_i], zmd->addr.g.zone, zmd->npieces);
	    }
	}

	ztl()->pro->free_fn (ucmd->prov);

	if (ucmd->callback) {
	    ucmd->completed = 1;
	    ucmd->callback (ucmd);
	} else {
	    ucmd->completed = 1;
	}
    }
}

static void ztl_wca_callback (struct xapp_io_mcmd *mcmd)
{
    ztl_wca_callback_mcmd (mcmd);
}

static int ztl_wca_submit (struct xapp_io_ucmd *ucmd)
{
    pthread_spin_lock (&ucmd_spin);
    STAILQ_INSERT_TAIL (&ucmd_head, ucmd, entry);
    pthread_spin_unlock (&ucmd_spin);

    return 0;
}

static uint32_t ztl_wca_ncmd_prov_based (struct app_pro_addr *prov)
{
    uint32_t zn_i, ncmd;

    ncmd = 0;
    for (zn_i = 0; zn_i < prov->naddr; zn_i++) {
	ncmd += prov->nsec[zn_i] / ZTL_WCA_SEC_MCMD;
	if (prov->nsec[zn_i] % ZTL_WCA_SEC_MCMD != 0)
	    ncmd++;
    }

    return ncmd;
}

static void ztl_wca_process_ucmd (struct xapp_io_ucmd *ucmd)
{
    struct app_pro_addr *prov;
    struct xapp_mp_entry *mp_cmd;
    struct xapp_io_mcmd *mcmd;
    uint32_t nsec, nsec_zn, ncmd, ncmd_zn, cmd_i, zn_i, zncmd_i;
    uint64_t boff;
    int ret;

    ZDEBUG (ZDEBUG_WCA, "ztl-wca: Processing user write. ID %lu", ucmd->id);

    nsec = ucmd->size / core.media->geo.nbytes;

    /* We do not support non-aligned buffers */
    if (ucmd->size % core.media->geo.nbytes != 0)
	goto FAILURE;

    /* First we check the number of commands based on ZTL_WCA_SEC_MCMD */
    ncmd = nsec / ZTL_WCA_SEC_MCMD;
    if (nsec % ZTL_WCA_SEC_MCMD != 0)
	ncmd++;

    if (ncmd > XAPP_IO_MAX_MCMD) {
	log_erra ("ztl-wca: User command exceed XAPP_IO_MAX_MCMD. "
		"%d of %d", ncmd, XAPP_IO_MAX_MCMD);
	goto FAILURE;
    }

    /* Note: Provisioning types are user level metadata, if other
     * types of provisioning are added we need to support it here */
    prov = ztl()->pro->new_fn (nsec, ucmd->prov_type, ucmd->app_md);
    if (!prov)
	goto FAILURE;

    /* We check the number of commands again based on the provisioning */
    ncmd = ztl_wca_ncmd_prov_based (prov);
    if (ncmd > XAPP_IO_MAX_MCMD) {
	log_erra ("ztl-wca: User command exceed XAPP_IO_MAX_MCMD. "
		"%d of %d", ncmd, XAPP_IO_MAX_MCMD);
	goto FAIL_NCMD;
    }

    ucmd->prov  = prov;
    ucmd->nmcmd = ncmd;
    ucmd->completed = 0;
    ucmd->ncb = 0;

    boff = (uint64_t) ucmd->buf;

    ZDEBUG (ZDEBUG_WCA, "ztl-wca: NMCMD: %d", ncmd);

    /* Populate media commands */
    cmd_i = 0;
    for (zn_i = 0; zn_i < prov->naddr; zn_i++) {
	ncmd_zn = prov->nsec[zn_i] / ZTL_WCA_SEC_MCMD;
	if (prov->nsec[zn_i] % ZTL_WCA_SEC_MCMD != 0)
	    ncmd_zn++;

	nsec_zn = prov->nsec[zn_i];
	for (zncmd_i = 0; zncmd_i < ncmd_zn; zncmd_i++) {

	    /* We are using a memory pool for user commands, if other types
	     * such as GC in introduceds, we need to choose the provisioning
	     * type here */
	    mp_cmd = xapp_mempool_get (XAPP_MEMPOOL_MCMD, ZTL_PRO_TUSER);
	    if (!mp_cmd)
		goto FAIL_MP;

	    mcmd = (struct xapp_io_mcmd *) mp_cmd->opaque;

	    memset (mcmd, 0x0, sizeof (struct xapp_io_mcmd));
	    mcmd->mp_cmd    = mp_cmd;
	    mcmd->opcode    = XAPP_ZONE_APPEND;
	    mcmd->synch     = 0;
	    mcmd->sequence  = cmd_i;
	    mcmd->naddr     = 1;
	    mcmd->status    = 0;
	    mcmd->nsec[0]   = (nsec_zn >= ZTL_WCA_SEC_MCMD) ?
					  ZTL_WCA_SEC_MCMD : nsec_zn;
	    nsec_zn -= mcmd->nsec[0];
	    ucmd->msec[cmd_i] = mcmd->nsec[0];

	    mcmd->addr[0].g.grp  = prov->addr[zn_i].g.grp;
	    mcmd->addr[0].g.zone = prov->addr[zn_i].g.zone;

	    mcmd->prp[0] = boff;
	    boff += core.media->geo.nbytes * mcmd->nsec[0];

	    mcmd->callback  = ztl_wca_callback_mcmd;
	    mcmd->opaque    = ucmd;
	    mcmd->async_ctx = tctx;

	    ucmd->mcmd[cmd_i] = mcmd;
	    cmd_i++;
	}
    }

    ZDEBUG (ZDEBUG_WCA, "ztl-wca: Populated: %d", cmd_i);

    /* Submit media commands */
    for (cmd_i = 0; cmd_i < ncmd; cmd_i++) {
	ret = xapp_media_submit_io (ucmd->mcmd[cmd_i]);
	if (ret)
	    goto FAIL_SUBMIT;
    }

    ZDEBUG (ZDEBUG_WCA, "  Submitted: %d", cmd_i);

    return;

/* If we get a submit failure but previous I/Os have been
 * submitted, we fail all subsequent I/Os and completion is
 * performed by the callback function */
FAIL_SUBMIT:
    if (cmd_i) {
	ucmd->status = XAPP_ZTL_WCA_S2_ERR;
	xapp_atomic_int16_update (&ucmd->ncb, ncmd - cmd_i);

	/* Check for completion in case of completion concurrence */
	if (ucmd->ncb == ucmd->nmcmd) {
	    ucmd->completed = 1;
	    /* TODO: We do not perform cleanup here yet. We need to lock
	     * the callback thread to avoid double cleanup */
	}
    } else {
	cmd_i = ncmd;
	goto FAIL_MP;
    }
    return;

FAIL_MP:
    while (cmd_i) {
	cmd_i--;
	xapp_mempool_put (ucmd->mcmd[cmd_i]->mp_cmd,
			  XAPP_MEMPOOL_MCMD,
			  ZTL_PRO_TUSER);
	ucmd->mcmd[cmd_i]->mp_cmd = NULL;
	ucmd->mcmd[cmd_i] = NULL;
    }

FAIL_NCMD:
    for (zn_i = 0; zn_i < prov->naddr; zn_i++)
	prov->nsec[zn_i] = 0;
    ztl()->pro->free_fn (prov);

FAILURE:
    ucmd->status = XAPP_ZTL_WCA_S_ERR;

    if (ucmd->callback) {
	ucmd->completed = 1;
        ucmd->callback (ucmd);
    } else {
	ucmd->completed = 1;
    }
}

static void *ztl_wca_write_th (void *arg)
{
    struct xapp_io_ucmd *ucmd;

    wca_running = 1;

    while (wca_running) {
NEXT:
	if (!STAILQ_EMPTY (&ucmd_head)) {

	    pthread_spin_lock (&ucmd_spin);
	    ucmd = STAILQ_FIRST (&ucmd_head);
	    STAILQ_REMOVE_HEAD (&ucmd_head, entry);
	    pthread_spin_unlock (&ucmd_spin);

	    ztl_wca_process_ucmd (ucmd);

	    goto NEXT;
	}
	pthread_spin_unlock (&ucmd_spin);

	usleep (1);
    }

    return NULL;
}

static int ztl_wca_init (void)
{
    STAILQ_INIT (&ucmd_head);

    /* Initialize thread media context
     * If more write threads are to be used, we need more contexts */
    tctx = xapp_ctx_media_init (0, ZTL_MCMD_ENTS);
    if (!tctx)
	return XAPP_ZTL_WCA_ERR;

    if (pthread_spin_init (&ucmd_spin, 0))
	goto TCTX;

    if (pthread_create (&wca_thread, NULL, ztl_wca_write_th, NULL))
	goto SPIN;

    log_info ("ztl-wca: Write-caching started.");

    return 0;

SPIN:
    pthread_spin_destroy (&ucmd_spin);
TCTX:
    xapp_ctx_media_exit (tctx);
    return XAPP_ZTL_WCA_ERR;
}

static void ztl_wca_exit (void)
{
    wca_running = 0;
    pthread_join (wca_thread, NULL);
    pthread_spin_destroy (&ucmd_spin);
    xapp_ctx_media_exit (tctx);

    log_info ("ztl-wca: Write-caching stopped.");
}

static struct app_wca_mod libztl_wca = {
    .mod_id      = LIBZTL_WCA,
    .name        = "LIBZTL-WCA",
    .init_fn     = ztl_wca_init,
    .exit_fn     = ztl_wca_exit,
    .submit_fn   = ztl_wca_submit,
    .callback_fn = ztl_wca_callback
};

void ztl_wca_register (void) {
    ztl_mod_register (ZTLMOD_WCA, LIBZTL_WCA, &libztl_wca);
}
