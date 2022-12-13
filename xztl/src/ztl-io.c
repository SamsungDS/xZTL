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

#include <sched.h>
#include <unistd.h>
#include <xztl-media.h>
#include <xztl.h>
#include <xztl-mods.h>
#include <xztl-pro.h>
#include <xztl-stats.h>

#define ZTL_MCMD_ENTS XZTL_IO_MAX_MCMD
#define ZROCKS_DEBUG  0
#define ZNS_ALIGMENT  4096

extern struct app_group **glist;
extern struct znd_media zndmedia;

struct ztl_queue_pool qp[ZROCKS_LEVEL_NUM];
struct ztl_read_rs    read_resource[XZTL_READ_RS_NUM];
pthread_mutex_t       rs_mutex;

static int _ztl_io_get_read_rs(void) {
    int id;
    int ret = -1;

    for (id = 0; id < zndmedia.read_ctx_num; id++) {
        if (!read_resource[id].usedflag) {
            read_resource[id].usedflag = true;
            ret                        = id;
            break;
        }
    }
    return ret;
}

static void _ztl_io_put_read_rs(int id) {
    read_resource[id].usedflag = false;
}

static void _ztl_io_write_rs_exit(struct ztl_queue_pool *q) {
    int mcmd_id;

    xztl_ctx_media_exit(q->tctx);
    xztl_media_dma_free(q->prov);

    for (mcmd_id = 0; mcmd_id < ZTL_IO_RC_NUM; mcmd_id++)
        free(q->mcmd[mcmd_id]);
}

static int _ztl_io_write_rs_init(struct ztl_queue_pool *q) {
    int mcmd_id;

    for (mcmd_id = 0; mcmd_id < ZTL_IO_RC_NUM; mcmd_id++) {
        q->mcmd[mcmd_id] = aligned_alloc(64, sizeof(struct xztl_io_mcmd));
    }

    q->prov = xztl_media_dma_alloc(sizeof(struct app_pro_addr));
    if (!q->prov) {
        log_err(
            "_ztl_io_rs_init: Thread resource (data buffer) allocation error.");
        return XZTL_ZTL_IO_ERR;
    }

    struct app_pro_addr *prov = (struct app_pro_addr *)q->prov;
    prov->grp                 = glist[0];

    q->tctx = xztl_ctx_media_init(zndmedia.io_depth);
    if (!q->tctx) {
        log_err("_ztl_thd_init: Thread resource (tctx) allocation error.");
        return XZTL_ZTL_IO_ERR;
    }

    q->node = NULL;

    return XZTL_OK;
}

static void _ztl_io_read_rs_exit(struct ztl_read_rs *r) {
    int mcmd_id;

    xztl_ctx_media_exit(r->tctx);
    for (mcmd_id = 0; mcmd_id < ZTL_IO_RC_NUM; mcmd_id++) {
        free(r->mcmd[mcmd_id]);
        xztl_media_dma_free(r->prp[mcmd_id]);
    }
}

static int _ztl_io_read_rs_init(struct ztl_read_rs *r) {
    uint64_t base_align_bytes = ZNS_ALIGMENT * ZTL_IO_SEC_MCMD;
    int      mcmd_id;

    for (mcmd_id = 0; mcmd_id < ZTL_IO_RC_NUM; mcmd_id++) {
        r->mcmd[mcmd_id] = aligned_alloc(64, sizeof(struct xztl_io_mcmd));
        r->prp[mcmd_id]  = xztl_media_dma_alloc(base_align_bytes);
    }

    r->tctx = xztl_ctx_media_init(zndmedia.io_depth);
    if (!r->tctx) {
        log_err("_ztl_thd_init: Thread resource (tctx) allocation error.");
        return XZTL_ZTL_IO_ERR;
    }

    r->usedflag = false;

    return XZTL_OK;
}

