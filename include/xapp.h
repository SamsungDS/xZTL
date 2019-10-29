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

#define AND64 	  0xffffffffffffffff

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

#define XAPP_IO_MAX_OFF	     16 /* Max number off zone offsets per command */
#define XAPP_IO_MAX_MCMD     64 /* 16 MB for 4BK sectors */

struct xapp_io_ucmd {
    uint64_t 	   id;
    void	  *buf;
    uint32_t 	   size;
    uint8_t 	   prov_type;
    uint8_t 	   app_md;  /* Application is responsible for mapping/recovery */
    uint8_t 	   status;
    uint64_t 	   status_mcmd;
    uint64_t 	   maddr[XAPP_IO_MAX_OFF];
    xapp_callback *callback_fn;

    struct xapp_io_mcmd *mcmd[XAPP_IO_MAX_MCMD];
};

struct xapp_core {
    struct xapp_media *media;
};

enum xapp_status {
    XAPP_OK		= 0x00,
    XAPP_MEM		= 0x01,
    XAPP_NOMEDIA	= 0x02,
    XAPP_NOINIT		= 0x03,
    XAPP_NOEXIT 	= 0x04,
    XAPP_MEDIA_NOGEO	= 0x05,
    XAPP_MEDIA_GEO	= 0x06,
    XAPP_MEDIA_NOIO	= 0x07,
    XAPP_MEDIA_NOZONE	= 0x08,
    XAPP_MEDIA_NOALLOC	= 0x09,
    XAPP_MEDIA_NOFREE	= 0x0a,
    XAPP_MCTX_MEM_ERR   = 0x0b,
    XAPP_MCTX_ASYNC_ERR = 0x0c,
    XAPP_MCTX_MP_ERR    = 0x0d,
    XAPP_ZTL_PROV_ERR   = 0x0e,
    XAPP_ZTL_GROUP_ERR  = 0x0f,
    XAPP_ZTL_ZMD_REP	= 0x10,
    XAPP_ZTL_PROV_FULL  = 0x11,
    XAPP_ZTL_MPE_ERR	= 0x12,
    XAPP_ZTL_MAP_ERR	= 0x13,
    XAPP_ZTL_WCA_ERR    = 0x14,

    XAPP_MEDIA_ERROR	= 0x100,
};

/* Compare and swap atomic operations */

void xapp_atomic_int8_update (uint8_t *ptr, uint8_t value);
void xapp_atomic_int16_update (uint16_t *ptr, uint16_t value);
void xapp_atomic_int32_update (uint32_t *ptr, uint32_t value);
void xapp_atomic_int64_update (uint64_t *ptr, uint64_t value);

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

#endif /* XAPP_H */
