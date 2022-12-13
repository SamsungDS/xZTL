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

#ifndef XZTL_STATS_H
#define XZTL_STATS_H

#define _GNU_SOURCE

#include <stdint.h>

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

enum xztl_stats_node_types {
    XZTL_CMD_ENTRY_VALID,
    XZTL_CMD_ENTRY_USED,
    XZTL_CMD_ENTRY_MAX
};

/* Statistics */
int  xztl_stats_init(void);
void xztl_stats_exit(void);
void xztl_stats_add_io(struct xztl_io_mcmd *cmd);
void xztl_stats_inc(uint32_t type, uint64_t val);
void xztl_stats_node_inc(int32_t level, uint32_t type, uint64_t val);
void xztl_stats_print_io(void);
void xztl_stats_print_io_simple(void);

/* Prometheus */
int  xztl_prometheus_init(void);
void xztl_prometheus_exit(void);
void xztl_prometheus_add_io(struct xztl_io_mcmd *cmd);
void xztl_prometheus_add_wa(uint64_t user_writes, uint64_t zns_writes);
void xztl_prometheus_add_read_latency(uint64_t usec);

#endif /* XZTL_H */