static void ztl_io_read_callback_mcmd(void *arg) {
    struct xztl_io_ucmd *ucmd;
    struct xztl_io_mcmd *mcmd;
    uint32_t             misalign;

    mcmd = (struct xztl_io_mcmd *)arg;
    ucmd = (struct xztl_io_ucmd *)mcmd->opaque;

    if (mcmd->status) {
        xztl_stats_inc(XZTL_STATS_READ_CALLBACK_FAIL, 1);
        log_erra(
            "ztl_io_read_callback_mcmd: Callback. ID [%lu], S [%d/%d], C %d, "
            "WOFF [0x%lx]. St [%d]\n",
            ucmd->id, mcmd->sequence, ucmd->nmcmd, ucmd->ncb,
            ucmd->moffset[mcmd->sequence], mcmd->status);

        mcmd->callback_err_cnt++;
        if (mcmd->callback_err_cnt < MAX_CALLBACK_ERR_CNT) {
            int ret = xztl_media_submit_io(mcmd);
            if (ret) {
                xztl_stats_inc(XZTL_STATS_READ_SUBMIT_FAIL, 1);
                log_erra(
                    "ztl_io_read_callback_mcmd: submit_io. ID [%lu], S "
                    "[%d/%d], C %d, WOFF [0x%lx]. ret [%d]\n",
                    ucmd->id, mcmd->sequence, ucmd->nmcmd, ucmd->ncb,
                    ucmd->moffset[mcmd->sequence], ret);
            }
            return;
        }

        ucmd->status = mcmd->status;
    } else {
        /* If I/O succeeded, we copy the data from the correct offset to the
         * user */
        misalign = mcmd->sequence;  // temp
        memcpy((char *)ucmd->buf + mcmd->buf_off,
               (char *)mcmd->prp[0] + misalign,
               mcmd->cpsize);  // NOLINT
    }
}

static void ztl_io_write_callback_mcmd(void *arg) {
    struct xztl_io_ucmd  *ucmd;
    struct xztl_io_mcmd  *mcmd;
    int                   ret;

    mcmd = (struct xztl_io_mcmd *)arg;
    ucmd = (struct xztl_io_ucmd *)mcmd->opaque;

    if (mcmd->status) {
        xztl_stats_inc(XZTL_STATS_WRITE_CALLBACK_FAIL, 1);
        log_erra(
            "ztl_io_write_callback_mcmd: Callback. ID [%lu], S [%d/%d], C "
            "[%d], WOFF [0x%lx]. St [%d]\n",
            ucmd->id, mcmd->sequence, ucmd->nmcmd, ucmd->ncb,
            ucmd->moffset[mcmd->sequence], mcmd->status);
        mcmd->callback_err_cnt++;
        if (mcmd->callback_err_cnt < MAX_CALLBACK_ERR_CNT) {
            ret = xztl_media_submit_io(mcmd);
            if (ret) {
                xztl_stats_inc(XZTL_STATS_WRITE_SUBMIT_FAIL, 1);
                log_erra(
                    "ztl_io_write_callback_mcmd: submit ID [%lu], S [%d/%d], C "
                    "%d, WOFF [0x%lx]. ret [%d]\n",
                    ucmd->id, mcmd->sequence, ucmd->nmcmd, ucmd->ncb,
                    ucmd->moffset[mcmd->sequence], ret);
            }
            return;
        }
        ucmd->status = mcmd->status;
    } else {
        ucmd->moffset[mcmd->sequence] = mcmd->paddr[0];
    }

    ucmd->minflight[mcmd->sequence_zn] = 0;
    ATOMIC_ADD(&ucmd->ncb, 1)

    if (ucmd->ncb == ucmd->nmcmd) {
        ztl()->pro->free_fn(ucmd->prov);
    }
}

static void ztl_io_poke_ctx(struct xztl_mthread_ctx *tctx) {
    struct xztl_misc_cmd misc;
    misc.opcode         = XZTL_MISC_ASYNCH_POKE;
    misc.asynch.ctx_ptr = tctx;
    misc.asynch.limit   = 1;
    misc.asynch.count   = 0;

    if (!xztl_media_submit_misc(&misc)) {
        if (!misc.asynch.count) {
            // We may check outstanding commands here
        }
    }
}

