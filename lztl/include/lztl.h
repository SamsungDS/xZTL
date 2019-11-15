#include <pthread.h>
#include <sys/queue.h>
#include <xapp.h>
#include <xapp-ztl.h>

#define ZTL_PRO_TYPES    1   /* Number of provisioning types */
#define ZTL_PRO_MP_SZ    32  /* Mempool size per thread */

/* Debug options */
#define ZDEBUG_PRO_GRP 0
#define ZDEBUG_PRO     1
#define ZDEBUG_MPE     1
#define ZDRBUG_MAP     1
#define ZDEBUG_WCA     1

#define ZDEBUG(type, format, ...) do {		\
    if ((type)) {				\
	log_infoa (format, ## __VA_ARGS__);	\
    }						\
} while ( 0 )


enum ztl_pro_type_list {
    ZTL_PRO_TUSER = 0x0
};

struct ztl_pro_zone {
    struct xapp_maddr		addr;
    struct app_zmd_entry       *zmd_entry;
    uint64_t 			capacity;
    uint8_t 			lock;
    uint8_t 			state;
    TAILQ_ENTRY (ztl_pro_zone) entry;
    TAILQ_ENTRY (ztl_pro_zone) open_entry;
};

struct ztl_pro_grp {
    struct ztl_pro_zone *vzones;
    uint32_t nfree;
    uint32_t nused;
    uint32_t nopen[ZTL_PRO_TYPES];

    TAILQ_HEAD (free_list, ztl_pro_zone) free_head;
    TAILQ_HEAD (used_list, ztl_pro_zone) used_head;

    /* Open zones for distinct provisioning types */
    TAILQ_HEAD (open_list, ztl_pro_zone) open_head[ZTL_PRO_TYPES];

    pthread_spinlock_t spin;
};

struct map_md_addr {
    union {
        struct {
            uint64_t addr  : 63;
            uint64_t flag : 1;
        } g;
        uint64_t addr;
    };
};

int  ztl_pro_grp_init (struct app_group *grp);
void ztl_pro_grp_exit (struct app_group *grp);
int  ztl_pro_grp_put_zone (struct app_group *grp, uint32_t zone_i);
int  ztl_pro_grp_get (struct app_group *grp, struct app_pro_addr *ctx,
					     uint32_t nsec, uint8_t ptype);
void ztl_pro_grp_free (struct app_group *grp, uint32_t zone_i,
					    uint32_t nsec, uint8_t type);
