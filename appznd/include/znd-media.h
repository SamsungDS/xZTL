#ifndef ZNMEDIA
#define ZNMEDIA

#include <libxnvme.h>
#include <xapp.h>

enum znd_media_error {
    ZND_MEDIA_NODEVICE 	= 0x1,
    ZND_MEDIA_NOGEO	= 0x2,
};

struct znd_media {
    struct xnvme_dev 	   *dev;
    const struct xnvme_geo *devgeo;
    struct xapp_media 	    media;
};

/* Registration function */
int znd_media_register (void);

#endif /* ZNMEDIA */
