#ifndef XAPP_ZTL_H
#define XAPP_ZTL_H

#include <xapp.h>
#include <xapp-mempool.h>

#define APP_MOD_COUNT     9
#define APP_FN_SLOTS      32

#define APP_MAX_GRPS	  32
#define APP_PRO_MAX_OFFS  8  /* Maximum of offsets returned by new/get */

enum xapp_mod_types {
    ZTLMOD_BAD = 0x0,
    ZTLMOD_ZMD = 0x1,
    ZTLMOD_PRO = 0x2,
    ZTLMOD_MPE = 0x3,
    ZTLMOD_MAP = 0x4,
    ZTLMOD_LOG = 0x5,
    ZTLMOD_REC = 0x6,
    ZTLMOD_GC  = 0x7,
    ZTLMOD_WCA = 0x8,
};

/* BAD modules */
/* NOT USED */

/* ZMD modules */
#define LIBZTL_ZMD     0x4

/* PRO modules */
#define LIBZTL_PRO     0x2

/* MPE modules */
#define LIBZTL_MPE     0x4

/* MAP modules */
#define LIBZTL_MAP     0x2

/* WCA modules */
#define LIBZTL_WCA     0x3

/* GC modules */
#define LIBZTL_GC      0x2

/* LOG modules */
#define LIBZTL_LOG     0x2

/* REC modules */
#define LIBZTL_REC     0x2

enum xapp_gl_functions {
    XAPP_FN_GLOBAL      = 0,
    XAPP_FN_TRANSACTION = 1
};

enum xapp_zmd_flags {
    XAPP_ZMD_USED = (1 << 0),
    XAPP_ZMD_OPEN = (1 << 1),
    XAPP_ZMD_RSVD = (1 << 2), /* Reserved zone */
    XAPP_ZMD_AVLB = (1 << 3), /* Indicates the zone is valid and can be used */
    XAPP_ZMD_COLD = (1 << 4), /* Contains cold data recycled by GC */
    XAPP_ZMD_META = (1 << 5)  /* Contains metadata, such as log for recovery*/
};

struct app_magic {
    uint8_t  magic;
    uint8_t  rsv[7];
} __attribute__((packed));

struct app_zmd_entry {
    uint16_t             flags;
    struct xapp_maddr    addr;
    uint64_t             wptr;
    uint32_t             invalid_sec;
    uint32_t		 nblks;
    /* TODO: Decide how to store LPID list here */
} __attribute__((packed));

struct app_tiny_entry {
        struct xapp_maddr   addr;
} __attribute__((packed));

struct app_tiny_tbl {
    struct app_tiny_entry *tbl;
    uint8_t               *dirty;
    uint32_t               entries;
    uint32_t               entry_sz;
};

struct app_map_entry {
    uint64_t lba;
    uint64_t ppa;
} __attribute__((packed)); /* 16 bytes entry */

struct app_mpe {
    struct app_magic byte;
    uint32_t         entries;
    uint32_t         entry_sz;

    uint8_t             *tbl;
    uint32_t             ent_per_pg;
    struct app_tiny_tbl  tiny;   /* This is the 'tiny' table for checkpoint */
} __attribute__((packed));

struct app_zmd {
    struct app_magic byte;
    uint32_t         entries;
    uint32_t         entry_sz;

    uint8_t             *tbl;        /* This is the 'small' fixed table */
    uint32_t             ent_per_pg;
    struct app_tiny_tbl  tiny;       /* This is the 'tiny' table for checkpoint */
    struct znd_report   *report;
};

struct app_grp_flags {
    volatile uint8_t    active;
    volatile uint8_t    need_gc;
    volatile uint8_t    busy; /* Number of concurrent contexts using the group */
};

struct app_group {
    uint16_t                id;
    struct app_grp_flags    flags;
    struct app_zmd          zmd;
    struct app_mpe          mpe;
    void		   *pro;
    uint16_t                cp_zone;   /* Rsvd zone ID for checkpoint */
    LIST_ENTRY(app_group)   entry;
};

struct app_pro_addr {
    struct app_group    *grp;
    struct xapp_maddr    addr[APP_PRO_MAX_OFFS];
    uint16_t             naddr;
    uint16_t 		 thread_id;

    struct xapp_mp_entry *mp_entry;
};

/* ------- XApp: MODULE FUNCTIONS DEFINITION ------- */

typedef int     (app_grp_init)(void);
typedef void    (app_grp_exit)(void);
typedef struct app_group *
	        (app_grp_get)(uint16_t gid);
