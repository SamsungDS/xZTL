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

#ifndef XZTL_MODS_H
#define XZTL_MODS_H

#include <xztl-mempool.h>
#include <xztl.h>

#define APP_MOD_COUNT 10
#define APP_FN_SLOTS  32

#define APP_MAX_GRPS 32

/* 32K/4K page for 4K/512b sec sz */
#define ZTL_MPE_PG_SEC 8

/* Small mapping always follows this granularity */
#define ZTL_MPE_CPGS 256

/* Set ZTL_WRITE_AFFINITY to 1 to enable thread affinity to a single core */
#define ZTL_WRITE_AFFINITY 0
#define ZTL_WRITE_CORE     0

struct ztl_queue_pool {
    pthread_spinlock_t ucmd_spin;
    STAILQ_HEAD(, xztl_io_ucmd) ucmd_head;

    /* Resource pre-alloc */
    struct xztl_mthread_ctx *tctx;
    struct xztl_io_mcmd     *mcmd[ZTL_IO_RC_NUM];
    void                    *prov;
    struct ztl_pro_node     *node;

    pthread_t w_thread;
    uint8_t   flag_running;
};

struct ztl_read_rs {
    /* Resource pre-alloc */
    struct xztl_mthread_ctx *tctx;
    struct xztl_io_mcmd     *mcmd[ZTL_IO_RC_NUM];
    char                    *prp[ZTL_IO_RC_NUM];

    bool usedflag;
};

enum xztl_mod_types {
    ZTLMOD_BAD  = 0x0,
    ZTLMOD_ZMD  = 0x1,
    ZTLMOD_PRO  = 0x2,
    ZTLMOD_MPE  = 0x3,
    ZTLMOD_MAP  = 0x4,
    ZTLMOD_LOG  = 0x5,
    ZTLMOD_REC  = 0x6,
    ZTLMOD_IO   = 0x7,
    ZTLMOD_MGMT = 0x8,
};

/* BAD (Bad Block Info) modules - NOT USED */

/* ZMD (Zone Metadata) modules */
#define LIBZTL_ZMD 0x4

/* PRO (Provisioning) modules */
#define LIBZTL_PRO 0x2

/* MGMT (Management command) modules */
#define LIBZTL_MGMT 0x2

/* THD (Thread resource) modules */
#define LIBZTL_THD 0x2

/* MPE (Persistent Mapping) modules */
#define LIBZTL_MPE 0x4

/* MAP (Mapping) modules */
#define LIBZTL_MAP 0x2

/* WCA (Write-cache) modules */
#define LIBZTL_IO 0x3

/* LOG (Write-ahead logging) modules */
#define LIBZTL_LOG 0x2

/* REC (Recovery) modules */
#define LIBZTL_REC 0x2

enum xztl_gl_functions { XZTL_FN_GLOBAL = 0, XZTL_FN_TRANSACTION = 1 };

enum xztl_zmd_flags {
    XZTL_ZMD_USED = (1 << 0),
    XZTL_ZMD_OPEN = (1 << 1),
    XZTL_ZMD_RSVD = (1 << 2), /* Reserved zone */
    XZTL_ZMD_AVLB = (1 << 3), /* Indicates the zone is valid and can be used */
    XZTL_ZMD_COLD = (1 << 4), /* Contains cold data recycled by GC */
    XZTL_ZMD_META = (1 << 5)  /* Contains metadata, such as log for recovery*/
};

enum xztl_zmd_node_status {
    XZTL_ZMD_NODE_FREE = 0,
    XZTL_ZMD_NODE_USED,
    XZTL_ZMD_NODE_FULL
};

struct app_zmd_entry {
    uint16_t flags;
    uint16_t level; /* Used to define user level metadata to the zone */
    struct xztl_maddr addr;
    uint64_t          wptr;
    uint64_t wptr_inflight; /* In-flight writing LBAs (not completed yet) */

    /* If we implement recovery at the ZTL, we need to decide how to store
     * mapping pieces information here as a list */
    /* INFO: Make this struct packed if we flush to flash */
};

struct app_map_entry {
    union {
        struct {
            /* Media offset */
            /* 4KB  sector: Max capacity: 4PB
             * 512b sector: Max capacity: 512TB */
            uint64_t offset : 40;

            /* Number of sectors */
            /* 4KB  sector: Max entry size: 32GB
             * 512b sector: Max entry size: 4GB */
            uint64_t nsec : 23;

            /* Multi-piece mapping bit */
            uint64_t multi : 1;
        } g;

        uint64_t addr;
    };
}; /* 8 bytes entry */

struct app_mpe {
    struct app_magic byte;
    uint32_t         entries;
    uint32_t         entry_sz;

    uint8_t            *tbl;
    uint32_t            ent_per_pg;
    struct app_tiny_tbl tiny; /* This is the 'tiny' table for checkpoint */
    pthread_mutex_t    *entry_mutex;
} __attribute__((packed));

struct map_md_addr {
    union {
        struct {
            uint64_t addr : 63;
            uint64_t flag : 1;
        } g;
        uint64_t addr;
    };
};

/* ------- xZTL: MODULE FUNCTIONS DEFINITION ------- */
typedef int(app_zmd_create)(struct app_group *grp);
typedef int(app_zmd_flush)(struct app_group *grp);
typedef int(app_zmd_load)(struct app_group *grp);
typedef void(app_zmd_mark)(struct app_group *lgrp, uint64_t index);
typedef struct app_zmd_entry *(app_zmd_get)(struct app_group *grp,
                                            uint64_t zone, uint8_t by_offset);
