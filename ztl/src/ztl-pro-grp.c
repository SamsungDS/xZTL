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

#include <sys/queue.h>
#include <stdlib.h>
#include <xapp.h>
#include <xapp-ztl.h>
#include <lztl.h>
#include <libznd.h>
#include <libxnvme_spec.h>

extern struct xapp_core core;

static void ztl_pro_grp_print_status (struct app_group *grp)
{
    struct ztl_pro_grp *pro;
    struct ztl_pro_zone *zone;
    uint32_t type_i;

    pro = (struct ztl_pro_grp *) grp->pro;

    printf ("\nztl-pro group %d: free %d, used %d\n", grp->id, pro->nfree, pro->nused);

    for (type_i = 0; type_i < ZTL_PRO_TYPES; type_i++) {
	printf (" OPEN: %d (T%d)\n", pro->nopen[type_i], type_i);
	TAILQ_FOREACH (zone, &pro->open_head[type_i], open_entry) {
	    printf ("  Zone: (%d/%d/0x%lx/0x%lx). Lock: %d\n",
					zone->addr.g.grp,
					zone->addr.g.zone,
	    		     (uint64_t) zone->addr.g.sect,
	    				zone->zmd_entry->wptr,
					zone->lock);
	}
    }
}

static struct ztl_pro_zone *ztl_pro_grp_zone_open (struct app_group *grp,
						   uint8_t ptype)
{
    struct ztl_pro_grp   *pro;
    struct ztl_pro_zone  *zone;
    struct xapp_zn_mcmd   cmd;
    struct app_zmd_entry *zmde;
    int ret;

    pro  = (struct ztl_pro_grp *) grp->pro;

    pthread_spin_lock (&pro->spin);
    zone = TAILQ_FIRST (&pro->free_head);
    if (!zone) {
	log_infoa ("ztl-pro: No zones left. Grp %d.", grp->id);
	pthread_spin_unlock (&pro->spin);
	return NULL;
    }
    TAILQ_REMOVE (&pro->free_head, zone, entry);
    TAILQ_INSERT_TAIL (&pro->used_head, zone, entry);
    pthread_spin_unlock (&pro->spin);

    xapp_atomic_int32_update (&pro->nfree, pro->nfree - 1);
    xapp_atomic_int32_update (&pro->nused, pro->nused + 1);

    zmde = zone->zmd_entry;

    /* Reset the zone if write pointer is at the end */
    if (zmde->wptr > zone->addr.g.sect) {
	cmd.opcode    = XAPP_ZONE_MGMT_RESET;
	cmd.addr.addr = zone->addr.addr;
	ret = xapp_media_submit_zn (&cmd);
    	if (ret || cmd.status) {
	    log_erra ("ztl-pro: Zone reset failure (%d/%d). status %d",
			zone->addr.g.grp, zone->addr.g.zone, cmd.status);
	    goto ERR;
	}
    }

    /* A single thread is used for each provisioning type, no lock needed */
    TAILQ_INSERT_TAIL (&pro->open_head[ptype], zone, open_entry);
    xapp_atomic_int32_update (&pro->nopen[ptype], pro->nopen[ptype] + 1);

    zmde->level = ptype;

    return zone;

ERR:
    /* Move zone out of provisioning */
    log_infoa ("ztl-pro (open): Zone reset failed. (%d/%d)",
					    grp->id, zone->addr.g.zone);
    xapp_atomic_int16_update (&zmde->flags, 0);

    pthread_spin_lock (&pro->spin);
    TAILQ_REMOVE (&pro->used_head, zone, entry);
    pthread_spin_unlock (&pro->spin);

    xapp_atomic_int32_update (&pro->nused, pro->nused - 1);

    return NULL;
}

