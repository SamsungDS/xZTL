#ifndef XAPP_MEDIA
#define XAPP_MEDIA

typedef int (xapp_init_fn) (void);
typedef int (xapp_exit_fn) (void);
typedef int (xapp_register_fn) (void);

#include <xapp-media.h>

enum xapp_status {
    XAPP_OK		= 0x0,
    XAPP_MEM		= 0x1,
    XAPP_NOMEDIA	= 0x2,
    XAPP_NOINIT		= 0x3,
    XAPP_NOEXIT 	= 0x4,
    XAPP_MEDIA_NOGEO	= 0x5,
    XAPP_MEDIA_GEO	= 0x6,
    XAPP_MEDIA_NOIO	= 0x7,
    XAPP_MEDIA_NOZONE	= 0x8,
    XAPP_MEDIA_NOALLOC	= 0x9,
    XAPP_MEDIA_NOFREE	= 0xa,

    XAPP_MEDIA_ERROR	= 0x100,
};

struct xapp_core {
    struct xapp_media *media;
};

/* Add media layer */
void xapp_add_media (xapp_register_fn *fn);

/* Set the media abstraction */
int xapp_media_set (struct xapp_media *media);

/* Initialize XApp instance */
int xapp_init (void);

/* Safe shut down */
int xapp_exit (void);

/* Media functions */
int xapp_media_submit_zn (struct xapp_zn_mcmd *cmd);

/* Layer specific functions (for testing) */
int xapp_media_init (void);
int xapp_media_exit (void);

#endif /* XAPP_MEDIA */
