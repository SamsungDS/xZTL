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

#include <libxnvme.h>
#include <libxnvme_znd.h>
#include <libzrocks.h>
#include <omp.h>
#include <pthread.h>
#include <stdlib.h>
#include <xztl-media.h>
#include <xztl-mempool.h>
#include <xztl.h>
#include <xztl-mods.h>
#include <xztl-pro.h>
#include <xztl-stats.h>

#define ZROCKS_DEBUG       0
#define ZROCKS_BUF_ENTS    1024
#define ZROCKS_MAX_READ_SZ (256 * ZNS_ALIGMENT) /* 512 KB */

extern struct znd_media zndmedia;
#define ZROCKS_READ_MAX_RETRY 3

void *zrocks_alloc(size_t size) {
    return xztl_media_dma_alloc(size);
}

void zrocks_free(void *ptr) {
    xztl_media_dma_free(ptr);
}

int zrocks_new(uint64_t id, void *buf, size_t size, uint16_t level) {
    // struct xztl_io_ucmd ucmd;
    // int                 ret;

    if (ZROCKS_DEBUG)
        log_infoa("zrocks_new: ID [%lu], level [%d], size [%lu]\n", id, level,
                  size);

    // ucmd.app_md = 0;
    // ret = __zrocks_write(&ucmd, id, buf, size, level);

    // return (!ret) ? ucmd.status : ret;
    return XZTL_OK;
}

int zrocks_write(void *buf, size_t size, int level, struct zrocks_map maps[],
                 uint16_t *pieces) {
    struct xztl_io_ucmd ucmd;
    uint32_t            misalign;
    size_t              new_sz, alignment;
    int                 i;

    if (level < 0) {
        level = 0;
    }

    /* Align with ZTL_IO_SEC_MCMD*/
    alignment = ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;
    misalign  = size % alignment;
    new_sz    = (misalign != 0) ? size + (alignment - misalign) : size;
    level     = (level >= ZROCKS_LEVEL_NUM) ? (ZROCKS_LEVEL_NUM - 1) : level;

    if (ZROCKS_DEBUG)
        log_infoa(
            "zrocks_write: level [%d], size [%lu], new size [%lu], aligment "
            "[%lu], misalign [%d]\n",
            level, size, new_sz, alignment, misalign);

    ucmd.app_md    = 1;
    ucmd.prov_type = level;
    ucmd.id        = XZTL_CMD_WRITE;
    ucmd.buf       = buf;
    ucmd.size      = new_sz;
    ucmd.status    = 0;
    ucmd.completed = 0;
    ucmd.callback  = NULL;
    ucmd.prov      = NULL;
    ucmd.pieces    = 0;

    ztl()->io->submit_fn(&ucmd);

    /* Wait for asynchronous command */
    while (!ucmd.completed) {
        usleep(1);
    }

    for (i = 0; i < ucmd.pieces; i++) {
        maps[i].g.node_id = ucmd.node_id[i];
        maps[i].g.start   = ucmd.start[i];
        maps[i].g.num     = ucmd.num[i];
    }

    *pieces = ucmd.pieces;

    xztl_stats_inc(XZTL_STATS_APPEND_BYTES_U, size);
    xztl_stats_inc(XZTL_STATS_APPEND_BYTES, new_sz);
    xztl_stats_inc(XZTL_STATS_APPEND_UCMD, 1);

    return XZTL_OK;
}

int zrocks_read_obj(uint64_t id, uint64_t offset, void *buf, size_t size) {
    uint64_t objsec_off;

    if (ZROCKS_DEBUG)
        log_infoa("zrocks_read_obj: ID [%lu], off [%lu], size [%lu]\n", id,
                  offset, size);

    /* This assumes a single zone offset per object */
    objsec_off = ztl()->map->read_fn(id);

    if (ZROCKS_DEBUG)
        log_infoa("zrocks_read_obj: objsec_off [%lx], userbytes_off [%lu]\n",
                  objsec_off, offset);

    return XZTL_OK;
}