typedef void(app_zmd_invalidate)(struct app_group *grp, struct xztl_maddr *addr,
                                 uint8_t full);

typedef int(app_pro_init)(void);
typedef void(app_pro_exit)(void);
typedef int(app_pro_new)(uint32_t nsec, int32_t node_id,
                         struct app_pro_addr *ctx, uint32_t start);
typedef void(app_pro_free)(struct app_pro_addr *ctx);
typedef int(app_pro_get_node)(struct ztl_queue_pool *q);

typedef int(app_grp_init)(void);
typedef void(app_grp_exit)(void);
typedef struct app_group *(app_grp_get)(uint16_t gid);
typedef int(app_grp_get_list)(struct app_group **lgrp, uint16_t ngrps);

typedef int(app_mpe_create)(void);
typedef int(app_mpe_load)(void);
typedef int(app_mpe_flush)(void);
typedef void(app_mpe_mark)(uint32_t index);
typedef struct map_md_addr *(app_mpe_get)(uint32_t index);

typedef int(app_map_init)(void);
typedef void(app_map_exit)(void);
typedef void(app_map_persist)(void);
typedef int(app_map_upsert)(uint64_t id, uint64_t addr, uint64_t *old,
                            uint64_t old_caller);
typedef uint64_t(app_map_read)(uint64_t id);
typedef int(app_map_upsert_md)(uint64_t index, uint64_t addr,
                               uint64_t old_addr);

typedef int(app_io_init)(void);
typedef void(app_io_exit)(void);
typedef void(app_io_submit)(struct xztl_io_ucmd *ucmd);
typedef int(app_io_read)(struct xztl_io_ucmd *ucmd);
typedef void(app_io_nodeset)(int32_t node_id, int32_t level, int32_t num);

typedef int(app_mgmt_init)(void);
typedef void(app_mgmt_exit)(void);
typedef int(app_mgmt_reset)(struct app_group *grp, struct ztl_pro_node *node,
                            int32_t op_code);
typedef int(app_mgmt_finish)(struct app_group *grp, struct ztl_pro_node *node,
                             int32_t op_code);

typedef void(app_mgmt_clear_invalid_node)(struct app_group *grp);

struct app_zmd_mod {
    uint8_t             mod_id;
    char               *name;
    app_zmd_create     *create_fn;
    app_zmd_flush      *flush_fn;
    app_zmd_load       *load_fn;
    app_zmd_get        *get_fn;
    app_zmd_invalidate *invalidate_fn;
    app_zmd_mark       *mark_fn;
};

struct app_pro_mod {
    uint8_t           mod_id;
    char             *name;
    app_pro_init     *init_fn;
    app_pro_exit     *exit_fn;
    app_pro_new      *new_fn;
    app_pro_free     *free_fn;
    app_pro_get_node *get_node_fn;
};

struct app_mpe_mod {
    uint8_t         mod_id;
    char           *name;
    app_mpe_create *create_fn;
    app_mpe_load   *load_fn;
    app_mpe_flush  *flush_fn;
    app_mpe_mark   *mark_fn;
    app_mpe_get    *get_fn;
};

struct app_map_mod {
    uint8_t            mod_id;
    char              *name;
    app_map_init      *init_fn;
    app_map_exit      *exit_fn;
    app_map_persist   *persist_fn;
    app_map_upsert    *upsert_fn;
    app_map_read      *read_fn;
    app_map_upsert_md *upsert_md_fn;
};

struct app_io_mod {
    uint8_t         mod_id;
    char           *name;
    app_io_init    *init_fn;
    app_io_exit    *exit_fn;
    app_io_submit  *submit_fn;
    app_io_read    *read_fn;
    app_io_nodeset *nodeset_fn;
};

struct app_mgmt_mod {
    uint8_t        mod_id;
    char          *name;
    app_mgmt_init *init_fn;
    app_mgmt_exit *exit_fn;
    // app_mgmt_open*    open_fn;
    // app_mgmt_close*   close_fn;
    app_mgmt_reset              *reset_fn;
    app_mgmt_finish             *finish_fn;
    app_mgmt_clear_invalid_node *clear_fn;
};

struct app_groups {
    app_grp_init     *init_fn;
    app_grp_exit     *exit_fn;
    app_grp_get      *get_fn;
    app_grp_get_list *get_list_fn;
};

struct app_global {
    struct app_groups groups;
    struct app_mpe    smap;

    void                *mod_list[APP_MOD_COUNT][APP_FN_SLOTS];
    struct app_zmd_mod  *zmd;
    struct app_pro_mod  *pro;
    struct app_mpe_mod  *mpe;
    struct app_map_mod  *map;
    struct app_io_mod   *io;
    struct app_mgmt_mod *mgmt;
};

/* ZTL core functions */

struct app_global *ztl(void);
int                ztl_mod_register(uint8_t modtype, uint8_t id, void *mod);
int                ztl_mod_set(uint8_t *modset);
int                ztl_init(void);
void               ztl_exit(void);

/* LIBZTL module registration */

void ztl_grp_register(void);
void ztl_zmd_register(void);
void ztl_mpe_register(void);
void ztl_map_register(void);
void ztl_io_register(void);
void ztl_pro_register(void);
void ztl_mgmt_register(void);

#endif /* XZTL_MODS_H */