struct ztl_pro_zone *ztl_pro_grp_get_best_zone (struct app_group *grp,
				uint32_t nsec, uint16_t ptype, uint8_t multi)
{
    struct ztl_pro_zone *zone;
    struct ztl_pro_grp  *pro;
    uint64_t off;
    uint32_t min_piece;

    pro = (struct ztl_pro_grp *) grp->pro;
    min_piece = (multi) ? APP_PRO_MIN_PIECE_SZ : nsec;

    /* Scan open zones: Pick the first available on the list */
    TAILQ_FOREACH (zone, &pro->open_head[ptype], open_entry) {
	off = (zone->zmd_entry->addr.g.sect + zone->capacity) - min_piece;
	if ( (zone->zmd_entry->wptr <= off) && !zone->lock)
	    return zone;

	/* TODO: Finish zone if space left is less than APP_PRO_MIN_PIECE_SZ */
    }

    return NULL;
}

int ztl_pro_grp_get (struct app_group *grp, struct app_pro_addr *ctx,
				uint32_t nsec, uint16_t ptype, uint8_t multi)
{
    struct ztl_pro_zone *zone;
    uint64_t sec_left, sec_avlb, zn_i;

    sec_left = nsec;
    zn_i = 0;
    ctx->naddr = 0;

    while (sec_left) {
	zone = ztl_pro_grp_get_best_zone (grp, nsec, ptype, multi);
	if (!zone) {
	    zone = ztl_pro_grp_zone_open (grp, ptype);
	    if (!zone)
		return -1;
	}
	zone->lock = 1;

	ctx->naddr++;
	ctx->addr[zn_i].addr = zone->addr.addr;

	sec_avlb = zone->zmd_entry->addr.g.sect + zone->capacity -
						    zone->zmd_entry->wptr;

	ctx->nsec[zn_i] = (sec_avlb > sec_left) ? sec_left : sec_avlb;

	sec_left -= ctx->nsec[zn_i];

	ZDEBUG (ZDEBUG_PRO, "ztl-pro-grp  (get): (%d/%d/0x%lx/0x%lx) type %d. sp: %d, sl: %lu",
			zone->addr.g.grp,
			zone->addr.g.zone,
	     (uint64_t) zone->addr.g.sect,
			zone->zmd_entry->wptr,
			ptype,
			ctx->nsec[zn_i],
			sec_left);

	zn_i++;
	if (sec_left && (zn_i >= APP_PRO_MAX_OFFS))
	    goto NO_LEFT;
    }

    if (ZDEBUG_PRO_GRP)
	ztl_pro_grp_print_status (grp);

    return 0;

NO_LEFT:
    while (zn_i) {
	zn_i--;
	ztl_pro_grp_free (grp, ctx->addr[zn_i].g.zone, 0, ptype);
	ctx->naddr--;
	ctx->addr[zn_i].addr = 0;
	ctx->nsec[zn_i] = 0;
    }

    return -1;
}

void ztl_pro_grp_free (struct app_group *grp, uint32_t zone_i,
					    uint32_t nsec, uint16_t type)
{
    struct ztl_pro_zone *zone;

    zone = &((struct ztl_pro_grp *) grp->pro)->vzones[zone_i];

    /* Move the write pointer */
    /* A single thread touches the write pointer, no lock needed */
    zone->zmd_entry->wptr += nsec;

    zone->lock = 0;

    ZDEBUG (ZDEBUG_PRO, "ztl-pro-grp (free): (%d/%d/0x%lx/0x%lx) type %d",
		zone->addr.g.grp,
		zone->addr.g.zone,
     (uint64_t) zone->addr.g.sect,
		zone->zmd_entry->wptr,
		type);

    if (ZDEBUG_PRO_GRP)
	ztl_pro_grp_print_status (grp);
}

