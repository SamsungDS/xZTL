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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xztl.h>
#include <xztl-mods.h>
#define DATA_LEN 512 * 256 * 8

// extern uint16_t app_ngrps;
static uint8_t         app_map_new;
static struct app_mpe *smap;

static int ztl_mpe_create(void) {
    smap = &ztl()->smap;
    int                   i;
    struct app_map_entry *ent;

    for (i = 0; i < smap->entries; i++) {
        ent = ((struct app_map_entry *)smap->tbl) + i;
        memset(ent, 0x0, sizeof(struct app_map_entry));
    }

    app_map_new = 1;

    return XZTL_OK;
}

static int ztl_mpe_load(void) {
    return XZTL_OK;
}

static int ztl_mpe_flush(void) {
    return XZTL_OK;
}

static struct map_md_addr *ztl_mpe_get(uint32_t index) {
    /* TODO: In case we implement recovery at the ZTL:
     *       If index > n_entries, increase size of table */

    return ((struct map_md_addr *)smap->tbl) + index;
}

static void ztl_mpe_mark(uint32_t index) {
}

static struct app_mpe_mod ztl_mpe = {.mod_id    = LIBZTL_ZMD,
                                     .name      = "LIBZTL-ZMD",
                                     .create_fn = ztl_mpe_create,
                                     .flush_fn  = ztl_mpe_flush,
                                     .load_fn   = ztl_mpe_load,
                                     .get_fn    = ztl_mpe_get,
                                     .mark_fn   = ztl_mpe_mark};

void ztl_mpe_register(void) {
    ztl_mod_register(ZTLMOD_MPE, LIBZTL_MPE, &ztl_mpe);
}
