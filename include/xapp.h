#ifndef XAPP_H
#define XAPP_H

#include <stdint.h>
#include <unistd.h>

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

typedef int   (xapp_init_fn)     (void);
typedef int   (xapp_exit_fn)     (void);
typedef int   (xapp_register_fn) (void);
typedef void *(xapp_thread)      (void *arg);
typedef void  (xapp_callback)    (void *arg);

#include <xapp-media.h>

struct xapp_core {
    struct xapp_media *media;
};
extern struct xapp_core core;

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

#endif /* XAPP_H */