int ztl_io_read_ucmd(struct xztl_io_ucmd *ucmd, struct ztl_read_rs *r) {
    struct xztl_io_mcmd *mcmd;
    uint32_t             node_id = ucmd->node_id[0];

    struct app_group        *grp = glist[0];
    struct ztl_pro_node_grp *pro = grp->pro;
    struct ztl_pro_node *znode = (struct ztl_pro_node *)(&pro->vnodes[node_id]);

    size_t size = ucmd->size;

    uint64_t misalign, sec_size, sec_start, zindex, zone_sec_off, read_num;
    uint64_t sec_left, bytes_off, left;
    int      nlevel, cmd_i, total_cmd, submitted;
    int      ret = 0;

    uint64_t offset = ucmd->offset;
    misalign        = offset % ZNS_ALIGMENT;
    sec_size        = (size + misalign) / ZNS_ALIGMENT +
               (((size + misalign) % ZNS_ALIGMENT) ? 1 : 0);
    sec_start      = offset / ZNS_ALIGMENT;
    int level_secs = ZTL_PRO_ZONE_NUM_INNODE * ZTL_IO_SEC_MCMD;

    /* ??????(8 * 16=128) (0~127 0lun 128~255 1lun ...)*/
    nlevel = sec_start / level_secs;

    /* ?nlevel + 1?????16*/
    zindex = (sec_start % level_secs) / ZTL_IO_SEC_MCMD;

    /* ?????zone offset???????(64KB) */
    zone_sec_off =
        (nlevel * ZTL_IO_SEC_MCMD) + (sec_start % level_secs) % ZTL_IO_SEC_MCMD;
    read_num = ZTL_IO_SEC_MCMD - (sec_start % level_secs) % ZTL_IO_SEC_MCMD;
    read_num = (sec_size > read_num) ? read_num : sec_size;

    if (ZROCKS_DEBUG)
        log_infoa("zrocks (__read): sec_size %lu\n", sec_size);

    sec_left  = sec_size;
    bytes_off = 0;
    left      = size;
    total_cmd = 0;

    while (sec_left) {
        mcmd = r->mcmd[total_cmd];
        memset(mcmd, 0x0, sizeof(struct xztl_io_mcmd));

        mcmd->opcode       = XZTL_CMD_READ;
        mcmd->naddr        = 1;
        mcmd->synch        = 0;
        mcmd->async_ctx    = r->tctx;
        mcmd->addr[0].addr = 0;
        mcmd->nsec[0]      = read_num;
        sec_left -= mcmd->nsec[0];
        mcmd->callback_err_cnt = 0;
        mcmd->prp[0]           = (uint64_t)r->prp[total_cmd];

        mcmd->addr[0].g.sect =
            znode->vzones[zindex]->addr.g.sect + zone_sec_off;
        mcmd->status   = 0;
        mcmd->callback = ztl_io_read_callback_mcmd;

        mcmd->sequence = misalign;   // tmp prp offset
        mcmd->buf_off  = bytes_off;  // temp
        mcmd->cpsize =
            (mcmd->nsec[0] * ZNS_ALIGMENT) - misalign > left
                ? left
                : ((mcmd->nsec[0] * ZNS_ALIGMENT) - misalign);  // copy size
        left -= mcmd->cpsize;

        mcmd->opaque          = ucmd;
        mcmd->submitted       = 0;
        ucmd->mcmd[total_cmd] = mcmd;

        total_cmd++;
        if (sec_left == 0)
            break;

        zindex = (zindex + 1) % ZTL_PRO_ZONE_NUM_INNODE;
        if (zindex == 0)
            nlevel++;

        zone_sec_off = nlevel * ZTL_IO_SEC_MCMD;
        read_num = (sec_left > ZTL_IO_SEC_MCMD) ? ZTL_IO_SEC_MCMD : sec_left;
        bytes_off += mcmd->cpsize;
        misalign = 0;
    }

    if (ZROCKS_DEBUG)
        log_infoa("ztl_io_read_ucmd: total mcmd [%u]\n", total_cmd);

    if (total_cmd == 1) {
        ucmd->mcmd[0]->synch = 1;
        ret                  = xztl_media_submit_io(ucmd->mcmd[0]);

        misalign = ucmd->mcmd[0]->sequence;
        memcpy(ucmd->buf,
               (char *)(ucmd->mcmd[0]->prp[0] + misalign), /* NOLINT */
               ucmd->mcmd[0]->cpsize);

        ucmd->completed = 1;
        return ret;
    }

    submitted   = 0;
    ucmd->nmcmd = total_cmd;
    while (submitted < total_cmd) {
        for (cmd_i = 0; cmd_i < total_cmd; cmd_i++) {
            if (ucmd->mcmd[cmd_i]->submitted)
                continue;

            ret = xztl_media_submit_io(ucmd->mcmd[cmd_i]);
            if (ret) {
                xztl_stats_inc(XZTL_STATS_READ_SUBMIT_FAIL, 1);
                log_erra("ztl_wca_read_ucmd: xztl_media_submit_io err [%d]\n",
                         ret);
                continue;
            }

            ucmd->mcmd[cmd_i]->submitted = 1;
            submitted++;
        }
    }

    int err = xnvme_queue_drain(r->tctx->queue);
    if (err < 0) {
        log_erra("ztl_io_read_ucmd: xnvme_queue_drain() returns error [%d]\n",
                 err);
    }

    ucmd->completed = 1;
    return ret;
}

