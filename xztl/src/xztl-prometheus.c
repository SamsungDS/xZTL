/* xZTL: Zone Translation Layer User-space Library
 *
 * Copyright 2020 Samsung Electronics
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>
#include <xztl-mempool.h>
#include <xztl.h>

struct xztl_prometheus_stats {
    uint64_t written_bytes;
    uint64_t read_bytes;
    uint64_t io_count;
    uint64_t user_write_bytes;
    uint64_t zns_write_bytes;

    /* Flushing thread Timing */
    struct timespec ts_s;
    struct timespec ts_e;
    uint64_t        us_s;
    uint64_t        us_e;

    /* Flushing latency thread Timing */
    struct timespec ts_l_s;
    struct timespec ts_l_e;
    uint64_t        us_l_s;
    uint64_t        us_l_e;
};

static pthread_t                    th_flush;
static struct xztl_prometheus_stats pr_stats;
static uint8_t                      xztl_flush_l_running, xztl_flush_running;

/* Latency queue */

#define MAX_LATENCY_ENTS 8192

struct latency_entry {
    uint64_t usec;
    void    *mp_entry;
    STAILQ_ENTRY(latency_entry) entry;
};

static pthread_t          latency_tid;
static pthread_spinlock_t lat_spin;
static STAILQ_HEAD(latency_head, latency_entry) lat_head;

static void xztl_prometheus_file_int64(const char *fname, uint64_t val) {
    FILE *fp;

    fp = fopen(fname, "w+");
    if (fp) {
        fprintf(fp, "%lu", val);
        fclose(fp);
    }
}

static void xztl_prometheus_file_double(const char *fname, double val) {
    FILE *fp;

    fp = fopen(fname, "w+");
    if (fp) {
        fprintf(fp, "%.6lf", val);
        fclose(fp);
    }
}

static void xztl_prometheus_reset(void) {
    uint64_t write, read, io;
    double   thput_w, thput_r, thput, wa;

    GET_MICROSECONDS(pr_stats.us_s, pr_stats.ts_s);

    write = pr_stats.written_bytes;
    read  = pr_stats.read_bytes;
    io    = pr_stats.io_count;

    ATOMIC_SWAP(&pr_stats.written_bytes, pr_stats.written_bytes, 0);
    ATOMIC_SWAP(&pr_stats.read_bytes, pr_stats.read_bytes, 0);
    ATOMIC_SWAP(&pr_stats.io_count, pr_stats.io_count, 0);

    thput_w = (double)write / (double)1048576;  // NOLINT
    thput_r = (double)read / (double)1048576;   // NOLINT
    thput   = thput_w + thput_r;

    if (pr_stats.user_write_bytes) {
        wa = (double)pr_stats.zns_write_bytes /
             (double)pr_stats.user_write_bytes;  // NOLINT
    } else {
        wa = 1;
    }

    xztl_prometheus_file_double("/tmp/ztl_prometheus_thput_w", thput_w);
    xztl_prometheus_file_double("/tmp/ztl_prometheus_thput_r", thput_r);
    xztl_prometheus_file_double("/tmp/ztl_prometheus_thput", thput);
    xztl_prometheus_file_int64("/tmp/ztl_prometheus_iops", io);
    xztl_prometheus_file_double("/tmp/ztl_prometheus_wamp_ztl", wa);
}

void *xztl_prometheus_flush(void *arg) {
    GET_MICROSECONDS(pr_stats.us_s, pr_stats.ts_s);

    xztl_flush_running++;
    while (xztl_flush_running) {
        usleep(1);
        GET_MICROSECONDS(pr_stats.us_e, pr_stats.ts_e);

        if (pr_stats.us_e - pr_stats.us_s >= 1000000) {
            xztl_prometheus_reset();
        }
    }

    while (pr_stats.us_e - pr_stats.us_s < 1000000) {
        GET_MICROSECONDS(pr_stats.us_e, pr_stats.ts_e);
    }
    xztl_prometheus_reset();

    return NULL;
}

void xztl_prometheus_add_io(struct xztl_io_mcmd *cmd) {
    uint32_t          nsec = 0, i;
    struct xztl_core *core;
    get_xztl_core(&core);
    for (i = 0; i < cmd->naddr; i++) nsec += cmd->nsec[i];

    switch (cmd->opcode) {
        case XZTL_ZONE_APPEND:
        case XZTL_CMD_WRITE:
            ATOMIC_ADD(&pr_stats.written_bytes, nsec * core->media->geo.nbytes);
            break;
        case XZTL_CMD_READ:
            ATOMIC_ADD(&pr_stats.read_bytes, nsec * core->media->geo.nbytes);
            break;
        default:
            return;
    }

    ATOMIC_ADD(&pr_stats.io_count, 1);
}

void xztl_prometheus_add_wa(uint64_t user_writes, uint64_t zns_writes) {
    ATOMIC_SWAP(&pr_stats.user_write_bytes, pr_stats.user_write_bytes, user_writes);
    ATOMIC_SWAP(&pr_stats.zns_write_bytes, pr_stats.zns_write_bytes, zns_writes);
}

