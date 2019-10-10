#include <stdlib.h>
#include <stdio.h>
#include <xapp.h>
#include <sys/queue.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <libznd.h>
#include <xapp-media.h>

extern struct xapp_core core;

LIST_HEAD(app_grp, app_group) app_grp_head = LIST_HEAD_INITIALIZER(app_grp_head);

static struct app_group *groups_get (uint16_t grp_id)
{
    struct app_group *grp;

    LIST_FOREACH (grp, &app_grp_head, entry) {
	if(grp->id == grp_id)
	    return grp;
    }

    return NULL;
}

static int groups_get_list (struct app_group **list, uint16_t ngrp)
{
    int n = 0;
    int i = ngrp - 1;
    struct app_group *grp;

    LIST_FOREACH (grp, &app_grp_head, entry) {
	if (i < 0)
	    break;
	list[i] = grp;
	i--;
	n++;
    }

    return n;
}

static void groups_zmd_exit (void)
{
    struct app_group *grp;

    LIST_FOREACH(grp, &app_grp_head, entry) {
	free (grp->zmd.tbl);
    }
}

static int groups_zmd_init (struct app_group *grp)
{
    struct app_zmd *zmd;
    struct xapp_mgeo *g;
    int ret;

    zmd = &grp->zmd;
    g   = &core.media->geo;

    zmd->entry_sz   = sizeof (struct app_zmd_entry);
    zmd->ent_per_pg = g->nbytes / zmd->entry_sz;
    zmd->entries    = g->zn_grp;

    zmd->tbl = calloc (zmd->entry_sz, g->zn_grp);
    if (!zmd->tbl)
	return -1;

    zmd->byte.magic = 0;

    ret = ztl()->zmd->load_fn (grp);
    if (ret)
	goto FREE;

    /* Create and flush zmd table if it does not exist */
    if (zmd->byte.magic == APP_MAGIC) {
	ret = ztl()->zmd->create_fn (grp);
	if (ret)
	    goto FREE;
    }

    /* TODO: Setup tiny table */

    log_infoa ("ztl-group: Zone MD started. Grp: %d", grp->id);

    return XAPP_OK;

FREE:
    free (zmd->tbl);
    log_erra ("ztl-group: Zone MD startup failed. Grp: %d", grp->id);

    return -1;
}

static void groups_free (void)
{
    struct app_group *grp;

    while (!LIST_EMPTY(&app_grp_head)) {
	grp = LIST_FIRST (&app_grp_head);
	LIST_REMOVE (grp, entry);
	free (grp);
    }
}

static void groups_exit (void)
{
    groups_zmd_exit ();
    groups_free ();
}

static int groups_init (void)
{
    struct app_group *grp;
    uint16_t grp_i;

    for (grp_i = 0; grp_i < core.media->geo.ngrps; grp_i++) {
	grp = calloc (sizeof (struct app_group), 1);
	if (!grp) {
	    log_err ("ztl-groups: Memory allocation failed");
	    return -1;
	}
	grp->id = grp_i;

	/* Initialize zone metadata */
	if (groups_zmd_init (grp))
	    goto FREE;

	/* Enable group */
	app_grp_switch_on (grp);

	LIST_INSERT_HEAD (&app_grp_head, grp, entry);
    }

    return XAPP_OK;

FREE:
    groups_free ();
    return -1;
}

void app_groups_register (void) {
    ztl()->groups.init_fn     = groups_init;
    ztl()->groups.exit_fn     = groups_exit;
    ztl()->groups.get_fn      = groups_get;
    ztl()->groups.get_list_fn = groups_get_list;

    LIST_INIT(&app_grp_head);
}