int ztl_io_read(struct xztl_io_ucmd *ucmd) {
    int id, ret;

    pthread_mutex_lock(&rs_mutex);
    id = _ztl_io_get_read_rs();
    if (id < 0) {
        log_err("_ztl_io_get_read_rs err.\n");
        pthread_mutex_unlock(&rs_mutex);
        return XZTL_ZTL_IO_ERR;
    }
    pthread_mutex_unlock(&rs_mutex);

    ret = ztl_io_read_ucmd(ucmd, &read_resource[id]);
    _ztl_io_put_read_rs(id);
    return ret;
}

int ztl_io_write_ucmd(struct xztl_io_ucmd *ucmd) {
    struct ztl_queue_pool *q;
    struct app_pro_addr   *prov;
    struct xztl_io_mcmd   *mcmd;
    struct xztl_core      *core;
    get_xztl_core(&core);
    uint32_t nsec, ncmd, submitted, zn_i;
    uint64_t boff;
    int      ret, num, left;
    int      i, cmd_i;

    ZDEBUG(ZDEBUG_IO, "ztl_io_write_ucmd: Processing user write. ID [%lu]",
           ucmd->id);

    nsec = ucmd->size / core->media->geo.nbytes;

    /* We do not support non-aligned buffers */
    if (ucmd->size % (core->media->geo.nbytes * ZTL_IO_SEC_MCMD) != 0) {
        log_erra(
            "ztl_io_write_ucmd: Buffer is not aligned to [%d] bytes [%lu] "
            "bytes.",
            core->media->geo.nbytes * ZTL_IO_SEC_MCMD, ucmd->size);
        goto FAILURE;
    }

    /* First we check the number of commands based on ZTL_IO_SEC_MCMD */
    ncmd = nsec / ZTL_IO_SEC_MCMD;
    if (ncmd > XZTL_IO_MAX_MCMD) {
        log_erra(
            "ztl_io_write_ucmd: User command exceed XZTL_IO_MAX_MCMD. "
            "[%d] of [%d]",
            ncmd, XZTL_IO_MAX_MCMD);
        goto FAILURE;
    }
    ZDEBUG(ZDEBUG_IO, "ztl_io_write_ucmd: NMCMD [%d]", ncmd);

    q           = &qp[ucmd->prov_type];
    prov        = q->prov;
    prov->naddr = 0;

    ucmd->prov      = prov;
    ucmd->nmcmd     = ncmd;
    ucmd->completed = 0;
    ucmd->ncb       = 0;

    boff = (uint64_t)ucmd->buf;

    left = ncmd;
reget:
    ret = ztl()->pro->get_node_fn(q);
    if (ret) {
        log_erra("_ztl_io_get_prov: Get node failed [%d].", ret);
        goto FAILURE;
    }
    num = (q->node->optimal_write_sec_left <= left)
              ? q->node->optimal_write_sec_left
              : left;
    if (q->node->level == -1) {
        q->node->level = ucmd->prov_type;
    }

    /* Record mapping tables */
    ucmd->node_id[ucmd->pieces] = q->node->id;
    ucmd->start[ucmd->pieces]   = q->node->optimal_write_sec_used;
    ucmd->num[ucmd->pieces]     = num;

    /* Alloc zone addr*/
    ret = ztl()->pro->new_fn(num, q->node->id, prov,
                             q->node->optimal_write_sec_used);
    if (ret) {
        log_erra("_ztl_io_get_prov: Alloc zone addr failed [%d].", ret);
        goto FAIL_NCMD;
    }

    /* Populate media commands */
    int zn_cmd_id[ZTL_PRO_ZONE_NUM_INNODE * 2][2000] = {{-1}};
    int zn_cmd_id_num[ZTL_PRO_ZONE_NUM_INNODE * 2]   = {0};
    int zn_cmd_id_index[ZTL_PRO_ZONE_NUM_INNODE * 2] = {0};
    int zone_sector_num[ZTL_PRO_ZONE_NUM_INNODE * 2] = {0};

    for (i = 0; i < prov->naddr; i++) {
        zone_sector_num[i] = prov->nsec[i];
    }

    cmd_i = 0;
    while (cmd_i < num) {
        for (zn_i = 0; zn_i < prov->naddr; zn_i++) {
            if (zone_sector_num[zn_i] <= 0) {
                continue;
            }

            mcmd = q->mcmd[cmd_i];
            mcmd->opcode =
                (XZTL_WRITE_APPEND) ? XZTL_ZONE_APPEND : XZTL_CMD_WRITE;
            mcmd->synch            = 0;
            mcmd->submitted        = 0;
            mcmd->sequence         = cmd_i;
            mcmd->sequence_zn      = zn_i;
            mcmd->naddr            = 1;
            mcmd->status           = 0;
            mcmd->callback_err_cnt = 0;
            mcmd->nsec[0]          = ZTL_IO_SEC_MCMD;

            mcmd->addr[0].addr   = 0;
            mcmd->addr[0].g.grp  = prov->addr[zn_i].g.grp;
            mcmd->addr[0].g.zone = prov->addr[zn_i].g.zone;
            if (!XZTL_WRITE_APPEND) {
                mcmd->addr[0].g.sect = (uint64_t)prov->addr[zn_i].g.sect;
            }

            zone_sector_num[zn_i] -= mcmd->nsec[0];
            prov->addr[zn_i].g.sect += mcmd->nsec[0];

            ucmd->msec[cmd_i] = mcmd->nsec[0];
            mcmd->prp[0]      = boff;
            boff += core->media->geo.nbytes * mcmd->nsec[0];

            mcmd->callback  = ztl_io_write_callback_mcmd;
            mcmd->opaque    = ucmd;
            mcmd->async_ctx = q->tctx;

            ucmd->mcmd[cmd_i]                      = mcmd;
            ucmd->mcmd[cmd_i]->submitted           = 0;
            zn_cmd_id[zn_i][zn_cmd_id_num[zn_i]++] = cmd_i;
            cmd_i++;
        }
    }

    left = left - num;
    ucmd->pieces++;

    ZDEBUG(ZDEBUG_IO, "ztl_io_write_ucmd: Populated: %d", cmd_i);

    /* Submit media commands */
    for (cmd_i = 0; cmd_i < ZTL_PRO_ZONE_NUM_INNODE * 2; cmd_i++)
        ucmd->minflight[cmd_i] = 0;

    submitted   = 0;
    ucmd->nmcmd = num;
    ucmd->ncb   = 0;

    while (submitted < num) {
        for (zn_i = 0; zn_i < prov->naddr; zn_i++) {
            int index = zn_cmd_id_index[zn_i];
            int cmd_id_num = zn_cmd_id_num[zn_i];

            if (index >= cmd_id_num) {
                continue;
            }

            /* Limit to 1 write per zone if append is not supported */
            if (!XZTL_WRITE_APPEND) {
                if (ucmd->minflight[zn_i]) {
                    ztl_io_poke_ctx(q->tctx);
                    continue;
                }

                ucmd->minflight[zn_i] = 1;
            }

            ret = xztl_media_submit_io(ucmd->mcmd[zn_cmd_id[zn_i][index]]);
            if (ret) {
                ZDEBUG(ZDEBUG_IO, " XZTL_STATS_WRITE_SUBMIT_FAIL [%d]", zn_i);
                xztl_stats_inc(XZTL_STATS_WRITE_SUBMIT_FAIL, 1);
                ztl_io_poke_ctx(q->tctx);
                zn_i--;
                continue;
            }

            ucmd->mcmd[zn_cmd_id[zn_i][index]]->submitted = 1;
            submitted++;
            zn_cmd_id_index[zn_i]++;

            if (submitted % 8 == 0)
                ztl_io_poke_ctx(q->tctx);
        }
        usleep(1);
    }

    /* Poke the context for completions */
    while (ucmd->ncb < ucmd->nmcmd) {
        ztl_io_poke_ctx(q->tctx);
    }

    q->node->optimal_write_sec_left -= num;
    q->node->optimal_write_sec_used += num;

    ATOMIC_ADD(&q->node->nr_valid, num);
    if (q->node->optimal_write_sec_left == 0) {
        q->node->status = XZTL_ZMD_NODE_FULL;
    }

    if (left > 0) {
        goto reget;
    }

    ZDEBUG(ZDEBUG_IO, " ztl_io_write_ucmd: Submitted [%d]", submitted);
    ucmd->completed = 1;
    return XZTL_OK;

    /* If we get a submit failure but previous I/Os have been
     * submitted, we fail all subsequent I/Os and completion is
     * performed by the callback function */

FAIL_NCMD:
    for (i = 0; i < prov->naddr; i++) prov->nsec[i] = 0;

    ztl()->pro->free_fn(prov);

FAILURE:
    ucmd->status    = XZTL_ZTL_IO_S_ERR;
    ucmd->completed = 1;
    return XZTL_ZTL_IO_S_ERR;
}

