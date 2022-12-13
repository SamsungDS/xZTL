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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xztl.h>
#include <xztl-stats.h>

#define XZTL_STATS_IO_TYPES 18

struct xztl_stats_data {
    uint64_t io[XZTL_STATS_IO_TYPES];
};

struct xztl_node_stats_data {
    uint64_t data[ZROCKS_LEVEL_NUM][XZTL_CMD_ENTRY_MAX];
};

static struct xztl_stats_data xztl_stats;
static struct xztl_node_stats_data xztl_node_stats;

void xztl_stats_print_err(void) {
    log_infoa("write submit failed times [%lu]\n",
              xztl_stats.io[XZTL_STATS_WRITE_SUBMIT_FAIL]);
    log_infoa("read submit failed times [%lu]\n",
              xztl_stats.io[XZTL_STATS_READ_SUBMIT_FAIL]);
    log_infoa("write callback failed times [%lu]\n",
              xztl_stats.io[XZTL_STATS_WRITE_CALLBACK_FAIL]);
    log_infoa("read callback failed times [%lu]\n",
              xztl_stats.io[XZTL_STATS_READ_CALLBACK_FAIL]);
    log_infoa("mgmt failed times [%lu]\n", xztl_stats.io[XZTL_STATS_MGMT_FAIL]);
    log_infoa("write meta failed times [%lu]\n",
              xztl_stats.io[XZTL_STATS_META_WRITE_FAIL]);
    log_infoa("read meta failed times [%lu]\n",
              xztl_stats.io[XZTL_STATS_META_READ_FAIL]);
}

void xztl_stats_print_io(void) {
    uint64_t tot_b, tot_b_w, tot_b_r;
    double   wa;

    printf("\n User I/O commands\n");
    printf("   write  : %lu\n", xztl_stats.io[XZTL_STATS_APPEND_UCMD]);
    printf("   read   : %lu\n", xztl_stats.io[XZTL_STATS_READ_UCMD]);

    printf("\n Media I/O commands\n");
    printf("   append : %lu\n", xztl_stats.io[XZTL_STATS_APPEND_MCMD]);
    printf("   read   : %lu\n", xztl_stats.io[XZTL_STATS_READ_MCMD]);
    printf("   reset  : %lu\n", xztl_stats.io[XZTL_STATS_RESET_MCMD]);

    tot_b_r = xztl_stats.io[XZTL_STATS_READ_BYTES_U];
    tot_b_w = xztl_stats.io[XZTL_STATS_APPEND_BYTES_U];
    tot_b   = tot_b_w + tot_b_r;

    wa = tot_b_w;

    printf("\n Data transferred (Application->ZTL): %.2f MB (%lu bytes)\n",
           tot_b / (double)1048576, (uint64_t)tot_b);  // NOLINT
    printf("   data written     : %10.2lf MB (%lu bytes)\n",
           (double)tot_b_w / (double)1048576, (uint64_t)tot_b_w);  // NOLINT
    printf("   data read        : %10.2lf MB (%lu bytes)\n",
           (double)tot_b_r / (double)1048576, (uint64_t)tot_b_r);  // NOLINT

    tot_b_r = xztl_stats.io[XZTL_STATS_READ_BYTES];
    tot_b_w = xztl_stats.io[XZTL_STATS_APPEND_BYTES];
    tot_b   = tot_b_w + tot_b_r;

    wa = (double)tot_b_w / wa;  // NOLINT

    printf("\n Data transferred (ZTL->Media): %.2f MB (%lu bytes)\n",
           tot_b / (double)1048576, (uint64_t)tot_b);  // NOLINT
    printf("   data written     : %10.2lf MB (%lu bytes)\n",
           (double)tot_b_w / (double)1048576, (uint64_t)tot_b_w);  // NOLINT
    printf("   data read        : %10.2lf MB (%lu bytes)\n",
           (double)tot_b_r / (double)1048576, (uint64_t)tot_b_r);  // NOLINT

    printf("\n Write Amplification: %.6lf\n", wa);
}

