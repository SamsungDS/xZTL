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

#ifndef XAPP_MEDIA_H
#define XAPP_MEDIA_H

#include <stdint.h>
#include <libxnvme.h>
#include <stdlib.h>
#include <xapp.h>

/* Append Command support */
#define XAPP_WRITE_APPEND 0

/* A single address is needed for zone append. We should increase
 * this number in case of possible vectored I/Os */
#define XAPP_MAX_MADDR  1

#define XAPP_MCTX_SZ	640

#define XAPP_MEDIA_MAX_GRP    128	/* groups */
#define XAPP_MEDIA_MAX_PUGRP  8		/* punits */
#define XAPP_MEDIA_MAX_ZNPU   8388608	/* zones */
#define XAPP_MEDIA_MAX_SECZN  8388608	/* sectors */
#define XAPP_MEDIA_MAX_SECSZ  1048576	/* bytes */
#define XAPP_MEDIA_MAX_OOBSZ  128	/* bytes */

enum xapp_media_opcodes {
    /* Admin commands */
    XAPP_ADM_IDENTIFY   = 0x06,
    XAPP_ADM_IDFY_OCSSD = 0xE2,
    XAPP_ADM_GET_LOG	= 0x02,
    XAPP_ADM_SET_FEAT	= 0x09,
    XAPP_ADM_GET_FEAT	= 0x0A,
    XAPP_ADM_FORMAT_NVM = 0x80,
    XAPP_ADM_SANITIZE   = 0x84,

    /* I/O commands */
    XAPP_CMD_WRITE 	 = 0x01,
    XAPP_CMD_READ  	 = 0x02,
    XAPP_CMD_WRITE_OCSSD = 0x91,
    XAPP_CMD_READ_OCSSD  = 0x92,

    /* Zoned commands */
    XAPP_ZONE_MGMT 	 = 0x79,
    XAPP_ZONE_APPEND     = 0x7D,

    /* Zone Management actions */
    XAPP_ZONE_MGMT_CLOSE  = 0x1,
    XAPP_ZONE_MGMT_FINISH = 0x2,
    XAPP_ZONE_MGMT_OPEN	  = 0x3,
    XAPP_ZONE_MGMT_RESET  = 0x4,
    XAPP_ZONE_MGMT_REPORT = 0xf,
    XAPP_ZONE_ERASE_OCSSD = 0x90,

    /* Media other commands */
    XAPP_MISC_ASYNCH_INIT = 0x1,
    XAPP_MISC_ASYNCH_TERM = 0x2,
    XAPP_MISC_ASYNCH_POKE = 0x3,
    XAPP_MISC_ASYNCH_OUTS = 0x4,
    XAPP_MISC_ASYNCH_WAIT = 0x5
};

struct xapp_mthread_ctx {
    uint16_t 	    tid;
    xapp_thread    *comp_th;
    pthread_t       comp_tid;
    int             comp_active;
    pthread_spinlock_t       qpair_spin;
    struct xnvme_async_ctx *asynch;
};

/* Structure aligned to 8 bytes */
struct xapp_io_mcmd {
     uint8_t		opcode;
     uint8_t		submitted;
     uint8_t		synch;
     uint32_t 		sequence;
     uint32_t		sequence_zn;
     uint32_t 	      	naddr;
     uint16_t 		status;
     uint64_t 		nsec[XAPP_MAX_MADDR];
     struct xapp_maddr 	addr[XAPP_MAX_MADDR];
     uint64_t		prp[XAPP_MAX_MADDR];
     uint64_t 		paddr[XAPP_MAX_MADDR];
     xapp_callback     *callback;
     void	       *opaque;
     struct xapp_mthread_ctx *async_ctx;
     struct xapp_mp_entry    *mp_cmd;

    /* For latency */
     uint64_t us_start;
     uint64_t us_end;

     /* change to pointer when xnvme is updated */
     struct xnvme_req media_ctx;
};

struct xapp_zn_mcmd {
    uint8_t		 opcode;
    uint8_t 		 status;
    struct xapp_maddr	 addr;
    uint32_t 		 nzones;
    void		*opaque;
};

struct xapp_misc_cmd {
    uint8_t  opcode;
    uint8_t  rsv[7];

    union {
	uint64_t rsv2[7];

	struct {
	    struct xapp_mthread_ctx *ctx_ptr;

	    uint32_t depth;	   /* Context/queue depth */
	    uint32_t limit;	   /* Max number of completions */
	    uint32_t count;	   /* Processed completions */

	    uint32_t rsv31;
	    uint64_t rsv32[4];
	} asynch;
    };
};

struct xapp_mgeo {
    uint32_t	ngrps;      /* Groups */
    uint32_t	pu_grp;     /* PUs per group */
    uint32_t	zn_pu;      /* Zones per PU */
    uint32_t	sec_zn;	    /* Sectors per zone */
    uint32_t	nbytes;	    /* Per sector */
    uint32_t	nbytes_oob; /* Per sector */

    /* Calculated values */
    uint32_t    zn_dev;     /* Total zones in device */
    uint32_t	zn_grp;	    /* Zones per group */
    uint32_t    sec_dev;    /* Toatl sectors in device */
    uint32_t	sec_grp;    /* Sectors per group */
    uint32_t	sec_pu;     /* Sectors per PU */
    uint64_t    nbytes_zn;  /* Bytes per zone */
    uint64_t 	nbytes_grp; /* Bytes per group */
    uint32_t	oob_grp;    /* OOB size per group */
    uint32_t	oob_pu;     /* OOB size per PU */
    uint32_t	oob_zn;     /* OOB size per zone */
};

typedef int   (xapp_media_io_fn)        (struct xapp_io_mcmd *cmd);
typedef int   (xapp_media_zn_fn)        (struct xapp_zn_mcmd *cmd);
typedef void *(xapp_media_dma_alloc_fn) (size_t size, uint64_t *phys);
typedef void  (xapp_media_dma_free_fn)  (void *ptr);
typedef int   (xapp_media_cmd_fn)	(struct xapp_misc_cmd *cmd);

struct xapp_media {
    struct xapp_mgeo         geo;
    xapp_init_fn	    *init_fn;
    xapp_exit_fn	    *exit_fn;
    xapp_media_io_fn	    *submit_io;
    xapp_media_zn_fn	    *zone_fn;
    xapp_media_dma_alloc_fn *dma_alloc;
    xapp_media_dma_free_fn  *dma_free;
    xapp_media_cmd_fn       *cmd_exec;
};

#endif /* XAPP_MEDIA_H */