static void *ztl_io_write_th(void *arg) {
    struct xztl_io_ucmd   *ucmd = NULL;
    struct ztl_queue_pool *q    = (struct ztl_queue_pool *)arg;

    q->flag_running = 1;

    while (q->flag_running) {
    NEXT:
        if (!STAILQ_EMPTY(&q->ucmd_head)) {
            pthread_spin_lock(&q->ucmd_spin);
            ucmd = STAILQ_FIRST(&q->ucmd_head);
            if (ucmd == NULL) {
                pthread_spin_unlock(&q->ucmd_spin);
                goto NEXT;
            }
            STAILQ_REMOVE_HEAD(&q->ucmd_head, entry);
            pthread_spin_unlock(&q->ucmd_spin);

            ztl_io_write_ucmd(ucmd);

            goto NEXT;
        }
    }

    return NULL;
}

static void ztl_io_submit(struct xztl_io_ucmd *ucmd) {
    struct ztl_queue_pool *q = &qp[ucmd->prov_type];

    pthread_spin_lock(&q->ucmd_spin);
    STAILQ_INSERT_TAIL(&q->ucmd_head, ucmd, entry);
    pthread_spin_unlock(&q->ucmd_spin);
}

static int _ztl_io_w_queue_init(int level) {
    struct ztl_queue_pool *q = &qp[level];

    STAILQ_INIT(&q->ucmd_head);

    /* Resource pre-alloc */
    if (_ztl_io_write_rs_init(q)) {
        log_err("_ztl_io_queue_init: Thread resource  allocation error.");
        return XZTL_ZTL_IO_ERR;
    }

    if (pthread_spin_init(&q->ucmd_spin, 0))
        goto RC;

    if (pthread_create(&q->w_thread, NULL, ztl_io_write_th, (void *)q))
        goto SPIN;

    return XZTL_OK;

SPIN:
    pthread_spin_destroy(&q->ucmd_spin);
RC:
    _ztl_io_write_rs_exit(q);

    return XZTL_ZTL_IO_ERR;
}

