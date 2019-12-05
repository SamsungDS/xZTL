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

#ifndef XAPP_H
#define XAPP_H

#include <stdint.h>
#include <unistd.h>
#include <sys/queue.h>
#include <syslog.h>

#define XAPP_MP_DEBUG 0

#define XAPP_DEV_NAME "pci://0000:03:00.0/2"

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
typedef int   (xapp_register_media_fn) (const char *dev_name);
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

#define XAPP_IO_MAX_MCMD     65536 /* 4KB sectors : 16 GB user buffers */
				   /* 512b sectors: 2 GB user buffers */

struct xapp_io_ucmd {
    uint64_t 	   id;
    void	  *buf;
    uint32_t 	   size;
    uint16_t 	   prov_type;
    uint8_t 	   app_md;  /* Application is responsible for mapping/recovery */
    uint8_t 	   status;

    xapp_callback *callback;

    struct app_pro_addr *prov;
    struct xapp_io_mcmd *mcmd[XAPP_IO_MAX_MCMD];

    uint16_t 	   nmcmd;
    uint16_t       noffs;
    uint64_t 	   moffset[XAPP_IO_MAX_MCMD];
    uint32_t 	   msec[XAPP_IO_MAX_MCMD];
    uint16_t 	   ncb;
    uint16_t 	   completed;

    STAILQ_ENTRY (xapp_io_ucmd)	entry;
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
    XAPP_ZTL_APPEND_ERR = 0x15,
    XAPP_ZTL_WCA_S_ERR  = 0x16,
    XAPP_ZTL_WCA_S2_ERR = 0x17,

    XAPP_MEDIA_ERROR	= 0x100,
};

/* Compare and swap atomic operations */

void xapp_atomic_int8_update (uint8_t *ptr, uint8_t value);
void xapp_atomic_int16_update (uint16_t *ptr, uint16_t value);
void xapp_atomic_int32_update (uint32_t *ptr, uint32_t value);
void xapp_atomic_int64_update (uint64_t *ptr, uint64_t value);

/* Add media layer */
void xapp_add_media (xapp_register_media_fn *fn);

/* Set the media abstraction */
int xapp_media_set (struct xapp_media *media);

/* Initialize XApp instance */
int xapp_init (const char *device_name);

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