void xztl_stats_print_io_simple(void) {
    uint64_t flush_w, app_w, padding_w;
    FILE    *fp;

    flush_w   = xztl_stats.io[XZTL_STATS_APPEND_BYTES];
    app_w     = xztl_stats.io[XZTL_STATS_APPEND_BYTES_U];
    padding_w = flush_w - app_w;

    printf("\nZTL Application Writes : %.2f MB (%lu bytes)\n",
           app_w / (double)1048576, app_w);  // NOLINT
    printf("ZTL Padding            : %.2f MB (%lu bytes)\n",
           padding_w / (double)1048576, padding_w);  // NOLINT
    printf("ZTL Total Writes       : %.2f MB (%lu bytes)\n",
           flush_w / (double)1048576, flush_w);  // NOLINT
    printf("ZTL write-amplification: %.6lf\n",
           (double)flush_w / (double)app_w);  // NOLINT
    for (int level = 0; level < ZROCKS_LEVEL_NUM; level++) {
        printf("\nLevel %d Entry Valid: %-8lu Entry Used : %-8lu Space Used Ratio : %.6lf",
            level, xztl_node_stats.data[level][XZTL_CMD_ENTRY_VALID],
            xztl_node_stats.data[level][XZTL_CMD_ENTRY_USED],
            xztl_node_stats.data[level][XZTL_CMD_ENTRY_USED] != 0 ?
            (xztl_node_stats.data[level][XZTL_CMD_ENTRY_VALID] * 1.0 /
            xztl_node_stats.data[level][XZTL_CMD_ENTRY_USED]) : 0.0);
    }
    printf(
        "\nRecycled Zones: %lu (%.2f MB, %lu bytes)\n",
        xztl_stats.io[XZTL_STATS_RECYCLED_ZONES],
        xztl_stats.io[XZTL_STATS_RECYCLED_BYTES] / (double)1048576,  // NOLINT
        xztl_stats.io[XZTL_STATS_RECYCLED_BYTES]);
    printf("Zone Resets   : %lu\n", xztl_stats.io[XZTL_STATS_RESET_MCMD]);
    printf("\n");

    fp = fopen("/tmp/ztl_written_bytes", "w+");
    if (fp) {
        fprintf(fp, "%lu", flush_w);
        fclose(fp);
    }
    fp = fopen("/tmp/app_written_bytes", "w+");
    if (fp) {
        fprintf(fp, "%lu", app_w);
        fclose(fp);
    }
}

void xztl_stats_add_io(struct xztl_io_mcmd *cmd) {
    uint32_t          nsec = 0, type_b, type_c, i;
    struct xztl_core *core;
    get_xztl_core(&core);
    for (i = 0; i < cmd->naddr; i++) nsec += cmd->nsec[i];

    switch (cmd->opcode) {
        case XZTL_ZONE_APPEND:
        case XZTL_CMD_WRITE:
            type_b = XZTL_STATS_APPEND_BYTES;
            type_c = XZTL_STATS_APPEND_MCMD;
            break;
        case XZTL_CMD_READ:
            type_b = XZTL_STATS_READ_BYTES;
            type_c = XZTL_STATS_READ_MCMD;
            break;
        default:
            return;
    }

    ATOMIC_ADD(&xztl_stats.io[type_c], 1);
    ATOMIC_ADD(&xztl_stats.io[type_b], nsec * core->media->geo.nbytes);

#if XZTL_PROMETHEUS
    /* Prometheus */
    xztl_prometheus_add_io(cmd);
#endif
}

void xztl_stats_inc(uint32_t type, uint64_t val) {
    ATOMIC_ADD(&xztl_stats.io[type], val);

#if XZTL_PROMETHEUS
    /* Prometheus */
    if (type == XZTL_STATS_APPEND_BYTES_U) {
        xztl_prometheus_add_wa(xztl_stats.io[XZTL_STATS_APPEND_BYTES_U],
                               xztl_stats.io[XZTL_STATS_APPEND_BYTES]);
    }
#endif
}

void xztl_stats_node_inc(int32_t level, uint32_t type, uint64_t val) {
    ATOMIC_ADD(&xztl_node_stats.data[level][type], val);
}
void xztl_stats_reset_io(void) {
    uint32_t type_i;

    for (type_i = 0; type_i < XZTL_STATS_IO_TYPES; type_i++)
        ATOMIC_SWAP(&xztl_stats.io[type_i], xztl_stats.io[type_i], 0);
}

void xztl_stats_exit(void) {
    xztl_stats_print_err();

#if XZTL_PROMETHEUS
    xztl_prometheus_exit();
#endif
}

int xztl_stats_init(void) {
    memset(xztl_stats.io, 0x0, sizeof(uint64_t) * XZTL_STATS_IO_TYPES);
    for (int i = 0; i < ZROCKS_LEVEL_NUM; i++) {
        memset(xztl_node_stats.data[i], 0x0,
               sizeof(uint64_t) * XZTL_CMD_ENTRY_MAX);
    }

#if XZTL_PROMETHEUS
    if (xztl_prometheus_init()) {
        log_err("xztl_stats_init: Prometheus not started.");
        return XZTL_ZTL_STATS_ERR;
    }
#endif

    return XZTL_OK;
}