static void ztl_io_nodeset(int32_t node_id, int32_t level, int32_t nr_valid) {
    struct app_group        *grp = glist[0];
    struct ztl_pro_node_grp *pro = grp->pro;
    struct ztl_pro_node *znode = (struct ztl_pro_node *)(&pro->vnodes[node_id]);
    znode->nr_valid += nr_valid;
    if (znode->status == XZTL_ZMD_NODE_USED) {
        level = (level >= ZROCKS_LEVEL_NUM) ? (ZROCKS_LEVEL_NUM - 1) : level;
        qp[level].node = znode;
        znode->level = level;
    }
}
static void _ztl_io_w_queue_exit(int level) {
    struct ztl_queue_pool *q = &qp[level];

    q->flag_running = 0;

    pthread_join(q->w_thread, NULL);
    pthread_spin_destroy(&q->ucmd_spin);
    _ztl_io_write_rs_exit(q);
}

static void ztl_io_exit(void) {
    int level, rn;

    for (level = 0; level < ZROCKS_LEVEL_NUM; level++)
        _ztl_io_w_queue_exit(level);

    pthread_mutex_destroy(&rs_mutex);
    for (rn = 0; rn < zndmedia.read_ctx_num; rn++)
        _ztl_io_read_rs_exit(&read_resource[rn]);

    log_info("ztl-io: Write-read stopped.");
}

