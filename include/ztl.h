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
#include <xztl.h>
#include <xztl-ztl.h>

#define ZTL_PRO_TYPES    64  /* Number of provisioning types */
#define ZTL_PRO_MP_SZ    32  /* Mempool size per thread */
#define ZTL_PRO_STRIPE	 32  /* Number of zones for parallel write */

enum ztl_pro_type_list {
    ZTL_PRO_TUSER = 0x0
};

struct ztl_pro_zone {
    struct xztl_maddr		addr;
    struct app_zmd_entry       *zmd_entry;
    uint64_t 			capacity;
    uint8_t 			lock;
    uint8_t 			state;
    TAILQ_ENTRY (ztl_pro_zone) entry;
    TAILQ_ENTRY (ztl_pro_zone) open_entry;
};

struct ztl_pro_grp {
    struct ztl_pro_zone *vzones;
    uint32_t nfree;
    uint32_t nused;
    uint32_t nopen[ZTL_PRO_TYPES];

    TAILQ_HEAD (free_list, ztl_pro_zone) free_head;
    TAILQ_HEAD (used_list, ztl_pro_zone) used_head;

    /* Open zones for distinct provisioning types */
    TAILQ_HEAD (open_list, ztl_pro_zone) open_head[ZTL_PRO_TYPES];

    pthread_spinlock_t spin;
};

struct map_md_addr {
    union {
        struct {
            uint64_t addr  : 63;
            uint64_t flag : 1;
        } g;
        uint64_t addr;
    };
};

int  ztl_pro_grp_init (struct app_group *grp);
void ztl_pro_grp_exit (struct app_group *grp);
int  ztl_pro_grp_put_zone (struct app_group *grp, uint32_t zone_i);
int  ztl_pro_grp_finish_zn (struct app_group *grp, uint32_t zid, uint8_t type);
int  ztl_pro_grp_get (struct app_group *grp, struct app_pro_addr *ctx,
			    uint32_t nsec, uint16_t ptype, uint8_t multi);
void ztl_pro_grp_free (struct app_group *grp, uint32_t zone_i,
					    uint32_t nsec, uint16_t type);
