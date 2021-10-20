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

#include <xztl-ztl.h>
#include <ztl_metadata.h>
#include <libznd.h>
#include <libxnvme.h>
#include <ztl-media.h>
#include <ztl.h>
#include <libzrocks.h>

static struct znd_media *_zndmedia;
static struct ztl_metadata metadata;

uint64_t zrocks_get_metadata_slba() {
    return 0;
}

int get_metadata_zone_num() {
    return metadata.zone_num;
}

struct ztl_metadata *get_ztl_metadata() {
    return &metadata;
}


static uint64_t get_metadata_slba(int start, int end) {
    int zone_id;
    struct ztl_pro_zone *zone;
    for (zone_id = start; zone_id < end; zone_id++) {
        zone = &metadata.metadata_zone[zone_id];
        if (zone->zmd_entry->wptr
                < zone->zmd_entry->addr.addr + zone->capacity) {
            return zone->zmd_entry->wptr;
        }
    }
    return 0;
}

int ztl_metadata_init(struct app_group *grp) {
    struct znd_descr *zinfo;
    struct ztl_pro_zone *zone;
    struct app_zmd_entry *zmde;
    struct xztl_core *core;
    int zone_i;
    get_xztl_core(&core);
    _zndmedia = get_znd_media();
    metadata.zone_num = 1;
    metadata.nlb_max = _zndmedia->devgeo->mdts_nbytes/core->media->geo.nbytes;
    metadata.metadata_zone = (struct ztl_pro_zone*) calloc(
            sizeof(struct ztl_pro_zone), metadata.zone_num);
    if (!metadata.metadata_zone) {
       return XZTL_ZTL_MD_ERR;
    }

    if (pthread_mutex_init(&metadata.page_spin, 0)) {
        return XZTL_ZTL_MD_ERR;
    }

    for (zone_i = 0; zone_i < metadata.zone_num; zone_i++) {

        /* We are getting the full report here */
        zinfo = ZND_REPORT_DESCR(grp->zmd.report,
                grp->id * core->media->geo.zn_grp + zone_i);

        zone = &metadata.metadata_zone[zone_i];

        zmde = ztl()->zmd->get_fn(grp, zone_i, 0);

        if (zmde->addr.g.zone != zone_i || zmde->addr.g.grp != grp->id)
            log_erra("ztl-pro: zmd entry address does not match (%d/%d)(%d/%d)",
                    zmde->addr.g.grp, zmde->addr.g.zone, grp->id, zone_i);

        if ((zmde->flags & XZTL_ZMD_RSVD) || !(zmde->flags & XZTL_ZMD_AVLB)) {
            printf("flags: %x\n", zmde->flags);
            continue;
        }

        zone->addr.addr = zmde->addr.addr;
        zone->capacity = zinfo->zcap;
        zone->state = zinfo->zs;
        zone->zmd_entry = zmde;
        zone->lock = 0;
        zmde->wptr = zmde->wptr_inflight = zinfo->wp;
    }
    metadata.file_slba = get_metadata_slba(0, metadata.zone_num);
    printf("init metadata.file_slba:%d\n", metadata.file_slba);
    return 0;
}

int zrocks_read_metadata(uint64_t slba, unsigned char* buf, uint32_t length) {
    struct xztl_mp_entry *mp_entry = NULL;
    uint16_t nlb = length / _zndmedia->devgeo->nbytes;
    int ret = 0;
    uint16_t left_nlb = nlb;
    while (left_nlb > 0) {
        uint16_t read_nlb = left_nlb > MAX_READ_NLB_NUM ? MAX_READ_NLB_NUM : left_nlb;
        mp_entry = xztl_mempool_get(ZROCKS_MEMORY, 0);
        if (!mp_entry) {
            return -1;
        }

        struct xztl_io_mcmd cmd;
        cmd.opcode  = XZTL_CMD_READ;
        cmd.naddr   = 1;
        cmd.synch   = 1;
        cmd.addr[0].addr = 0;
        cmd.nsec[0] = read_nlb;
        cmd.prp[0]  = (uint64_t) mp_entry->opaque;
        cmd.addr[0].g.sect = slba;
        cmd.status  = 0;
        ret = xztl_media_submit_io(&cmd);
        if (ret) {
            xztl_mempool_put(mp_entry, ZROCKS_MEMORY, 0);
            log_erra("zrocks_read_metadata. ret %d", ret);
            return ret;
        }

        memcpy(buf, (unsigned char *) mp_entry->opaque, read_nlb * ZNS_ALIGMENT);
        xztl_mempool_put(mp_entry, ZROCKS_MEMORY, 0);
        left_nlb -= read_nlb;
        buf += (read_nlb * ZNS_ALIGMENT);
        slba += read_nlb;
    }

    return ret;
}

static inline int zrocks_reset_file_md(uint8_t reset_op) {
    struct xnvme_req xreq = { 0 };
    int err = 0;
    int zone_id;

    for (zone_id = 0; zone_id < metadata.zone_num; zone_id++) {
        err = znd_cmd_mgmt_send(_zndmedia->dev, xnvme_dev_get_nsid(_zndmedia->dev),
                metadata.metadata_zone[zone_id].zmd_entry->addr.addr, reset_op,
                0, NULL, XNVME_CMD_SYNC, &xreq);
        if (err) {
            log_erra("znd_cmd_mgmt_send. err %d", err);
            break;
        }
    }
    return err;
}

int zrocks_write_file_metadata(const unsigned char* buf, uint32_t length) {
    uint16_t nlb;
    uint32_t max_len, remain_len, write_len;
    struct xztl_core *core;
    get_xztl_core(&core);
    struct xnvme_req xreq = { 0 };
    int err = 0;
    max_len = MAX_WRITE_NLB_NUM * ZNS_ALIGMENT;
    remain_len = length;
    const unsigned char *data = buf;
    pthread_mutex_lock(&metadata.page_spin);
    if (metadata.file_slba*core->media->geo.nbytes + length
            >= (metadata.metadata_zone[metadata.zone_num - 1].capacity)*core->media->geo.nbytes) {
        zrocks_reset_file_md(ZND_SEND_RESET);
        metadata.file_slba = 0;
    }

    while (remain_len > 0) {
        write_len = (remain_len > max_len) ? max_len : remain_len;
        nlb = write_len / core->media->geo.nbytes;
        err = xnvme_cmd_write(_zndmedia->dev, xnvme_dev_get_nsid(_zndmedia->dev),
                metadata.file_slba, nlb - 1, data, NULL, XNVME_CMD_SYNC, &xreq);
        if (err || xnvme_req_cpl_status(&xreq)) {
            xnvmec_perr("xnvme_cmd_write()", err);
            xnvme_req_pr(&xreq, XNVME_PR_DEF);
            err = err ? err : -XZTL_ZTL_MD_ERR;
            break;
        }
        metadata.file_slba += nlb;
        data += write_len;
        remain_len -= write_len;
    }
    pthread_mutex_unlock(&metadata.page_spin);
    return err;
}
