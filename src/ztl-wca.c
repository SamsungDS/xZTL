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
#include <xztl-ztl.h>
#include <xztl.h>
#include <ztl.h>

#define ZTL_MCMD_ENTS       XZTL_IO_MAX_MCMD
#define ZROCKS_DEBUG        0

#define XZTL_CTX_NVME_DEPTH 128

extern struct app_group **glist;

uint8_t THREAD_NUM;

static void *zrocks_alloc(size_t size) {
    return xztl_media_dma_alloc(size);
}

static void zrocks_free(void *ptr) {
    xztl_media_dma_free(ptr);
}

static void zrocks_read_callback_mcmd(void *arg) {
    struct xztl_io_ucmd *ucmd;
    struct xztl_io_mcmd *mcmd;
    uint32_t             misalign;

    mcmd = (struct xztl_io_mcmd *)arg;
    ucmd = (struct xztl_io_ucmd *)mcmd->opaque;

    if (mcmd->status) {
        ucmd->status = mcmd->status;
    } else {
        /* If I/O succeeded, we copy the data from the correct offset to the
         * user */
        misalign = mcmd->sequence;  // temp
        memcpy(ucmd->buf + mcmd->buf_off,
            (char *)mcmd->prp[0] + misalign, mcmd->cpsize); // NOLINT
    }

    xztl_atomic_int16_update(&ucmd->ncb, ucmd->ncb + 1);

    if (mcmd->status) {
        ZDEBUG(ZDEBUG_WCA,
               "ztl-wca: Callback. (ID %lu, S %d/%d, C %d, WOFF 0x%lx). St: %d",
               ucmd->id, mcmd->sequence, ucmd->nmcmd, ucmd->ncb,
               ucmd->moffset[mcmd->sequence], mcmd->status);
    }

    if (ucmd->ncb == ucmd->nmcmd) {
        ucmd->completed = 1;
    }
}

/* This function checks if the media offsets are sequential.
 * If not, we return a negative value. For now we do not support
 * multi-piece mapping in ZTL-managed mapping */
static int ztl_wca_check_offset_seq(struct xztl_io_ucmd *ucmd) {
    uint32_t off_i;

    for (off_i = 1; off_i < ucmd->nmcmd; off_i++) {
        if (ucmd->moffset[off_i] !=
            ucmd->moffset[off_i - 1] + ucmd->msec[off_i - 1]) {
            return -1;
        }
    }

    return XZTL_OK;
}

/* This function prepares a multi-piece mapping to return to the user.
 * Each entry contains the offset and size, and the full list represents
 * the entire buffer. */
static void ztl_wca_reorg_ucmd_off(struct xztl_io_ucmd *ucmd) {
    uint32_t off_i, curr, first_off, size;

    curr      = 0;
    first_off = 0;
    size      = 0;

    for (off_i = 1; off_i < ucmd->nmcmd; off_i++) {
        size += ucmd->msec[off_i - 1];

        /* If offset of sector is not sequential to the previousi one */
        if ((ucmd->moffset[off_i] !=
             ucmd->moffset[off_i - 1] + ucmd->msec[off_i - 1]) ||

            /* Or zone is not the same as the previous one */
            (ucmd->mcmd[off_i]->addr[0].g.zone !=
             ucmd->mcmd[off_i - 1]->addr[0].g.zone)) {
            /* Close the piece and set first offset + size */
            ucmd->moffset[curr] = ucmd->moffset[first_off];
            ucmd->msec[curr]    = size;

            first_off = off_i;
            size      = 0;
            curr++;

            /* If this is the last sector, we need to add it to the list */
            if (off_i == ucmd->nmcmd - 1) {
                size += ucmd->msec[first_off];
                ucmd->moffset[curr] = ucmd->moffset[first_off];
                ucmd->msec[curr]    = size;
                curr++;
            }

            /* If this is the last sector and belongs to the previous piece */
        } else if (off_i == ucmd->nmcmd - 1) {
            /* Merge sector to the previous piece */
            size += ucmd->msec[off_i];
            ucmd->moffset[curr] = ucmd->moffset[first_off];
            ucmd->msec[curr]    = size;
            curr++;
        }
    }

    ucmd->noffs = (ucmd->nmcmd > 1) ? curr : 1;
}

