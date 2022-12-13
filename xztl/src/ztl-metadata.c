/* xZTL: zone metadata
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
#include <libxnvme_nvm.h>
#include <libxnvme_znd.h>
#include <libxnvmec.h>
#include <libzrocks.h>
#include <xztl.h>
#include <xztl-metadata.h>
#include <xztl-stats.h>
#include <xztl-pro.h>

static struct znd_media   *_zndmedia;
static struct ztl_metadata metadata;

#define META_READ_MAX_RETRY  3
#define META_WRITE_MAX_RETRY 3

uint64_t zrocks_get_metadata_slba() {
    return XZTL_OK;
}

void zrocks_get_metadata_slbas(uint64_t *slbas, uint8_t *num) {
    int                  zone_id;
    struct ztl_pro_zone *zone;
    for (zone_id = 0; zone_id < metadata.zone_num; zone_id++) {
        zone           = &metadata.metadata_zone[zone_id];
        slbas[zone_id] = zone->addr.g.sect;
    }

    *num = metadata.zone_num;
}

static inline int zrocks_reset_file_md(uint64_t addr) {
    struct xnvme_cmd_ctx xnvme_ctx = xnvme_cmd_ctx_from_dev(_zndmedia->dev);
    int                  err       = 0;
    xnvme_ctx.async.queue          = NULL;
    xnvme_ctx.async.cb_arg         = NULL;
    err = xnvme_znd_mgmt_send(&xnvme_ctx, xnvme_dev_get_nsid(_zndmedia->dev),
                              addr, false, XNVME_SPEC_ZND_CMD_MGMT_SEND_RESET,
                              0, NULL);

    if (err) {
        log_erra("zrocks_reset_file_md: znd_cmd_mgmt_send. err [%d]\n", err);
        return XZTL_ZTL_MD_RESET_ERR;
    }

    return XZTL_OK;
}

void zrocks_switch_zone(uint64_t slbas) {
    int                  zone_id;
    struct ztl_pro_zone *zone;
    for (zone_id = 0; zone_id < metadata.zone_num; zone_id++) {
        zone = &metadata.metadata_zone[zone_id];
        if (zone->addr.g.sect != slbas) {
            metadata.curr_zone_index = zone_id;
            zrocks_reset_file_md(zone->addr.g.sect);
            zone->zmd_entry->wptr = zone->addr.g.sect;
            break;
        }
    }
}

int get_metadata_zone_num() {
    return metadata.zone_num;
}

struct ztl_metadata *get_ztl_metadata() {
    return &metadata;
}

static int get_curr_metadata_zone(int start, int end) {
    int                  zone_id;
    struct ztl_pro_zone *zone;
    for (zone_id = start; zone_id < end; zone_id++) {
        zone = &metadata.metadata_zone[zone_id];
        if (zone->zmd_entry->wptr < zone->addr.g.sect + zone->capacity) {
            return zone_id;
        }
    }
    return 0;
}

int ztl_metadata_init(struct app_group *grp) {
    struct xnvme_spec_znd_descr *zinfo;
    struct ztl_pro_zone         *zone;
    struct app_zmd_entry        *zmde;
    struct xztl_core            *core;
    int                          zone_i;
    get_xztl_core(&core);
    _zndmedia         = get_znd_media();
    metadata.zone_num = 2;
    metadata.nlb_max = _zndmedia->devgeo->mdts_nbytes / core->media->geo.nbytes;
    metadata.metadata_zone = (struct ztl_pro_zone *)calloc(
        metadata.zone_num, sizeof(struct ztl_pro_zone));
    if (!metadata.metadata_zone) {
        log_err("ztl_metadata_init failed:metadata.metadata_zone is NULL\n");
        return XZTL_ZTL_MD_INIT_ERR;
    }

    if (pthread_mutex_init(&metadata.page_spin, 0)) {
        log_err("ztl_metadata_init failed: pthread_mutex_init failed\n");
        return XZTL_ZTL_MD_INIT_ERR;
    }

    for (zone_i = 0; zone_i < metadata.zone_num; zone_i++) {
        /* We are getting the full report here */
        zinfo = XNVME_ZND_REPORT_DESCR(
            grp->zmd.report, grp->id * core->media->geo.zn_grp + zone_i);

        zone = &metadata.metadata_zone[zone_i];

        zmde = ztl()->zmd->get_fn(grp, zone_i, 0);

        if (zmde->addr.g.zone != zone_i || zmde->addr.g.grp != grp->id)
            log_erra(
                "ztl_metadata_init: zmd entry address does not match [%d/%d] "
                "[%d/%d]\n",
                zmde->addr.g.grp, zmde->addr.g.zone, grp->id, zone_i);

        if (!(zmde->flags & XZTL_ZMD_AVLB)) {
            log_erra("ztl_metadata_init: Cannot read an invalid zone [%d]\n",
                     zone_i);
            return XZTL_ZTL_MD_INIT_ERR;
        }

        if (zmde->flags & XZTL_ZMD_RSVD) {
            log_erra("ztl_metadata_init: Zone is RESERVED [%d]\n", zone_i);
            return XZTL_ZTL_MD_INIT_ERR;
        }

        zone->addr.addr = zmde->addr.addr;
        zone->capacity  = zinfo->zcap;
        zone->state     = zinfo->zs;
        zone->zmd_entry = zmde;
        zone->lock      = 0;
        zmde->wptr = zmde->wptr_inflight = zinfo->wp;
    }
    metadata.curr_zone_index = get_curr_metadata_zone(0, metadata.zone_num);
    log_infoa("ztl_metadata_init: metadata.current_zone [%ull]\n",
              metadata.curr_zone_index);

    return XZTL_OK;
}