int zrocks_read(uint32_t node_id, uint64_t offset, void *buf, uint64_t size) {
    int                 ret;
    struct xztl_io_ucmd ucmd;

    ucmd.id         = XZTL_CMD_READ;
    ucmd.buf        = buf;
    ucmd.size       = size;
    ucmd.offset     = offset;
    ucmd.status     = 0;
    ucmd.callback   = NULL;
    ucmd.prov       = NULL;
    ucmd.completed  = 0;
    ucmd.ncb        = 0;
    ucmd.node_id[0] = node_id;

    if (ZROCKS_DEBUG)
        log_infoa("zrocks_read: node [%d] off [%lu], size [%lu],\n", node_id,
                  offset, size);

    int retry = 0;
READ_FAIL:
    ret = ztl()->io->read_fn(&ucmd);
    if (ret || ucmd.status) {
        log_erra(
            "zrocks_read: read_fn failed. node [%d] off [%lu], sz [%lu] ret "
            "[%d] status[%d]\n",
            node_id, offset, size, ret, ucmd.status);
        if (ucmd.status == 22) {
            usleep(1);
            retry = 0;
        }

        retry++;
        if (retry < ZROCKS_READ_MAX_RETRY) {
            goto READ_FAIL;
        }
        return XZTL_ZROCKS_READ_ERR;
    }

    /* Wait for asynchronous command */
    while (!ucmd.completed) {
        usleep(1);
    }

    xztl_stats_inc(XZTL_STATS_READ_BYTES_U, size);
    xztl_stats_inc(XZTL_STATS_READ_UCMD, 1);

    return XZTL_OK;
}

int zrocks_delete(uint64_t id) {
    uint64_t old;

    return ztl()->map->upsert_fn(id, 0, &old, 0);
}

int zrocks_trim(struct zrocks_map *map) {
    struct app_group        *grp      = ztl()->groups.get_fn(0);
    struct ztl_pro_node_grp *node_grp = grp->pro;
    struct ztl_pro_node     *node =
        (struct ztl_pro_node *)(&node_grp->vnodes[map->g.node_id]);
    int ret = XZTL_OK;

    if (ZROCKS_DEBUG)
        log_infoa("zrocks_trim: node ID [%u]\n", node->id);

    ATOMIC_SUB(&node->nr_valid, map->g.num);
    if (ZROCKS_DEBUG)
        log_infoa("zrocks_trim: node ID [%u] contain invalid [%lu]\n", node->id,
                  node->nr_valid);

    if (node->nr_valid == 0 && node->status == XZTL_ZMD_NODE_FULL) {
        log_infoa("zrocks_trim: node ID [%u] need to reset\n", node->id);
        ret = ztl()->mgmt->reset_fn(grp, node, ZTL_MGMG_RESET_ZONE);
        if (ret) {
            log_erra("zrocks_trim: node ID [%u], ret [%d]\n", node->id, ret);
        }
    }

    return ret;
}

void zrocks_node_set(int32_t node_id, int32_t level, int32_t num) {
    ztl()->io->nodeset_fn(node_id, level, num);
}

int zrocks_exit(void) {
    xztl_mempool_destroy(ZROCKS_MEMORY, 0);
    return xztl_exit();
}

int zrocks_node_finish(uint32_t node_id) {
    struct app_group        *grp      = ztl()->groups.get_fn(0);
    struct ztl_pro_node_grp *node_grp = grp->pro;
    struct ztl_pro_node     *node =
        (struct ztl_pro_node *)(&node_grp->vnodes[node_id]);
    int ret;

    ret = ztl()->mgmt->finish_fn(grp, node, ZTL_MGMG_FULL_ZONE);
    if (ret) {
        log_erra("zrocks_node_finish: err node ID [%u], ret [%d]\n", node_id,
                 ret);
    }

    return ret;
}

void zrocks_clear_invalid_nodes() {
    struct app_group *grp = ztl()->groups.get_fn(0);
    ztl()->mgmt->clear_fn(grp);
}

int zrocks_init(const char *dev_name) {
    int ret;

    /* Add libznd media layer */
    xztl_add_media(znd_media_register);

    /* Add the ZTL modules */
    ztl_zmd_register();
    ztl_pro_register();
    ztl_mpe_register();
    ztl_map_register();
    // ztl_thd_register();
    ztl_io_register();
    ztl_mgmt_register();

    ret = xztl_init(dev_name);
    if (ret) {
        log_erra("zrocks_init: err xztl_init dev [%s] ret [%d]\n", dev_name,
                 ret);
        return XZTL_ZROCKS_INIT_ERR;
    }

    ret = xztl_mempool_create(ZROCKS_MEMORY, 0, ZROCKS_BUF_ENTS,
                              ZROCKS_MAX_READ_SZ, zrocks_alloc, zrocks_free);
    if (ret) {
        log_erra("zrocks_init: err xztl_mempool_create failed, ret [%d]\n",
                 ret);
        xztl_exit();
    }

    return ret;
}
