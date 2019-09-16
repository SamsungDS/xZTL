#ifndef XAPP
#define XAPP

#include <stdint.h>
#include <xapp.h>

#define XAPP_MAX_MADDR 64

#define XAPP_MEDIA_MAX_GRP    128	/* groups */
#define XAPP_MEDIA_MAX_PUGRP  8		/* punits */
#define XAPP_MEDIA_MAX_ZNPU   8388608	/* zones */
#define XAPP_MEDIA_MAX_SECZN  8388608	/* sectors */
#define XAPP_MEDIA_MAX_SECSZ  1048576	/* bytes */
#define XAPP_MEDIA_MAX_OOBSZ  128	/* bytes */

struct xapp_mgeo {
    uint32_t	ngrps;
    uint32_t	pu_grp;
    uint32_t	zn_pu;
    uint32_t	sec_zn;
    uint32_t	sec_sz;
    uint32_t	oob_sz; /* Per sector */

    /* Calculated values */
    uint32_t	zn_grp;
    uint32_t	sec_grp;
    uint32_t	sec_pu;
    uint32_t	oob_grp;
    uint32_t	oob_pu;
    uint32_t	oob_zn;
};

struct xapp_media {
    struct xapp_mgeo 	geo;
    xapp_init_fn	*init_fn;
    xapp_exit_fn	*exit_fn;
};

struct xapp_baddr {
    union {
	struct {
	    uint64_t grp;
	    uint64_t punit;
	    uint64_t zone;
	    uint64_t sect;
	} g;
	uint64_t addr;
    };
};

struct xapp_mcmd {
     uint32_t 	      	naddr;
     struct xapp_baddr 	addr[XAPP_MAX_MADDR];
     uint64_t		prp[XAPP_MAX_MADDR];
};

#endif /* XAPP */
