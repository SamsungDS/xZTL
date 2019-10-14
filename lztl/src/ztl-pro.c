#include <sys/queue.h>
#include <stdlib.h>
#include <xapp.h>
#include <xapp-ztl.h>
#include <lztl.h>

extern uint32_t app_ngrps;

void ztl_pro_free (struct app_pro_addr *ctx)
{

}

struct app_pro_addr *ztl_pro_new (uint32_t naddr, uint8_t type)
{
    return NULL;
}

int ztl_pro_put_zone (struct app_group *grp, uint32_t zid)
{
    return 0;
}

int ztl_pro_finish_zone (struct app_group *grp, uint32_t zid, uint8_t type)
{
    return 0;
}

void ztl_pro_check_gc (struct app_group *grp)
{

}

void ztl_pro_exit (void)
{
    struct app_group *list[app_ngrps];
    int ret;

    ret = ztl()->groups.get_list_fn (list, app_ngrps);
    if (ret != app_ngrps)
	log_infoa ("ztl-pro (exit): Groups mismatch (%d,%d).", ret, app_ngrps);

    while (ret) {
	ret--;
	ztl_pro_grp_exit (list[ret]);
    }
}

int ztl_pro_init (void)
{
    struct app_group *list[app_ngrps];
    int ret, grp_i;

    ret = ztl()->groups.get_list_fn (list, app_ngrps);
    if (ret != app_ngrps)
	return XAPP_ZTL_GROUP_ERR;

    for (grp_i = 0; grp_i < app_ngrps; grp_i++) {
	if (ztl_pro_grp_init (list[grp_i]))
	    goto EXIT;
    }

    return XAPP_OK;

EXIT:
    while (grp_i) {
	grp_i--;
	ztl_pro_grp_exit (list[grp_i]);
    }

    return XAPP_ZTL_GROUP_ERR;
}

static struct app_pro_mod ztl_pro = {
    .mod_id		= LIBZTL_PRO,
    .name		= "LIBZTL-PRO",
    .init_fn		= ztl_pro_init,
    .exit_fn		= ztl_pro_exit,
    .check_gc_fn	= ztl_pro_check_gc,
    .finish_zn_fn	= ztl_pro_finish_zone,
    .put_zone_fn	= ztl_pro_put_zone,
    .new_fn		= ztl_pro_new,
    .free_fn		= ztl_pro_free
};

void ztl_pro_register (void)
{
    ztl_mod_register (ZTLMOD_PRO, LIBZTL_PRO, &ztl_pro);
}
