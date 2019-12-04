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

#include <stdlib.h>
#include <pthread.h>
#include <xapp.h>
#include <xapp-media.h>
#include <xapp-ztl.h>
#include <xapp-mempool.h>
#include <ztl-media.h>
#include <libzrocks.h>
#include <libxnvme.h>

#define ZROCKS_DEBUG 		0
#define ZROCKS_BUF_ENTS 	128
#define ZROCKS_MAX_READ_SZ	(128 * 4096) /* 512KB */

/* Remove this lock if we find a way to get a thread ID starting from 0 */
static pthread_spinlock_t zrocks_mp_spin;

void *zrocks_alloc (size_t size)
{
    uint64_t phys;
    return xapp_media_dma_alloc (size, &phys);
}

void zrocks_free (void *ptr)
{
    xapp_media_dma_free (ptr);
}

static int __zrocks_write (struct xapp_io_ucmd *ucmd,
			uint64_t id, void *buf, uint32_t size, uint8_t level)
{
    /* For now, we only support level 0 */
    ucmd->prov_type = 0/*level*/;

    ucmd->id        = id;
    ucmd->buf       = buf;
    ucmd->size      = size;
    ucmd->status    = 0;
    ucmd->completed = 0;
    ucmd->callback  = NULL;
    ucmd->prov      = NULL;

    if (ztl()->wca->submit_fn (ucmd))
	return -1;

    /* Wait for asynchronous command */
    while (!ucmd->completed) {
	usleep (1);
    }

    return 0;
}

int zrocks_new (uint64_t id, void *buf, uint32_t size, uint8_t level)
{
    struct xapp_io_ucmd ucmd;
    int ret;

    ucmd.app_md = 0;
    ret = __zrocks_write (&ucmd, id, buf, size, level);

    return (!ret) ? ucmd.status : ret;
}

int zrocks_write (void *buf, uint32_t size, uint8_t level,
				    struct zrocks_map **map, uint16_t *pieces)
{
    struct xapp_io_ucmd ucmd;
    struct zrocks_map *list;
    int ret, off_i;

    ucmd.app_md = 1;
    ret = __zrocks_write (&ucmd, 0, buf, size, level);

    if (ret)
	return ret;

    if (ucmd.status)
	return ucmd.status;

    list = zrocks_alloc (sizeof(struct zrocks_map) * ucmd.noffs);
    if (!list)
	return -1;

    for (off_i = 0; off_i < ucmd.noffs; off_i++) {
	list[off_i].g.offset = (uint64_t) ucmd.moffset[off_i];
	list[off_i].g.nsec   = ucmd.msec[off_i];
	list[off_i].g.multi  = 1;
    }

    *map = list;
    *pieces = ucmd.noffs;

    return 0;
}

int zrocks_read_obj (uint64_t id, uint64_t offset, void *buf, uint32_t size)
{
    struct xapp_io_mcmd cmd;
    uint64_t objsec_off, usersec_off;
    struct xapp_mp_entry *mp_entry;
    int ret;

    if (ZROCKS_DEBUG)
	log_infoa ("zrocks (read): ID %lu, off %lu, size %d\n",
							id, offset, size);

    /* Get I/O buffer from mempool */
    pthread_spin_lock (&zrocks_mp_spin);
    mp_entry = xapp_mempool_get (ZROCKS_MEMORY, 0);
    if (!mp_entry) {
	pthread_spin_unlock (&zrocks_mp_spin);
	return -1;
    }
    pthread_spin_unlock (&zrocks_mp_spin);

    cmd.opcode  = XAPP_CMD_READ;
    cmd.naddr   = 1;
    cmd.synch   = 1;
    cmd.prp[0]  = (uint64_t) mp_entry->opaque;
    cmd.nsec[0] = size / ZNS_ALIGMENT + 1;
    cmd.status  = 0;

    if (size % ZNS_ALIGMENT != 0)
	cmd.nsec[0] += 1;

    /* This assumes a single zone offset per object */
    usersec_off = offset / ZNS_ALIGMENT;
    objsec_off  = ztl()->map->read_fn (id);

    /* Add a sector in case if read cross sector boundary */
    if (objsec_off + size >
	    ( (objsec_off / ZNS_ALIGMENT) + (cmd.nsec[0]) ) * ZNS_ALIGMENT)
	cmd.nsec[0] += 1;

    cmd.addr[0].addr = 0;
    cmd.addr[0].g.sect = objsec_off + usersec_off;

    if (ZROCKS_DEBUG)
	log_infoa ("  objsec_off %lx, usersec_off %lu, nsec %lu\n",
    				    objsec_off, usersec_off, cmd.nsec[0]);

    /* Maximum read size is 512 KB (128 sectors)
     * Multiple media commands are necessary for larger reads */
    if (cmd.nsec[0] * ZNS_ALIGMENT > ZROCKS_MAX_READ_SZ)
	return -1;

    ret = xapp_media_submit_io (&cmd);
    if (ret || cmd.status) {
	log_erra ("zrocks: Read failure. ID %lu, off 0x%lx, sz %d. ret %d, cmd.status %d",
						    id, offset, size, ret, cmd.status);
    } else {
	/* If I/O succeeded, we copy the data from the correct offset to the user */
	memcpy (buf, (char *) mp_entry->opaque + (offset % ZNS_ALIGMENT), size);
    }

    pthread_spin_lock (&zrocks_mp_spin);
    xapp_mempool_put (mp_entry, ZROCKS_MEMORY, 0);
    pthread_spin_unlock (&zrocks_mp_spin);

    return (!ret) ? cmd.status : ret;
}



int zrocks_read (uint64_t offset, void *buf, uint64_t size)
{
    // TODO:
    // read directly from device
    return 0;
}

int zrocks_delete (uint64_t id)
{
    uint64_t old;

    return ztl()->map->upsert_fn (id, 0, &old, 0);
}

int zrocks_exit (void)
{
    pthread_spin_destroy (&zrocks_mp_spin);
    xapp_mempool_destroy (ZROCKS_MEMORY, 0);
    return xapp_exit ();
}

int zrocks_init (const char *dev_name)
{
    int ret;

    /* Add libznd media layer */
    xapp_add_media (znd_media_register);

    /* Add the ZTL modules */
    ztl_zmd_register ();
    ztl_pro_register ();
    ztl_mpe_register ();
    ztl_map_register ();
    ztl_wca_register ();

    if (pthread_spin_init (&zrocks_mp_spin, 0))
	return -1;

    ret = xapp_init (dev_name);
    if (ret) {
	pthread_spin_destroy (&zrocks_mp_spin);
	return -1;
    }

    if (xapp_mempool_create (ZROCKS_MEMORY,
			     0,
			     ZROCKS_BUF_ENTS,
			     ZROCKS_MAX_READ_SZ + ZNS_ALIGMENT,
			     zrocks_alloc,
			     zrocks_free)) {
	xapp_exit ();
	pthread_spin_destroy (&zrocks_mp_spin);
    }

    return ret;
}
