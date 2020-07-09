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

#ifndef ZNMEDIA
#define ZNMEDIA

#include <libxnvme.h>
#include <xztl.h>
#include <xztl-media.h>

enum znd_media_error {
    ZND_MEDIA_NODEVICE 	 = 0x1,
    ZND_MEDIA_NOGEO	 = 0x2,
    ZND_INVALID_OPCODE	 = 0x3,
    ZND_MEDIA_REPORT_ERR = 0x4,
    ZND_MEDIA_OPEN_ERR	 = 0x5,
    ZND_MEDIA_ASYNCH_ERR = 0x6,
    ZND_MEDIA_ASYNCH_MEM = 0x7,
    ZND_MEDIA_ASYNCH_TH  = 0x8,
    ZND_MEDIA_POKE_ERR	 = 0x9,
    ZND_MEDIA_OUTS_ERR   = 0xa,
    ZND_MEDIA_WAIT_ERR   = 0xb
};

struct znd_media {
    struct xnvme_dev 	   *dev;
    const struct xnvme_geo *devgeo;
    struct xztl_media 	    media;
};

struct znd_log_cmd {
    uint8_t  opcode;
    uint8_t  rsv[7];
    void     *buf;
    uint64_t lba;
    uint32_t nlogs;

};

/* Registration function */
int znd_media_register (const char *dev_name);

#endif /* ZNMEDIA */