static void ztl_wca_callback_mcmd(void *arg) {
    struct xztl_io_ucmd * ucmd;
    struct xztl_io_mcmd * mcmd;
    struct app_map_entry  map;
    struct app_zmd_entry *zmd;
    uint64_t              old;
    int                   ret, off_i;

    mcmd = (struct xztl_io_mcmd *)arg;
    ucmd = (struct xztl_io_ucmd *)mcmd->opaque;

    ucmd->minflight[mcmd->sequence_zn] = 0;

    if (mcmd->status) {
        ucmd->status = mcmd->status;
    } else {
        ucmd->moffset[mcmd->sequence] = mcmd->paddr[0];
    }

    xztl_atomic_int16_update(&ucmd->ncb, ucmd->ncb + 1);

    if (mcmd->status)
        ZDEBUG(ZDEBUG_WCA,
               "ztl-wca: Callback. (ID %lu, S %d/%d, C %d, WOFF 0x%lx). St: %d",
               ucmd->id, mcmd->sequence, ucmd->nmcmd, ucmd->ncb,
               ucmd->moffset[mcmd->sequence], mcmd->status);


    if (ucmd->ncb == ucmd->nmcmd) {
        ucmd->completed = 1;
    }
}

static void ztl_wca_callback(struct xztl_io_mcmd *mcmd) {
    ztl_wca_callback_mcmd(mcmd);
}

static int ztl_thd_allocNode_for_thd(struct xztl_thread *tdinfo) {
    struct ztl_pro_node_grp *pro = (struct ztl_pro_node_grp *)(glist[0]->pro);

    uint32_t snodeidx = 0;
    uint32_t cnt      = 0;

    pthread_spin_lock(&pro->spin);

    for (int i = 0; i < pro->totalnode && cnt < ZTL_ALLOC_NODE_NUM; i++) {
        if (XZTL_ZMD_NODE_FREE == pro->vnodes[i].status &&
            pro->vnodes[i].zone_num == ZTL_PRO_ZONE_NUM_INNODE) {
            pro->vnodes[i].status = XZTL_ZMD_NODE_USED;
            STAILQ_INSERT_TAIL(&(tdinfo->free_head), &(pro->vnodes[i]), fentry);
            cnt++;
            tdinfo->nfree++;
        }
    }
    pthread_spin_unlock(&pro->spin);

    return cnt;
}

static uint32_t ztl_thd_getNodeId(struct xztl_thread *tdinfo) {
    struct ztl_pro_node *node = STAILQ_FIRST(&tdinfo->free_head);
    int                  cnt  = 0;

    if (!node) {
        cnt = ztl_thd_allocNode_for_thd(tdinfo);
        if (cnt == 0) {
            log_err("No available node resource.\n");
            return -1;
        } else {
            node = STAILQ_FIRST(&tdinfo->free_head);
            if (!node) {
                log_err("Invalid node.\n");
                return -1;
            }
        }
    }

    STAILQ_REMOVE_HEAD(&tdinfo->free_head, fentry);
    tdinfo->nfree--;

    return node->id;
}

static int ztl_thd_submit(struct xztl_io_ucmd *ucmd) {
    int  tid, ret;
    bool flag = true;

    tid = ucmd->xd.tid;

    if (ucmd->xd.node_id == -1) {
        ucmd->xd.node_id = ztl_thd_getNodeId(&xtd[tid]);
    }

    ret = 0;
    if (ucmd->xd.node_id != -1) {
        ucmd->xd.tdinfo = &xtd[tid];

        if (ucmd->prov_type == XZTL_CMD_READ) {
            ret = ztl_wca_read_ucmd(ucmd, ucmd->xd.node_id, ucmd->offset, ucmd->size);
        } else if (ucmd->prov_type == XZTL_CMD_WRITE) {
            ztl_wca_write_ucmd(ucmd, &ucmd->xd.node_id);
        }

    } else {
        log_erra("No available node resource.\n");
    }

    return ret;
}