static void xztl_prometheus_flush_latency(void) {
    FILE                 *fp;
    int                   dequeued = 0;
    struct latency_entry *ent;
    uint64_t              lat;

    GET_MICROSECONDS(pr_stats.us_l_s, pr_stats.ts_l_s);

    fp = fopen("/tmp/ztl_prometheus_read_lat", "w");
    if (fp) {
        while (!STAILQ_EMPTY(&lat_head) || dequeued < MAX_LATENCY_ENTS) {
            pthread_spin_lock(&lat_spin);

            ent = STAILQ_FIRST(&lat_head);
            if (ent) {
                lat = ent->usec;
                STAILQ_REMOVE_HEAD(&lat_head, entry);
                xztl_mempool_put(ent->mp_entry, XZTL_PROMETHEUS_LAT, 0);

                pthread_spin_unlock(&lat_spin);

                fprintf(fp, "%lu\n", lat);
            } else {
                pthread_spin_unlock(&lat_spin);
            }
            dequeued++;
        }
        fclose(fp);
    }
}

void *xztl_prometheus_latency_th(void *arg) {
    GET_MICROSECONDS(pr_stats.us_l_s, pr_stats.ts_l_s);

    xztl_flush_l_running++;
    while (xztl_flush_l_running) {
        usleep(1);
        GET_MICROSECONDS(pr_stats.us_l_e, pr_stats.ts_l_e);

        if ((xztl_mempool_left(XZTL_PROMETHEUS_LAT, 0) < 512) ||
            (pr_stats.us_l_e - pr_stats.us_l_s >= 1000000)) {
            xztl_prometheus_flush_latency();
        }
    }

    xztl_prometheus_flush_latency();

    return NULL;
}

void xztl_prometheus_add_read_latency(uint64_t usec) {
    struct xztl_mp_entry *mp_ent;
    struct latency_entry *ent;

    pthread_spin_lock(&lat_spin);

    /* Discard latency if queue is full */
    if (xztl_mempool_left(XZTL_PROMETHEUS_LAT, 0) == 0) {
        log_err(
            "xztl_prometheus_add_read_latency: xztl_mempool_left "
            "XZTL_PROMETHEUS_LAT 0.\n");
        pthread_spin_unlock(&lat_spin);
        return;
    }
    mp_ent = xztl_mempool_get(XZTL_PROMETHEUS_LAT, 0);
    if (!mp_ent) {
        log_err(
            "xztl_prometheus_add_read_latency: xztl_mempool_get "
            "XZTL_PROMETHEUS_LAT is NULL.\n");
        pthread_spin_unlock(&lat_spin);
        return;
    }
    ent           = (struct latency_entry *)mp_ent->opaque;
    ent->mp_entry = mp_ent;
    ent->usec     = usec;

    STAILQ_INSERT_TAIL(&lat_head, ent, entry);

    pthread_spin_unlock(&lat_spin);
}

void xztl_prometheus_exit(void) {
    xztl_flush_running   = 0;
    xztl_flush_l_running = 0;
    pthread_join(th_flush, NULL);
    pthread_join(latency_tid, NULL);
    xztl_mempool_destroy(XZTL_PROMETHEUS_LAT, 0);
    pthread_spin_destroy(&lat_spin);
}

int xztl_prometheus_init(void) {
    int ret;

    memset(&pr_stats, 0, sizeof(struct xztl_prometheus_stats));

    /* Create layency memory pool and queue */
    STAILQ_INIT(&lat_head);

    if (pthread_spin_init(&lat_spin, 0))
        return XZTL_ZTL_PROMETHEUS_ERR;

    ret = xztl_mempool_create(XZTL_PROMETHEUS_LAT, 0, MAX_LATENCY_ENTS,
                              sizeof(struct latency_entry), NULL, NULL);
    if (ret) {
        log_err("xztl_prometheus_init: Latency memory pool not started.");
        goto SPIN;
    }

    xztl_flush_l_running = 0;
    if (pthread_create(&latency_tid, NULL, xztl_prometheus_latency_th, NULL)) {
        log_err("xztl_prometheus_init: Flushing latency thread not started.");
        goto MP;
    }

    xztl_flush_running = 0;
    if (pthread_create(&th_flush, NULL, xztl_prometheus_flush, NULL)) {
        log_err("xztl_prometheus_init: Flushing thread not started.");
        goto LAT_TH;
    }

    while (!xztl_flush_running || !xztl_flush_l_running) {
    }

    return XZTL_OK;

LAT_TH:
    while (!xztl_flush_l_running) {
    }
    xztl_flush_l_running = 0;
    pthread_join(th_flush, NULL);
MP:
    xztl_mempool_destroy(XZTL_PROMETHEUS_LAT, 0);
SPIN:
    pthread_spin_destroy(&lat_spin);
    return XZTL_ZTL_PROMETHEUS_ERR;
}
