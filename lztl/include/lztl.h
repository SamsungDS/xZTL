#include <xapp.h>

#define ZTL_PRO_TYPES 1

struct ztl_prov_zone {
    struct xapp_maddr		addr;
    uint8_t 			lock;
    TAILQ_ENTRY (ztl_prov_zone) entry;
};

struct ztl_prov_grp {
    struct ztl_prov_zone *vzones;
    uint32_t nempty;
    uint32_t nfull;
    uint32_t nopen[ZTL_PRO_TYPES];

    TAILQ_HEAD (empty_list, ztl_prov_zone) empty_head;
    TAILQ_HEAD (full_list, ztl_prov_zone)  full_head;
    TAILQ_HEAD (open_list, ztl_prov_zone)  open_head[ZTL_PRO_TYPES];
};

int  ztl_pro_grp_init (struct app_group *grp);
void ztl_pro_grp_exit (struct app_group *grp);
