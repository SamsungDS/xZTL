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

#include <pthread.h>
#include <sys/queue.h>
#include <xztl-ztl.h>
#include <xztl.h>

#define ZTL_PRO_TYPES           64 /* Number of provisioning types */
#define ZTL_PRO_MP_SZ           32 /* Mempool size per thread */
#define ZTL_PRO_STRIPE          8  /* Number of zones for parallel write */
#define ZTL_PRO_ZONE_NUM_INNODE ZTL_PRO_STRIPE /* Number of zones per node */

enum ztl_pro_type_list { ZTL_PRO_TUSER = 0x0 };

enum ztl_pro_mgmt_opcode {
    ZTL_MGMG_FULL_ZONE  = 0x0,
    ZTL_MGMG_RESET_ZONE = 0x1
};

struct ztl_pro_zone {
    struct xztl_maddr     addr;
    struct app_zmd_entry *zmd_entry;
    uint64_t              capacity;
    uint8_t               lock;
    uint8_t               state;
    TAILQ_ENTRY(ztl_pro_zone) entry;
    TAILQ_ENTRY(ztl_pro_zone) open_entry;
};

struct ztl_pro_node {
    uint32_t id;

    struct ztl_pro_zone *vzones[ZTL_PRO_ZONE_NUM_INNODE];

    STAILQ_ENTRY(ztl_pro_node) fentry;
    uint32_t zone_num;
    uint32_t nr_finish_err;
    uint32_t nr_reset_err;

    uint32_t status;
};

struct ztl_pro_node_grp {
    struct ztl_pro_node *vnodes;
    struct ztl_pro_zone *vzones;

    uint32_t totalnode; /* # of free nodes */
    uint32_t nfull;     /* # of full nodes */

    // TAILQ_HEAD (free_list, ztl_pro_node) free_head;
    // TAILQ_HEAD (used_list, ztl_pro_node) full_head;
    pthread_spinlock_t spin;
};

/*struct ztl_pro_grp {
    struct ztl_pro_zone *vzones;
    uint32_t nfree;
    uint32_t nused;
    uint32_t nopen[ZTL_PRO_TYPES];

    TAILQ_HEAD(free_list, ztl_pro_zone) free_head;
    TAILQ_HEAD(used_list, ztl_pro_zone) used_head;

    /* Open zones for distinct provisioning types *//*
    TAILQ_HEAD(open_list, ztl_pro_zone) open_head[ZTL_PRO_TYPES];

    pthread_spinlock_t spin;
};*/

struct map_md_addr {
    union {
        struct {
            uint64_t addr : 63;
            uint64_t flag : 1;
        } g;
        uint64_t addr;
    };
};

int ztl_pro_grp_reset_all_zones(struct app_group *grp);
int ztl_pro_grp_node_init(struct app_group *grp);
int ztl_pro_grp_init(struct app_group *grp);

void ztl_pro_grp_exit(struct app_group *grp);
int  ztl_pro_grp_put_zone(struct app_group *grp, uint32_t zone_i);
int  ztl_pro_grp_finish_zn(struct app_group *grp, uint32_t zid, uint8_t type);
int  ztl_pro_grp_get(struct app_group *grp, struct app_pro_addr *ctx,
                     uint32_t nsec, int32_t *node_id,
                     struct xztl_thread *tdinfo);
void ztl_pro_grp_free(struct app_group *grp, uint32_t zone_i, uint32_t nsec);
int  ztl_pro_grp_node_reset(struct app_group *grp, struct ztl_pro_node *node);
int  ztl_pro_node_reset_zn(struct ztl_pro_zone *zone);
int  ztl_pro_grp_node_finish(struct app_group *grp, struct ztl_pro_node *node);
int  ztl_pro_grp_submit_mgmt(struct app_group *grp, struct ztl_pro_node *node,
                             int32_t op_code);
