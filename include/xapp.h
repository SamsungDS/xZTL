#ifndef XAPP_H
#define XAPP_H

#include <stdint.h>
#include <unistd.h>
#include <sys/queue.h>
#include <syslog.h>

#define log_erra(format, ...)         syslog(LOG_ERR, format, ## __VA_ARGS__)
#define log_infoa(format, ...)        syslog(LOG_INFO, format, ## __VA_ARGS__)
#define log_err(format)               syslog(LOG_ERR, format)
#define log_info(format)              syslog(LOG_INFO, format)

#define GET_NANOSECONDS(ns,ts) do {                                     \
            clock_gettime(CLOCK_REALTIME,&ts);                          \
            (ns) = ((ts).tv_sec * 1000000000 + (ts).tv_nsec);           \
} while ( 0 )

#define GET_MICROSECONDS(us,ts) do {                                    \
            clock_gettime(CLOCK_REALTIME,&ts);                          \
            (us) = ( ((ts).tv_sec * 1000000) + ((ts).tv_nsec / 1000) ); \
} while ( 0 )

#define TV_ELAPSED_USEC(tvs,tve,usec) do {                              \
            (usec) = ((tve).tv_sec*(uint64_t)1000000+(tve).tv_usec) -   \
            ((tvs).tv_sec*(uint64_t)1000000+(tvs).tv_usec);             \
} while ( 0 )

#define TS_ADD_USEC(ts,tv,usec) do {                                     \
            (ts).tv_sec = (tv).tv_sec;                                   \
            gettimeofday (&tv, NULL);                                    \
            (ts).tv_sec += ((tv).tv_usec + (usec) >= 1000000) ? 1 : 0;   \
            (ts).tv_nsec = ((tv).tv_usec + (usec) >= 1000000) ?          \
                            ((usec) - (1000000 - (tv).tv_usec)) * 1000 : \
                            ((tv).tv_usec + (usec)) * 1000;              \
} while ( 0 )

#undef	MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#undef	MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#define APP_MAGIC 0x3c

typedef int   (xapp_init_fn)     (void);
typedef int   (xapp_exit_fn)     (void);
typedef int   (xapp_register_fn) (void);
typedef void *(xapp_thread)      (void *arg);
typedef void  (xapp_callback)    (void *arg);

struct xapp_maddr {
    union {
	struct {
	    uint64_t grp   : 5;
	    uint64_t punit : 3;
	    uint64_t zone  : 20; /* 1M zones */
	    uint64_t sect  : 36; /* 256TB for 4KB sectors */
	} g;
	uint64_t addr;
    };
};

#include <xapp-media.h>

struct xapp_core {
    struct xapp_media *media;
};

enum xapp_status {
    XAPP_OK		= 0x0,
    XAPP_MEM		= 0x1,
    XAPP_NOMEDIA	= 0x2,
    XAPP_NOINIT		= 0x3,
    XAPP_NOEXIT 	= 0x4,
    XAPP_MEDIA_NOGEO	= 0x5,
    XAPP_MEDIA_GEO	= 0x6,
    XAPP_MEDIA_NOIO	= 0x7,
    XAPP_MEDIA_NOZONE	= 0x8,
    XAPP_MEDIA_NOALLOC	= 0x9,
    XAPP_MEDIA_NOFREE	= 0xa,
    XAPP_MCTX_MEM_ERR   = 0xb,
    XAPP_MCTX_ASYNC_ERR = 0xc,
    XAPP_MCTX_MP_ERR    = 0xd,
    XAPP_ZTL_PROV_ERR   = 0xe,
    XAPP_ZTL_GROUP_ERR  = 0xf,

    XAPP_MEDIA_ERROR	= 0x100,
};

/* Add media layer */
void xapp_add_media (xapp_register_fn *fn);

/* Set the media abstraction */
int xapp_media_set (struct xapp_media *media);

/* Initialize XApp instance */
int xapp_init (void);

/* Safe shut down */
int xapp_exit (void);

/* Media functions */
void *xapp_media_dma_alloc   (size_t bytes, uint64_t *phys);
void  xapp_media_dma_free    (void *ptr);
int   xapp_media_submit_zn   (struct xapp_zn_mcmd *cmd);
int   xapp_media_submit_misc (struct xapp_misc_cmd *cmd);
int   xapp_media_submit_io   (struct xapp_io_mcmd *cmd);

/* Thread context functions */
struct xapp_mthread_ctx *xapp_ctx_media_init (uint16_t tid, uint32_t depth);
int                      xapp_ctx_media_exit (struct xapp_mthread_ctx *tctx);

/* Layer specific functions (for testing) */
int xapp_media_init (void);
int xapp_media_exit (void);

void xapp_print_mcmd (struct xapp_io_mcmd *cmd);


/* XApp: Zone Translation Layer Framework */


/* ------- XApp MODULE IDS ------- */

#define APP_MOD_COUNT        9
#define APP_FN_SLOTS         32

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

struct app_magic {
    uint8_t  magic;
    uint8_t  rsv[7];
} __attribute__((packed));

struct app_zmd_entry {
    uint16_t             flags;
    struct xapp_maddr    addr;
    uint32_t             wptr;
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
    uint16_t                cp_zone;   /* Rsvd zone ID for checkpoint */
    LIST_ENTRY(app_group)   entry;
};

struct app_pro_addr {
    struct app_group   **grp;
    struct xapp_maddr   *addr;
    uint16_t             naddr;
    uint16_t             ngrp;
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
typedef void    (app_pro_check_gc) (struct app_group *);
typedef int     (app_pro_finish_zone) (struct app_group *,
                                    struct xapp_maddr *addr, uint8_t type);
typedef int     (app_pro_put_zone) (struct app_group *, uint16_t, uint16_t);
typedef struct app_pro_ppas *
	        (app_pro_new) (uint32_t, uint8_t);
typedef void    (app_pro_free) (struct app_pro_ppas *);

struct app_groups {
    app_grp_init         *init_fn;
    app_grp_exit         *exit_fn;
    app_grp_get          *get_fn;
    app_grp_get_list     *get_list_fn;
};

struct app_zmd_mod {
    uint8_t             mod_id;
    char               *name;
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

void ztl_zmd_register (void);

#endif /* XAPP_H */
