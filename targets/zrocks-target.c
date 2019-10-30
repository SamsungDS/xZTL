#include <xapp.h>
#include <xapp-media.h>
#include <xapp-ztl.h>
#include <ztl-media.h>

void *zrocks_alloc (uint32_t size)
{
    // allocate memory from device
    return NULL;
}

void zrocks_free (void *ptr)
{
    // free memory from device
}

int zrocks_write (void *buf, uint32_t size, uint8_t level, uint64_t *addr)
{
    // add parameter for level in struct ucmd
    // create ucmd
    // populate ucmd
    // set app_md to 1
    // wait until completed == ncmd
    // populate *addr
    // return
    return 0;
}

int zrocks_new (uint64_t id, void *buf, uint32_t size, uint8_t level)
{
    // create ucmd
    // populate ucmd
    // wait until completed == ncmd
    // return
    return 0;
}

int zrocks_delete (uint64_t id)
{
    // only used for ztl managed mapping
    // invalidate object offsets
    return 0;
}

int zrocks_read_obj (uint64_t id, uint64_t offset, void *buf, uint32_t size)
{
    struct xapp_io_mcmd cmd;
    uint64_t objoff;
    int ret;

    cmd.opcode  = XAPP_CMD_READ;
    cmd.synch   = 1;
    cmd.prp[0]  = (uint64_t) buf;
    cmd.nlba[0] = size;

    /* This assumes a single zone offset per object */
    objoff = ztl()->map->read_fn (id);
    cmd.addr[0].g.sect = objoff + offset;

    ret = xapp_media_submit_io (&cmd);
    if (ret || cmd.status)
	log_erra ("zrocks: Read failure. ID %lu, off 0x%lx, sz %d\n",
						    id, offset, size);

    return (!ret) ? cmd.status : ret;
}

int zrocks_read (uint64_t offset, void *buf, uint32_t size)
{
    // read directly from device
    return 0;
}

int zrocks_exit (void)
{
    return xapp_exit ();
}

int zrocks_init (void)
{
    /* Add libznd media layer */
    xapp_add_media (znd_media_register);

    /* Add the ZTL modules */
    ztl_zmd_register ();
    ztl_pro_register ();
    ztl_mpe_register ();
    ztl_map_register ();
    ztl_wca_register ();

    return xapp_init ();
}
