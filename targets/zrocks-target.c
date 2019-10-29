#include <xapp.h>
#include <xapp-media.h>
#include <xapp-ztl.h>
#include <ztl-media.h>

void *zrocks_alloc (uint32_t size)
{
    return NULL;
}

void zrocks_free (void *ptr)
{

}

int zrocks_new (uint64_t id, void *buf, uint32_t size, uint8_t level)
{
    return 0;
}

int zrocks_delete (uint64_t id)
{
    return 0;
}

int zrocks_read (uint64_t id, uint32_t offset, void *buf, uint32_t size)
{
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

    return xapp_init ();
}