typedef int     (app_grp_get_list)(struct app_group **lgrp, uint16_t ngrps);

typedef int     (app_zmd_create)(struct app_group *grp);
typedef int     (app_zmd_flush) (struct app_group *grp);
typedef int     (app_zmd_load) (struct app_group *grp);
typedef void    (app_zmd_mark) (struct app_group *lgrp, uint64_t index);
typedef struct app_zmd_entry *
	        (app_zmd_get) (struct app_group *grp, uint32_t zone);
typedef void    (app_zmd_invalidate)(struct app_group *grp,
                                     struct xapp_maddr *addr, uint8_t full);

typedef int     (app_pro_init) (void);
typedef void    (app_pro_exit) (void);
typedef void    (app_pro_check_gc) (struct app_group *grp);
typedef int     (app_pro_finish_zone) (struct app_group *grp,
                                    uint32_t zid, uint8_t type);
typedef int     (app_pro_put_zone) (struct app_group *grp, uint32_t zid);
typedef struct app_pro_addr *
	        (app_pro_new) (uint32_t naddr, uint8_t type);
typedef void    (app_pro_free) (struct app_pro_addr *ctx);

struct app_groups {
    app_grp_init         *init_fn;
    app_grp_exit         *exit_fn;
    app_grp_get          *get_fn;
    app_grp_get_list     *get_list_fn;
};

struct app_zmd_mod {
    uint8_t              mod_id;
    char                *name;
    app_zmd_create      *create_fn;
    app_zmd_flush       *flush_fn;
    app_zmd_load        *load_fn;
    app_zmd_get         *get_fn;
    app_zmd_invalidate  *invalidate_fn;
    app_zmd_mark        *mark_fn;
};

struct app_pro_mod {
    uint8_t              mod_id;
    char                *name;
    app_pro_init        *init_fn;
    app_pro_exit        *exit_fn;
    app_pro_check_gc    *check_gc_fn;
    app_pro_finish_zone *finish_zn_fn;
    app_pro_put_zone    *put_zone_fn;
    app_pro_new	        *new_fn;
    app_pro_free	*free_fn;

};

struct app_global {
    struct app_groups     groups;

    void                    *mod_list[APP_MOD_COUNT][APP_FN_SLOTS];
    struct app_zmd_mod      *zmd;
    struct app_pro_mod      *pro;
};

/* Built-in group functions */

static inline int app_grp_switch_read (struct app_group *grp)
{
    return grp->flags.active;
}

static inline void app_grp_switch_on (struct app_group *grp)
{
    uint8_t old;
    do {
	old = grp->flags.active;
    } while (!__sync_bool_compare_and_swap(&grp->flags.active, old, 1));
}

static inline void app_grp_switch_off (struct app_group *grp)
{
    uint8_t old;
    do {
	old = grp->flags.active;
    } while (!__sync_bool_compare_and_swap(&grp->flags.active, old, 0));
}

static inline int app_grp_need_gc (struct app_group *grp)
{
    return grp->flags.need_gc;
}

static inline void app_grp_need_gc_on (struct app_group *grp)
{
    uint8_t old;
    do {
	old = grp->flags.need_gc;
    } while (!__sync_bool_compare_and_swap(&grp->flags.need_gc, old, 1));
}

static inline void app_grp_need_gc_off (struct app_group *grp)
{
    uint8_t old;
    do {
	old = grp->flags.need_gc;
    } while (!__sync_bool_compare_and_swap(&grp->flags.need_gc, old, 0));
}

static inline int app_grp_ctxs_read (struct app_group *grp)
{
    return grp->flags.busy;
}

static inline void app_grp_ctx_add (struct app_group *grp)
{
    uint8_t old;
    do {
	old = grp->flags.busy;
    } while (!__sync_bool_compare_and_swap(&grp->flags.busy, old, old + 1));
}

static inline void app_grp_ctx_sub (struct app_group *grp)
{
    uint8_t old;
    do {
	old = grp->flags.busy;
    } while (!__sync_bool_compare_and_swap(&grp->flags.busy, old, old - 1));
}

/* ZTL core functions */

struct app_global *ztl (void);
int	ztl_mod_register (uint8_t modtype, uint8_t id, void *mod);
int     ztl_mod_set (uint8_t *modset);
int	ztl_init (void);
void	ztl_exit (void);

/* LIBZTL module registration */

void ztl_grp_register (void);
void ztl_zmd_register (void);
void ztl_pro_register (void);

#endif /* XAPP_ZTL_H */