int ztl_pro_grp_put_zone (struct app_group *grp, uint32_t zone_i)
{
    struct ztl_pro_zone  *zone;
    struct ztl_pro_grp   *pro;
    struct app_zmd_entry *zmde;

    pro  = (struct ztl_pro_grp *) grp->pro;
    zone = &pro->vzones[zone_i];
    zmde = zone->zmd_entry;

    if ( !(zmde->flags & XAPP_ZMD_AVLB) ) {
	log_infoa ("ztl-pro (put): Cannot PUT an invalid zone (%d/%d)",
							grp->id, zone_i);
	return -1;
    }

    if (zmde->flags & XAPP_ZMD_RSVD) {
	log_infoa ("ztl-pro (put): Zone is RESERVED (%d/%d)",
							grp->id, zone_i);
	return -2;
    }

    if ( !(zmde->flags & XAPP_ZMD_USED) ) {
	log_infoa ("ztl-pro (put): Zone is already EMPTY (%d/%d)",
							grp->id, zone_i);
	return -3;
    }

    if (zmde->flags & XAPP_ZMD_OPEN) {
	log_infoa ("ztl-pro (put): Zone is still OPEN (%d/%d)",
							grp->id, zone_i);
	return -4;
    }

    xapp_atomic_int16_update (&zmde->flags, zmde->flags ^ XAPP_ZMD_USED);
    xapp_atomic_int32_update (&pro->nused, pro->nused - 1);

    pthread_spin_lock (&pro->spin);
    TAILQ_REMOVE (&pro->used_head, zone, entry);
    TAILQ_INSERT_TAIL (&pro->free_head, zone, entry);
    pthread_spin_unlock (&pro->spin);

    xapp_atomic_int32_update (&pro->nfree, pro->nfree + 1);

    return 0;
}

static void ztl_pro_grp_zones_free (struct app_group *grp)
{
    struct ztl_pro_zone *zone;
    struct ztl_pro_grp  *pro;
    uint8_t ptype;

    pro = (struct ztl_pro_grp *) grp->pro;

    while (!TAILQ_EMPTY (&pro->used_head)) {
	zone = TAILQ_FIRST (&pro->used_head);
    	TAILQ_REMOVE (&pro->used_head, zone, entry);
    }

    while (!TAILQ_EMPTY (&pro->free_head)) {
	zone = TAILQ_FIRST (&pro->free_head);
        TAILQ_REMOVE (&pro->free_head, zone, entry);
    }

    for (ptype = 0; ptype < ZTL_PRO_TYPES; ptype++) {
	while (!TAILQ_EMPTY (&pro->open_head[ptype])) {
	    zone = TAILQ_FIRST (&pro->open_head[ptype]);
	    TAILQ_REMOVE (&pro->open_head[ptype], zone, open_entry);
	}
    }

    free (pro->vzones);
}

