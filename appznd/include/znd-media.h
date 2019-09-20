#ifndef ZNMEDIA
#define ZNMEDIA

#include <libxnvme.h>
#include <xapp.h>

enum zn_media_error {
    ZN_MEDIA_NODEVICE 	= 0x1,
    ZN_MEDIA_NOGEO	= 0x2,
};

struct zn_media {
    struct xnvme_dev 	   *dev;
    const struct xnvme_geo *devgeo;
    struct xapp_media 	    media;
};

/* Registration function */
int zn_media_register (void);

#endif /* ZNMEDIA */