static int ztl_io_init(void) {
    /* 创建以level为下标的队列数组并绑定处理线程 */
    int level, rn, ret;

    for (level = 0; level < ZROCKS_LEVEL_NUM; level++) {
        ret = _ztl_io_w_queue_init(level);
        if (ret != XZTL_OK) {
            log_err("ztl_io_init: IO resource allocation error.");
            return XZTL_ZTL_IO_ERR;
        }
    }

    for (rn = 0; rn < zndmedia.read_ctx_num; rn++) {
        ret = _ztl_io_read_rs_init(&read_resource[rn]);
        if (ret != XZTL_OK) {
            log_err("ztl_io_init: IO read resource allocation error.");
            // goto RC;
            return XZTL_ZTL_IO_ERR;
        }
    }

    if (pthread_mutex_init(&rs_mutex, 0))
        return XZTL_ZTL_IO_ERR;
    log_info("ztl-io: Write-read module started.");

    return XZTL_OK;

    /*RC:
        while (rn) {
            rn--;
            _ztl_io_read_rs_exit(&read_resource[rn]);
        }

        return XZTL_ZTL_IO_ERR;*/
}

static struct app_io_mod libztl_io = {.mod_id     = LIBZTL_IO,
                                      .name       = "LIBZTL-IO",
                                      .init_fn    = ztl_io_init,
                                      .exit_fn    = ztl_io_exit,
                                      .submit_fn  = ztl_io_submit,
                                      .read_fn    = ztl_io_read,
                                      .nodeset_fn = ztl_io_nodeset};

void ztl_io_register(void) {
    ztl_mod_register(ZTLMOD_IO, LIBZTL_IO, &libztl_io);
}
