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

#ifndef XZTL_METADATA_H
#define XZTL_METADATA_H

#include <libzrocks.h>
#include <xztl.h>
#include <xztl-mods.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZNS_OBJ_STORE     0
#define OBJ_SEC_NUM       8
#define OBJ_TABLE_SIZE    256
#define MAX_READ_NLB_NUM  128
#define MAX_WRITE_NLB_NUM 64  // 128 got errors ocassionally

struct ztl_metadata {
    struct ztl_pro_zone *metadata_zone;
    int                  zone_num;
    uint64_t             file_slba;
    int                  curr_zone_index;
    int                  nlb_max;
    pthread_mutex_t      page_spin;
};

struct obj_meta_data_head {
    union {
        struct head {
            struct app_magic byte;
            uint64_t         metadata_len;
        } h;
        uint8_t addr[ZNS_ALIGMENT];
    };
};

struct ztl_metadata *get_ztl_metadata();
uint64_t             zrocks_get_metadata_slba();
void                 zrocks_get_metadata_slbas(uint64_t *slbas, uint8_t *num);
void                 zrocks_set_metadata_slba(uint64_t slbas);
int                  ztl_metadata_init(struct app_group *grp);
int                  get_metadata_zone_num();

#ifdef __cplusplus
};  // closing brace for extern "C"
#endif

#endif