int ztl_pro_grp_init (struct app_group *grp)
{
    struct znd_descr *zinfo;
    struct znd_report    *rep;
    struct ztl_pro_zone  *zone;
    struct app_zmd_entry *zmde;
    struct ztl_pro_grp   *pro;
    uint8_t ptype;

    int ntype, zone_i;

    pro = calloc (sizeof (struct ztl_pro_grp), 1);
    if (!pro)
	return XAPP_ZTL_PROV_ERR;

    pro->vzones = calloc (sizeof (struct ztl_pro_zone), grp->zmd.entries);
    if (!pro->vzones) {
	free (pro);
	return XAPP_ZTL_PROV_ERR;
    }

    if (pthread_spin_init (&pro->spin, 0)) {
	free (pro->vzones);
	free (pro);
	return XAPP_ZTL_PROV_ERR;
    }

    grp->pro = pro;
    rep      = grp->zmd.report;

    TAILQ_INIT (&pro->free_head);
    TAILQ_INIT (&pro->used_head);

    for (ntype = 0; ntype < ZTL_PRO_TYPES; ntype++) {
	TAILQ_INIT (&pro->open_head[ntype]);
    }

    for (zone_i = 0; zone_i < grp->zmd.entries; zone_i++) {

	/* We are getting the full report here */
	zinfo = ZND_REPORT_DESCR (rep,
		    grp->id * core.media->geo.zn_grp + zone_i);

	zone = &pro->vzones[zone_i];

	zmde = ztl()->zmd->get_fn (grp, zone_i);

	if (zmde->addr.g.zone != zone_i || zmde->addr.g.grp != grp->id)
	    log_erra("ztl-pro: zmd entry address does not match (%d/%d)(%d/%d)",
		    zmde->addr.g.grp, zmde->addr.g.zone, grp->id, zone_i);

	if ( (zmde->flags & XAPP_ZMD_RSVD) ||
	    !(zmde->flags & XAPP_ZMD_AVLB) ) {
	    printf ("flags: %x\n", zmde->flags);
	    continue;
	}

	zone->addr.addr = zmde->addr.addr;
	zone->capacity  = zinfo->zcap;
	zone->state     = zinfo->zs;
	zone->zmd_entry = zmde;
	zone->lock      = 0;

	switch (zinfo->zs) {
	    case ZND_STATE_EMPTY:

		if ( (zmde->flags & XAPP_ZMD_USED) ||
		     (zmde->flags & XAPP_ZMD_OPEN) ) {
		    log_erra("ztl-pro: device reported EMPTY zone, but ZMD flag"
			     " does not match (%d/%d)(%x)",
			     grp->id, zone_i, zmde->flags);
		    continue;
		}

		zmde->invalid_sec = 0;
		zmde->nblks       = 0;
		TAILQ_INSERT_TAIL (&pro->free_head, zone, entry);
		pro->nfree++;

		ZDEBUG (ZDEBUG_PRO_GRP, " ZINFO: (%d/%d) empty\n",
				zmde->addr.g.grp, zmde->addr.g.zone);
		break;

	    case ZND_STATE_EOPEN:
	    case ZND_STATE_IOPEN:
	    case ZND_STATE_CLOSED:

		zmde->flags |= (XAPP_ZMD_OPEN | XAPP_ZMD_USED);

		TAILQ_INSERT_TAIL (&pro->used_head, zone, entry);

		 /* ZMD is not durable yet, so if a zone is already opened or
		  * full at startup, we assume it belongs to a single user
		  * provisioning defined by ZTL_PRO_TUSER */
		ptype = ZTL_PRO_TUSER;
		TAILQ_INSERT_TAIL (&pro->open_head[ptype], zone, open_entry);

		pro->nused++;
		pro->nopen[ptype]++;

		ZDEBUG (ZDEBUG_PRO_GRP, " ZINFO: (%d/%d) open\n",
				zmde->addr.g.grp, zmde->addr.g.zone);
		break;

	    case ZND_STATE_FULL:

		if (zmde->flags & XAPP_ZMD_OPEN) {
		    log_erra("ztl-pro: device reported FULL zone, but ZMD flag"
			    " does not match (%d/%d)(%x)",
			    grp->id, zone_i, zmde->flags);
		    continue;
		}

		zmde->flags |= XAPP_ZMD_USED;
		TAILQ_INSERT_TAIL (&pro->used_head, zone, entry);

		pro->nused++;

		ZDEBUG (ZDEBUG_PRO_GRP, " ZINFO: (%d/%d) full\n",
				zmde->addr.g.grp, zmde->addr.g.zone);
		break;

	    default:
		log_infoa ("ztl-pro: Unknown zone condition. zone %d, zs: %d",
			    grp->id * core.media->geo.zn_grp + zone_i, zinfo->zs);
	}

	zmde->wptr = zinfo->wp;
    }

    log_infoa ("ztl-pro: Started. Group %d.", grp->id);
    return 0;
}

void ztl_pro_grp_exit (struct app_group *grp)
{
    struct ztl_pro_grp *pro;

    pro = (struct ztl_pro_grp *) grp->pro;

    pthread_spin_destroy (&pro->spin);
    ztl_pro_grp_zones_free (grp);
    free (grp->pro);

    log_infoa ("ztl-pro: Stopped. Group %d.", grp->id);
}