int zrocks_read_metadata(uint64_t slba, unsigned char *buf, uint32_t length) {
    struct xztl_mp_entry *mp_entry = NULL;
    uint16_t              nlb      = length / _zndmedia->devgeo->nbytes;
    int                   ret      = 0;
    uint16_t              left_nlb = nlb;
    while (left_nlb > 0) {
        uint16_t read_nlb =
            left_nlb > MAX_READ_NLB_NUM ? MAX_READ_NLB_NUM : left_nlb;
        mp_entry = xztl_mempool_get(ZROCKS_MEMORY, 0);
        if (!mp_entry) {
            log_err("zrocks_read_metadata: xztl_mempool_get memory failed\n");
            return XZTL_ZTL_MD_READ_ERR;
        }

        struct xztl_io_mcmd cmd;
        cmd.opcode         = XZTL_CMD_READ;
        cmd.naddr          = 1;
        cmd.synch          = 1;
        cmd.addr[0].addr   = 0;
        cmd.nsec[0]        = read_nlb;
        cmd.prp[0]         = (uint64_t)mp_entry->opaque;
        cmd.addr[0].g.sect = slba;
        cmd.status         = 0;
        int retry          = 0;

    META_READ_FAIL:
        ret = xztl_media_submit_io(&cmd);
        if (ret) {
            xztl_stats_inc(XZTL_STATS_META_READ_FAIL, 1);
            log_erra("zrocks_read_metadata error: [%d]\n", ret);

            retry++;
            if (retry < META_READ_MAX_RETRY) {
                goto META_READ_FAIL;
            }

            xztl_mempool_put(mp_entry, ZROCKS_MEMORY, 0);
            return XZTL_ZTL_MD_READ_ERR;
        }

        memcpy(buf, (unsigned char *)mp_entry->opaque, read_nlb * ZNS_ALIGMENT);
        xztl_mempool_put(mp_entry, ZROCKS_MEMORY, 0);
        left_nlb -= read_nlb;
        buf += (read_nlb * ZNS_ALIGMENT);
        slba += read_nlb;
    }

    return XZTL_OK;
}

int zrocks_write_file_metadata(const unsigned char *buf, uint32_t length) {
    uint16_t          nlb;
    uint32_t          max_len, remain_len, write_len;
    struct xztl_core *core;
    int               err = 0;

    struct xnvme_cmd_ctx xnvme_ctx = xnvme_cmd_ctx_from_dev(_zndmedia->dev);
    get_xztl_core(&core);
    max_len                   = MAX_WRITE_NLB_NUM * ZNS_ALIGMENT;
    remain_len                = length;
    const unsigned char *data = buf;

    pthread_mutex_lock(&metadata.page_spin);
    struct ztl_pro_zone *zone =
        &metadata.metadata_zone[metadata.curr_zone_index];
    if (zone->zmd_entry->wptr + length / core->media->geo.nbytes >=
        zone->addr.g.sect + zone->capacity) {
        zrocks_switch_zone(zone->addr.g.sect);
        pthread_mutex_unlock(&metadata.page_spin);
        return XZTL_ZTL_MD_WRITE_FULL;
    }

    while (remain_len > 0) {
        write_len = (remain_len > max_len) ? max_len : remain_len;
        nlb       = write_len / core->media->geo.nbytes;
        int retry = 0;

    META_WRITE_FAIL:
        err = xnvme_nvm_write(&xnvme_ctx, xnvme_dev_get_nsid(_zndmedia->dev),
                              zone->zmd_entry->wptr, nlb - 1, data, NULL);

        if (err || xnvme_cmd_ctx_cpl_status(&xnvme_ctx)) {
            xnvmec_perr("xnvme_nvm_write()", err);
            xnvme_cmd_ctx_pr(&xnvme_ctx, XNVME_PR_DEF);
            err = err ? err : -XZTL_ZTL_MD_WRITE_ERR;
            xztl_stats_inc(XZTL_STATS_META_WRITE_FAIL, 1);
            retry++;
            if (retry < META_WRITE_MAX_RETRY) {
                goto META_WRITE_FAIL;
            }

            break;
        }

        zone->zmd_entry->wptr += nlb;
        data += write_len;
        remain_len -= write_len;
    }

    pthread_mutex_unlock(&metadata.page_spin);
    if (err) {
        log_erra(
            "zrocks_write_file_metadata. file_slb [%lu] sec [%u] err [%d]\n",
            metadata.file_slba, nlb, err);
        return XZTL_ZTL_MD_WRITE_ERR;
    }

    return XZTL_OK;
}
