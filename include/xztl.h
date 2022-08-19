/* xZTL: Zone Translation Layer User-space Library
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

#ifndef XZTL_H
#define XZTL_H

#define _GNU_SOURCE

#include <pthread.h>
#include <stdint.h>
#include <sys/queue.h>
#include <syslog.h>
#include <unistd.h>

#define XZTL_DEV_NAME "q"

/* Debugging options */
#define ZDEBUG_PRO_GRP 0
#define ZDEBUG_PRO     0
#define ZDEBUG_MPE     0
#define ZDEBUG_MAP     0
#define ZDEBUG_IO      0
#define ZDEBUG_MEDIA_W 0
#define ZDEBUG_MEDIA_R 0
#define ZDEBUG_MP      0

#define ZDEBUG(type, format, ...)             \
    do {                                      \
        if ((type)) {                         \
            log_infoa(format, ##__VA_ARGS__); \
        }                                     \
    } while (0)

#define XZTL_PROMETHEUS 0

#define log_erra(format, ...)  syslog(LOG_ERR, format, ##__VA_ARGS__)
#define log_infoa(format, ...) syslog(LOG_INFO, format, ##__VA_ARGS__)
#define log_err(format)        syslog(LOG_ERR, format)
#define log_info(format)       syslog(LOG_INFO, format)

#define GET_NANOSECONDS(ns, ts)                           \
    do {                                                  \
        clock_gettime(CLOCK_REALTIME, &ts);               \
        (ns) = ((ts).tv_sec * 1000000000 + (ts).tv_nsec); \
    } while (0)

#define GET_MICROSECONDS(us, ts)                                  \
    do {                                                          \
        clock_gettime(CLOCK_REALTIME, &ts);                       \
        (us) = (((ts).tv_sec * 1000000) + ((ts).tv_nsec / 1000)); \
    } while (0)

#define TV_ELAPSED_USEC(tvs, tve, usec)                               \
    do {                                                              \
        (usec) = ((tve).tv_sec * (uint64_t)1000000 + (tve).tv_usec) - \
                 ((tvs).tv_sec * (uint64_t)1000000 + (tvs).tv_usec);  \
    } while (0)

#define TS_ADD_USEC(ts, tv, usec)                                       \
    do {                                                                \
        (ts).tv_sec = (tv).tv_sec;                                      \
        gettimeofday(&tv, NULL);                                        \
        (ts).tv_sec += ((tv).tv_usec + (usec) >= 1000000) ? 1 : 0;      \
        (ts).tv_nsec = ((tv).tv_usec + (usec) >= 1000000)               \
                           ? ((usec) - (1000000 - (tv).tv_usec)) * 1000 \
                           : ((tv).tv_usec + (usec)) * 1000;            \
    } while (0)

#undef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#undef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define APP_MAGIC           0x3c
#define METADATA_HEAD_MAGIC 0x4c

#define AND64                0xffffffffffffffff
#define MAX_CALLBACK_ERR_CNT 3

typedef int(xztl_init_fn)(void);
typedef int(xztl_exit_fn)(void);
typedef int(xztl_register_fn)(void);
typedef int(xztl_register_media_fn)(const char *dev_name);
typedef void *(xztl_thread)(void *arg);
typedef void(xztl_callback)(void *arg);

struct xztl_maddr {
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

#include <xztl-media.h>

#define XZTL_IO_MAX_MCMD 65536 /* 4KB sectors : 16 GB user buffers */
                               /* 512b sectors: 2 GB user buffers */
#define XZTL_WIO_MAX_MCMD 1024

struct xztl_mthread_info {
    pthread_t comp_tid;
    int       comp_active;
};

struct xztl_io_ucmd {
    uint64_t id;
    void    *buf;
    size_t   size;
    uint64_t offset;  // for read command
    uint16_t prov_type;
    uint8_t  app_md; /* Application is responsible for mapping/recovery */
    uint8_t  status;

    xztl_callback *callback;

    struct app_pro_addr *prov;
    struct xztl_io_mcmd *mcmd[XZTL_WIO_MAX_MCMD];

    uint64_t node_id[2];
    uint64_t start[2];
    uint64_t num[2];
    uint16_t pieces;

    uint16_t nmcmd;
    uint64_t moffset[XZTL_WIO_MAX_MCMD];
    uint32_t msec[XZTL_WIO_MAX_MCMD];
    uint16_t ncb;
    uint16_t completed;

    pthread_spinlock_t inflight_spin;
    volatile uint8_t   minflight[256];

    STAILQ_ENTRY(xztl_io_ucmd) entry;
};

struct xztl_core {
    struct xztl_media *media;
};

enum xztl_status {
    XZTL_OK                 = 0x00,
    XZTL_MEM                = 0x01,
    XZTL_NOMEDIA            = 0x02,
    XZTL_NOINIT             = 0x03,
    XZTL_NOEXIT             = 0x04,
    XZTL_MEDIA_NOGEO        = 0x05,
    XZTL_MEDIA_GEO          = 0x06,
    XZTL_MEDIA_NOIO         = 0x07,
    XZTL_MEDIA_NOZONE       = 0x08,
    XZTL_MEDIA_NOALLOC      = 0x09,
    XZTL_MEDIA_NOFREE       = 0x0a,
    XZTL_MCTX_MEM_ERR       = 0x0b,
    XZTL_MCTX_ASYNC_ERR     = 0x0c,
    XZTL_MCTX_MP_ERR        = 0x0d,
    XZTL_ZTL_PROV_ERR       = 0x0e,
    XZTL_ZTL_GROUP_ERR      = 0x0f,
    XZTL_ZTL_PROV_GRP_ERR   = 0x10,
    XZTL_ZTL_MOD_ERR        = 0x11,
    XZTL_ZTL_ZMD_REP        = 0x12,
    XZTL_ZTL_PROV_FULL      = 0x13,
    XZTL_ZTL_MPE_ERR        = 0x14,
    XZTL_ZTL_MAP_ERR        = 0x15,
    XZTL_ZTL_IO_ERR         = 0x16,
    XZTL_ZTL_APPEND_ERR     = 0x17,
    XZTL_ZTL_IO_S_ERR       = 0x18,
    XZTL_ZTL_IO_S2_ERR      = 0x19,
    XZTL_ZTL_MD_INIT_ERR    = 0x1a,
    XZTL_ZTL_MD_READ_ERR    = 0x1b,
    XZTL_ZTL_MD_WRITE_ERR   = 0x1c,
    XZTL_ZTL_MD_WRITE_FULL  = 0x1d,
    XZTL_ZTL_MD_RESET_ERR   = 0x1e,
    XZTL_ZTL_STATS_ERR      = 0x1f,
    XZTL_ZTL_PROMETHEUS_ERR = 0x20,
    XZTL_MEDIA_ERROR        = 0x100,
    XZTL_ZROCKS_INIT_ERR    = 0x101,
    XZTL_ZROCKS_WRITE_ERR   = 0x102,
    XZTL_ZROCKS_READ_ERR    = 0X103,
    XZTL_ZTL_MGMT_ERR       = 0X104
};

enum xztl_stats_io_types {
    XZTL_STATS_READ_BYTES = 0,
    XZTL_STATS_APPEND_BYTES,
    XZTL_STATS_READ_MCMD,
    XZTL_STATS_APPEND_MCMD,
    XZTL_STATS_RESET_MCMD,

    XZTL_STATS_READ_BYTES_U,
    XZTL_STATS_APPEND_BYTES_U,
    XZTL_STATS_READ_UCMD,
    XZTL_STATS_APPEND_UCMD,

    XZTL_STATS_RECYCLED_BYTES,
    XZTL_STATS_RECYCLED_ZONES,

    XZTL_STATS_WRITE_SUBMIT_FAIL,
    XZTL_STATS_READ_SUBMIT_FAIL,
    XZTL_STATS_WRITE_CALLBACK_FAIL,
    XZTL_STATS_READ_CALLBACK_FAIL,
    XZTL_STATS_MGMT_FAIL,
    XZTL_STATS_META_WRITE_FAIL,
    XZTL_STATS_META_READ_FAIL
};

/* Return xzlt core */
void get_xztl_core(struct xztl_core **tcore);

/* Compare and swap atomic operations */

void xztl_atomic_int8_update(uint8_t *ptr, uint8_t value);
void xztl_atomic_int16_update(uint16_t *ptr, uint16_t value);
void xztl_atomic_int32_update(uint32_t *ptr, uint32_t value);
void xztl_atomic_int64_update(uint64_t *ptr, uint64_t value);

/* Add media layer */
void xztl_add_media(xztl_register_media_fn *fn);

/* Set the media abstraction */
int xztl_media_set(struct xztl_media *media);

/* Initialize XApp instance */
int xztl_init(const char *device_name);

/* Safe shut down */
int xztl_exit(void);

/* Media functions */
void *xztl_media_dma_alloc(size_t bytes);
void  xztl_media_dma_free(void *ptr);
int   xztl_media_submit_zn(struct xztl_zn_mcmd *cmd);
int   xztl_media_submit_misc(struct xztl_misc_cmd *cmd);
int   xztl_media_submit_io(struct xztl_io_mcmd *cmd);

/* Thread context functions */
struct xztl_mthread_ctx *xztl_ctx_media_init(uint32_t depth);
int                      xztl_ctx_media_exit(struct xztl_mthread_ctx *tctx);

/* Layer specific functions (for testing) */
int xztl_media_init(void);
int xztl_media_exit(void);

void xztl_print_mcmd(struct xztl_io_mcmd *cmd);

/* Statistics */
int  xztl_stats_init(void);
void xztl_stats_exit(void);
void xztl_stats_add_io(struct xztl_io_mcmd *cmd);
void xztl_stats_inc(uint32_t type, uint64_t val);
void xztl_stats_print_io(void);
void xztl_stats_print_io_simple(void);

/* Prometheus */
int  xztl_prometheus_init(void);
void xztl_prometheus_exit(void);
void xztl_prometheus_add_io(struct xztl_io_mcmd *cmd);
void xztl_prometheus_add_wa(uint64_t user_writes, uint64_t zns_writes);
void xztl_prometheus_add_read_latency(uint64_t usec);

#endif /* XZTL_H */
