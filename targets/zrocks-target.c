#include <xapp.h>
#include <xapp-media.h>
#include <znd-media.h>

int zrocks_exit (void)
{
    return xapp_exit ();
}

int zrocks_init (void)
{
    /* Add libznd media layer */
    xapp_add_media (znd_media_register);

    return xapp_init ();
}