static uint32_t ztl_wca_ncmd_prov_based(struct app_pro_addr *prov) {
    uint32_t zn_i, ncmd;

    ncmd = 0;
    for (zn_i = 0; zn_i < prov->naddr; zn_i++) {
        ncmd += prov->nsec[zn_i] / ZTL_WCA_SEC_MCMD;
        if (prov->nsec[zn_i] % ZTL_WCA_SEC_MCMD != 0)
            ncmd++;
    }

    return ncmd;
}

static void ztl_wca_poke_ctx(struct xztl_mthread_ctx *tctx) {
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

int ztl_wca_read_ucmd(struct xztl_io_ucmd *ucmd, uint32_t node_id,
                       uint64_t offset, size_t size) {
    struct ztl_pro_node_grp *pro;
    struct ztl_pro_node *    znode;
    struct xztl_io_mcmd *    mcmd;

    uint64_t misalign, sec_size, sec_start, zindex, zone_sec_off, read_num;
    uint64_t sec_left, bytes_off, left;
    uint32_t nlevel, ncmd, cmd_i, total_cmd, zone_i, submitted;
    int      ret = 0;

    struct xztl_thread *     tdinfo = ucmd->xd.tdinfo;
    struct xztl_mthread_ctx* tctx = tdinfo->tctx[0];

    // struct app_group *grp = ztl()->groups.get_fn(0);
    struct app_group *grp = glist[0];
    pro                   = grp->pro;
    znode                 = (struct ztl_pro_node *)(&pro->vnodes[node_id]);

    /*
     *1. check if it is normal : size==0   offset+size
     *2. offset
     *3. byte_off_sec:
     *4. sec_size:alignment 
     *5. sec_start:the start sector 
     */

    misalign = offset % ZNS_ALIGMENT;
    sec_size = (size + misalign) / ZNS_ALIGMENT +
               (((size + misalign) % ZNS_ALIGMENT) ? 1 : 0);
    sec_start       = offset / ZNS_ALIGMENT;
    int level_bytes = ZTL_PRO_ZONE_NUM_INNODE * ZTL_READ_SEC_MCMD;

    /* Count level:(8 * 16=128) (0~127 0lun 128~255 1lun ...)*/
    nlevel = sec_start / level_bytes;
    zindex = (sec_start % level_bytes) / ZTL_READ_SEC_MCMD;
    zone_sec_off = (nlevel * ZTL_READ_SEC_MCMD) +
                   (sec_start % level_bytes) % ZTL_READ_SEC_MCMD;
    read_num =
        ZTL_READ_SEC_MCMD - (sec_start % level_bytes) % ZTL_READ_SEC_MCMD;
    read_num = (sec_size > read_num) ? read_num : sec_size;

    /*printf("misalign=%lu sec_size=%lu sec_start=%lu nlevel=%d zindex=%lu
       zone_sec_off=%lu read_num=%lu\r\n",
        misalign, sec_size, sec_start, nlevel, zindex, zone_sec_off,
       read_num);*/

    if (ZROCKS_DEBUG)
        log_infoa("zrocks (__read): sec_size %lu\n", sec_size);

    sec_left  = sec_size;
    bytes_off = 0;
    left      = size;
    total_cmd = 0;
    while (sec_left) {
        mcmd = tdinfo->mcmd[total_cmd];
        memset(mcmd, 0x0, sizeof(struct xztl_io_mcmd));

        mcmd->opcode       = XZTL_CMD_READ;
        mcmd->naddr        = 1;
        mcmd->synch        = 0;
        mcmd->async_ctx    = tctx;
        mcmd->addr[0].addr = 0;
        mcmd->nsec[0]      = read_num;

        sec_left -= mcmd->nsec[0];
        mcmd->prp[0] = tdinfo->prp[total_cmd];

        mcmd->addr[0].g.sect =
            znode->vzones[zindex]->addr.g.sect + zone_sec_off;
        mcmd->status   = 0;
        mcmd->callback = zrocks_read_callback_mcmd;

        mcmd->sequence    = misalign;  // tmp prp offset
        mcmd->sequence_zn = zindex;
        mcmd->buf_off     = bytes_off;  // temp
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

        zone_sec_off = nlevel * ZTL_READ_SEC_MCMD;
        read_num =
            (sec_left > ZTL_READ_SEC_MCMD) ? ZTL_READ_SEC_MCMD : sec_left;
        bytes_off += mcmd->cpsize;
        misalign = 0;
    }

    if (total_cmd == 1) {
        ucmd->mcmd[0]->synch = 1;
        ret = xztl_media_submit_io(ucmd->mcmd[0]);

        misalign = ucmd->mcmd[0]->sequence;
        memcpy(ucmd->buf,
           (char *) ( ucmd->mcmd[0]->prp[0] + misalign), /* NOLINT */
            ucmd->mcmd[0]->cpsize);

        ucmd->completed = 1;
        return ret;
    }

    submitted = 0;
    while (submitted < total_cmd) {
        for (cmd_i = 0; cmd_i < total_cmd; cmd_i++) {
            if (ucmd->mcmd[cmd_i]->submitted)
                continue;

            ret = xztl_media_submit_io(ucmd->mcmd[cmd_i]);
            if (ret) {
                log_erra("__zrocks_read err %d\r\n", ret);
                goto FAIL_SUBMIT;
            }

            ucmd->mcmd[cmd_i]->submitted = 1;
            submitted++;
        }
    }

    int err = xnvme_queue_wait(tctx->queue);
    if (err < 0) {
        log_erra("xnvme_queue_wait() returns error %d\r\n", err);
    }
    ucmd->completed = 1;
    return ret;

FAIL_SUBMIT:
    if (submitted) {
        int err = xnvme_queue_wait(tctx->queue);
        if (err < 0) {
            log_erra("xnvme_queue_wait() returns error %d\r\n", err);
        }
        ucmd->completed = 1;
    }

    return ret;
}

void ztl_wca_write_ucmd(struct xztl_io_ucmd *ucmd, int32_t *node_id) {
    struct app_pro_addr *prov;
    struct xztl_io_mcmd *mcmd;
    struct xztl_core *   core;
    get_xztl_core(&core);
    uint32_t nsec, nsec_zn, ncmd, cmd_i, zn_i, submitted;
    int      zn_cmd_id[ZTL_PRO_STRIPE * 2][2000] = {-1};
    int      zn_cmd_id_num[ZTL_PRO_STRIPE * 2]   = {0};
    uint64_t boff;
    int      ret, ncmd_zn, zncmd_i;

    struct xztl_thread *     tdinfo = ucmd->xd.tdinfo;
    struct xztl_mthread_ctx* tctx = tdinfo->tctx[0];

    ZDEBUG(ZDEBUG_WCA, "ztl-wca: Processing user write. ID %lu", ucmd->id);

    nsec = ucmd->size / core->media->geo.nbytes;

    /* We do not support non-aligned buffers */
    if (ucmd->size % (core->media->geo.nbytes * ZTL_WCA_SEC_MCMD_MIN) != 0) {
        log_erra("ztl-wca: Buffer is not aligned to %d bytes: %lu bytes.",
                 core->media->geo.nbytes * ZTL_WCA_SEC_MCMD_MIN, ucmd->size);
        goto FAILURE;
    }

    /* First we check the number of commands based on ZTL_WCA_SEC_MCMD */
    ncmd = nsec / ZTL_WCA_SEC_MCMD;
    if (nsec % ZTL_WCA_SEC_MCMD != 0)
        ncmd++;

    if (ncmd > XZTL_IO_MAX_MCMD) {
        log_erra(
            "ztl-wca: User command exceed XZTL_IO_MAX_MCMD. "
            "%d of %d",
            ncmd, XZTL_IO_MAX_MCMD);
        goto FAILURE;
    }

    prov = tdinfo->prov;
    if (!prov) {
        log_erra("ztl-wca: Provisioning failed. nsec %d, node_id %d", nsec,
                 *node_id);
        goto FAILURE;
    }
    ztl_pro_grp_get(glist[0], prov, nsec, node_id, tdinfo);

    /* We check the number of commands again based on the provisioning */
    ncmd = ztl_wca_ncmd_prov_based(prov);
    if (ncmd > XZTL_IO_MAX_MCMD) {
        log_erra(
            "ztl-wca: User command exceed XZTL_IO_MAX_MCMD. "
            "%d of %d",
            ncmd, XZTL_IO_MAX_MCMD);
        goto FAIL_NCMD;
    }

    ucmd->prov      = prov;
    ucmd->nmcmd     = ncmd;
    ucmd->completed = 0;
    ucmd->ncb       = 0;

    boff = (uint64_t)ucmd->buf;

    ZDEBUG(ZDEBUG_WCA, "ztl-wca: NMCMD: %d", ncmd);

    /* Populate media commands */
    cmd_i = 0;
    int zone_sector_num[ZTL_PRO_STRIPE * 2] = {0};

    for (int i = 0; i < prov->naddr; i++) {
        zone_sector_num[i] = prov->nsec[i];
    }

    while (cmd_i < ncmd) {
        for (zn_i = 0; zn_i < prov->naddr; zn_i++) {
            nsec_zn = zone_sector_num[zn_i];
            if (nsec_zn <= 0) {
                continue;
            }

            mcmd = tdinfo->mcmd[cmd_i];
            mcmd->opcode =
                (XZTL_WRITE_APPEND) ? XZTL_ZONE_APPEND : XZTL_CMD_WRITE;
            mcmd->synch       = 0;
            mcmd->submitted   = 0;
            mcmd->sequence    = cmd_i;
            mcmd->sequence_zn = zn_i;
            mcmd->naddr       = 1;
            mcmd->status      = 0;
            mcmd->nsec[0] =
                (nsec_zn >= ZTL_WCA_SEC_MCMD) ? ZTL_WCA_SEC_MCMD : nsec_zn;

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

            mcmd->callback  = ztl_wca_callback_mcmd;
            mcmd->opaque    = ucmd;
            mcmd->async_ctx = tctx;

            ucmd->mcmd[cmd_i]            = mcmd;
            ucmd->mcmd[cmd_i]->submitted = 0;
            zn_cmd_id[zn_i][zn_cmd_id_num[zn_i]++] = cmd_i;
            cmd_i++;
        }
    }

    ZDEBUG(ZDEBUG_WCA, "ztl-wca: Populated: %d", cmd_i);

    /* Submit media commands */
    for (cmd_i = 0; cmd_i < ZTL_PRO_STRIPE * 2; cmd_i++)
        ucmd->minflight[cmd_i] = 0;


    submitted = 0;
    int zn_cmd_id_index[ZTL_PRO_STRIPE * 2] = {0};
    while (submitted < ncmd) {
        for (zn_i = 0; zn_i < prov->naddr; zn_i++) {
            int index = zn_cmd_id_index[zn_i];
            int num   = zn_cmd_id_num[zn_i];
            if (index >= num) {
                continue;
            }

            /* Limit to 1 write per zone if append is not supported */
            if (!XZTL_WRITE_APPEND) {
                if (ucmd->minflight[zn_i]) {
                    ztl_wca_poke_ctx(tctx);
                    continue;
                }

                ucmd->minflight[zn_i] = 1;
            }

            ret = xztl_media_submit_io(ucmd->mcmd[zn_cmd_id[zn_i][index]]);
            if (ret) {
                ztl_wca_poke_ctx(tctx);
                zn_i--;
                continue;
            }

            ucmd->mcmd[zn_cmd_id[zn_i][index]]->submitted = 1;
            submitted++;
            zn_cmd_id_index[zn_i]++;

            if (submitted % ZTL_PRO_STRIPE == 0)
                ztl_wca_poke_ctx(tctx);
        }
        usleep(1);
    }

    /* Poke the context for completions */
    while (ucmd->ncb < ucmd->nmcmd) {
        ztl_wca_poke_ctx(tctx);
    }

    ZDEBUG(ZDEBUG_WCA, "  Submitted: %d", submitted);

    return;

    /* If we get a submit failure but previous I/Os have been
     * submitted, we fail all subsequent I/Os and completion is
     * performed by the callback function */

FAIL_NCMD:
    for (zn_i = 0; zn_i < prov->naddr; zn_i++)
        prov->nsec[zn_i] = 0;

    ztl()->pro->free_fn(prov);

FAILURE:
    ucmd->status    = XZTL_ZTL_WCA_S_ERR;
    ucmd->completed = 1;
}

static void *ztl_process_th(void *arg) {
    struct xztl_io_ucmd *ucmd;

    uint8_t             tid = *(uint8_t *)arg; // NOLINT
    struct xztl_thread *td  = &xtd[tid];

    td->wca_running = 1;

    while (td->wca_running) {
NEXT:
        if (!STAILQ_EMPTY(&td->ucmd_head)) {
            pthread_spin_lock(&td->ucmd_spin);

            ucmd = STAILQ_FIRST(&td->ucmd_head);
            STAILQ_REMOVE_HEAD(&td->ucmd_head, entry);

            pthread_spin_unlock(&td->ucmd_spin);

            if (ucmd->prov_type == XZTL_CMD_READ) {
                ztl_wca_read_ucmd(ucmd, ucmd->xd.node_id, ucmd->offset,
                                  ucmd->size);
            } else if (ucmd->prov_type == XZTL_CMD_WRITE) {
                ztl_wca_write_ucmd(ucmd, &ucmd->xd.node_id);
            }

            goto NEXT;
        }
    }

    return NULL;
}

static int _ztl_thd_init(struct xztl_thread *td) {
    int mcmd_id;

    td->usedflag = false;

    for (mcmd_id = 0; mcmd_id < ZTL_TH_RC_NUM; mcmd_id++) {
        // each mcmd read max 256K(64 * 4K)
        td->prp[mcmd_id] = zrocks_alloc(64 *1024); //each mcmd read max 64K(64 * 4K)
        td->mcmd[mcmd_id] = aligned_alloc(64, sizeof(struct xztl_io_mcmd));
    }

    td->prov = zrocks_alloc(sizeof(struct app_pro_addr));
    if (!td->prov) {
        log_err("Thread resource (data buffer) allocation error.");
        return -1;
    }

    struct app_pro_addr *prov = (struct app_pro_addr *)td->prov;
    prov->grp                 = glist[0];

    td->tctx[0] = xztl_ctx_media_init(XZTL_CTX_NVME_DEPTH);
    if (!td->tctx[0]) {
        log_err("Thread resource (tctx) allocation error.");
        return -1;
    }

    STAILQ_INIT(&td->free_head);
    if (pthread_spin_init(&td->ucmd_spin, 0))
        return -1;
    return XZTL_OK;
}

static int ztl_thd_init(void) {
    int tid, ret;
    THREAD_NUM = 0;

    for (tid = 0; tid < ZTL_TH_NUM; tid++) {
        xtd[tid].tid = tid;
        ret          = _ztl_thd_init(&xtd[tid]);
        if (!ret) {
            THREAD_NUM++;
        } else {
            log_erra("ztl-thd: thread (%d) created failed.", tid);
            break;
        }
    }

    return ret;
}

static void ztl_thd_exit(void) {
    int                 tid, ret;
    struct xztl_thread *td = NULL;
    int                 mcmd_id;

    for (tid = 0; tid < ZTL_TH_NUM; tid++) {
        td              = &xtd[tid];
        td->wca_running = 0;
        for (mcmd_id = 0; mcmd_id < ZTL_TH_RC_NUM; mcmd_id++) {
            zrocks_free(td->prp[mcmd_id]);
            free(td->mcmd[mcmd_id]);
        }

        zrocks_free(td->prov);
        xztl_ctx_media_exit(td->tctx);
        pthread_spin_destroy(&td->ucmd_spin);

        pthread_join(td->wca_thread, NULL);
    }

    log_info("ztl-thd stopped.\n");
}

static struct app_wca_mod libztl_wca = {.mod_id      = LIBZTL_WCA,
                                        .name        = "LIBZTL-WCA",
                                        .init_fn     = ztl_thd_init,
                                        .exit_fn     = ztl_thd_exit,
                                        .submit_fn   = ztl_thd_submit,
                                        .callback_fn = ztl_wca_callback};

void ztl_wca_register(void) {
    ztl_mod_register(ZTLMOD_WCA, LIBZTL_WCA, &libztl_wca);
}
