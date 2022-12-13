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

#include <libxnvme_spec.h>
#include <libxnvme_znd.h>
#include <pthread.h>
#include <unistd.h>
#include <xztl-media.h>
#include <xztl-mempool.h>
#include <xztl.h>

#include "CUnit/Basic.h"

static const char **devname;

static void cunit_znd_assert_ptr(char *fn, void *ptr) {
    CU_ASSERT((uint64_t)ptr != 0);
    if (!ptr)
        printf("\n %s: ptr %p\n", fn, ptr);
}

static void cunit_znd_assert_int(char *fn, uint64_t status) {
    CU_ASSERT(status == 0);
    if (status)
        printf("\n %s: %lx\n", fn, status);
}

static void cunit_znd_assert_int_equal(char *fn, int value, int expected) {
    CU_ASSERT_EQUAL(value, expected);
    if (value != expected)
        printf("\n %s: value %d != expected %d\n", fn, value, expected);
}

static void cunit_znd_assert_string_equal(char *fn, char *actual,
                                          char *expected) {
    CU_ASSERT_STRING_EQUAL(actual, expected);
    if (strcmp(actual, expected))
        printf("\n %s: actual %s != expected %s\n", fn, actual, expected);
}

static int cunit_znd_media_init(void) {
    return 0;
}

static int cunit_znd_media_exit(void) {
    return 0;
}

static void test_znd_opt_parse(void) {
    char                dev_name[MAX_BUF_LEN] = {0};
    struct znd_opt_info opt_info;

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/ng2n", sizeof(dev_name));
    cunit_znd_assert_int_equal("znd_opt_parse too short param",
                               znd_opt_parse(dev_name, &opt_info),
                               ZND_MEDIA_NODEVICE);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name,
            "/dev/ng2n1/dev/ng2n1/dev/ng2n1/dev/ng2n1/dev/ng2n1/dev"
            "/ng2n1/dev/ng2n1",
            sizeof(dev_name));
    cunit_znd_assert_int_equal("znd_opt_parse too long param",
                               znd_opt_parse(dev_name, &opt_info),
                               ZND_MEDIA_NODEVICE);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "xdev/ng2n1", sizeof(dev_name));
    cunit_znd_assert_int_equal("znd_opt_parse err param first char",
                               znd_opt_parse(dev_name, &opt_info),
                               ZND_MEDIA_NODEVICE);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/nx2n1", sizeof(dev_name));
    cunit_znd_assert_int_equal("znd_opt_parse err param sixth char",
                               znd_opt_parse(dev_name, &opt_info),
                               ZND_MEDIA_NODEVICE);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/ng2n1?", sizeof(dev_name));
    cunit_znd_assert_int_equal("znd_opt_parse err ? param",
                               znd_opt_parse(dev_name, &opt_info),
                               ZND_MEDIA_NODEVICE);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/ng2n1?be=", sizeof(dev_name));
    cunit_znd_assert_int_equal("znd_opt_parse err null be",
                               znd_opt_parse(dev_name, &opt_info),
                               ZND_MEDIA_NODEVICE);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/ng2n1?be=x", sizeof(dev_name));
    cunit_znd_assert_int_equal("znd_opt_parse err be name",
                               znd_opt_parse(dev_name, &opt_info),
                               ZND_MEDIA_NODEVICE);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/ng2n1", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    cunit_znd_assert_int_equal("opt_device_type", opt_info.opt_dev_type,
                               OPT_DEV_CHAR);
    cunit_znd_assert_string_equal("opt_dev_name", opt_info.opt_dev_name,
                                  "/dev/ng2n1");
    cunit_znd_assert_int_equal("opt_async", opt_info.opt_async, OPT_BE_THRPOOL);
    cunit_znd_assert_int_equal("opt_nsid", opt_info.opt_nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/ng2n1?be=thrpool", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    cunit_znd_assert_int_equal("opt_device_type", opt_info.opt_dev_type,
                               OPT_DEV_CHAR);
    cunit_znd_assert_string_equal("opt_dev_name", opt_info.opt_dev_name,
                                  "/dev/ng2n1");
    cunit_znd_assert_int_equal("opt_async", opt_info.opt_async, OPT_BE_THRPOOL);
    cunit_znd_assert_int_equal("opt_nsid", opt_info.opt_nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/ng2n1?be=io_uring_cmd", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    cunit_znd_assert_int_equal("opt_device_type", opt_info.opt_dev_type,
                               OPT_DEV_CHAR);
    cunit_znd_assert_string_equal("opt_dev_name", opt_info.opt_dev_name,
                                  "/dev/ng2n1");
    cunit_znd_assert_int_equal("opt_async", opt_info.opt_async,
                               OPT_BE_IOURING_CMD);
    cunit_znd_assert_int_equal("opt_nsid", opt_info.opt_nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/nvme2n1", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    cunit_znd_assert_int_equal("opt_device_type", opt_info.opt_dev_type,
                               OPT_DEV_BLK);
    cunit_znd_assert_string_equal("opt_dev_name", opt_info.opt_dev_name,
                                  "/dev/nvme2n1");
    cunit_znd_assert_int_equal("opt_async", opt_info.opt_async, OPT_BE_THRPOOL);
    cunit_znd_assert_int_equal("opt_nsid", opt_info.opt_nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/nvme2n1?be=thrpool", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    cunit_znd_assert_int_equal("opt_device_type", opt_info.opt_dev_type,
                               OPT_DEV_BLK);
    cunit_znd_assert_string_equal("opt_dev_name", opt_info.opt_dev_name,
                                  "/dev/nvme2n1");
    cunit_znd_assert_int_equal("opt_async", opt_info.opt_async, OPT_BE_THRPOOL);
    cunit_znd_assert_int_equal("opt_nsid", opt_info.opt_nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/nvme2n1?be=io_uring", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    cunit_znd_assert_int_equal("opt_device_type", opt_info.opt_dev_type,
                               OPT_DEV_BLK);
    cunit_znd_assert_string_equal("opt_dev_name", opt_info.opt_dev_name,
                                  "/dev/nvme2n1");
    cunit_znd_assert_int_equal("opt_async", opt_info.opt_async, OPT_BE_IOURING);
    cunit_znd_assert_int_equal("opt_nsid", opt_info.opt_nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "/dev/nvme2n1?be=libaio", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    cunit_znd_assert_int_equal("opt_device_type", opt_info.opt_dev_type,
                               OPT_DEV_BLK);
    cunit_znd_assert_string_equal("opt_dev_name", opt_info.opt_dev_name,
                                  "/dev/nvme2n1");
    cunit_znd_assert_int_equal("opt_async", opt_info.opt_async, OPT_BE_LIBAIO);
    cunit_znd_assert_int_equal("opt_nsid", opt_info.opt_nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    strncpy(dev_name, "pci:0000:01:00.0?nsid=1", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    cunit_znd_assert_int_equal("opt_device_type", opt_info.opt_dev_type,
                               OPT_DEV_SPDK);
    cunit_znd_assert_string_equal("opt_dev_name", opt_info.opt_dev_name,
                                  "0000:01:00.0");
    cunit_znd_assert_int_equal("opt_async", opt_info.opt_async, OPT_BE_SPDK);
    cunit_znd_assert_int_equal("opt_nsid", opt_info.opt_nsid, 1);
}

static void test_znd_opt_assign(void) {
    char                dev_name[MAX_BUF_LEN] = {0};
    struct znd_opt_info opt_info;
    struct xnvme_opts   opts      = xnvme_opts_default();
    struct xnvme_opts   opts_read = xnvme_opts_default();

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    opts      = xnvme_opts_default();
    opts_read = xnvme_opts_default();
    strncpy(dev_name, "/dev/ng2n1?be=io_uring_cmd", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    znd_opt_assign(&opts, &opts_read, &opt_info);
    cunit_znd_assert_int_equal("opts.direct", opts.direct, 1);
    cunit_znd_assert_int_equal("opts.rdwr", opts.rdwr, 1);
    cunit_znd_assert_int_equal("opts.rdonly", opts.rdonly, 0);
    cunit_znd_assert_int_equal("opts.wronly", opts.wronly, 0);
    cunit_znd_assert_string_equal("opts.async", (char *)opts.async,
                                  "io_uring_cmd");
    cunit_znd_assert_int_equal("opts.be", (uint64_t)opts.be, 0);
    cunit_znd_assert_int_equal("opts.nsid", opts.nsid, 0);
    cunit_znd_assert_int_equal("opts_read.direct", opts_read.direct, 0);
    cunit_znd_assert_int_equal("opts_read.rdwr", opts_read.rdwr, 1);
    cunit_znd_assert_int_equal("opts_read.rdonly", opts_read.rdonly, 0);
    cunit_znd_assert_int_equal("opts_read.wronly", opts_read.wronly, 0);
    cunit_znd_assert_int_equal("opts_read.async", (uint64_t)opts_read.async, 0);
    cunit_znd_assert_int_equal("opts_read.be", (uint64_t)opts_read.be, 0);
    cunit_znd_assert_int_equal("opts_read.nsid", opts_read.nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    opts      = xnvme_opts_default();
    opts_read = xnvme_opts_default();
    strncpy(dev_name, "/dev/nvme2n1", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    znd_opt_assign(&opts, &opts_read, &opt_info);
    cunit_znd_assert_int_equal("opts.direct", opts.direct, 1);
    cunit_znd_assert_int_equal("opts.rdwr", opts.rdwr, 0);
    cunit_znd_assert_int_equal("opts.rdonly", opts.rdonly, 0);
    cunit_znd_assert_int_equal("opts.wronly", opts.wronly, 1);
    cunit_znd_assert_string_equal("opts.async", (char *)opts.async, "thrpool");
    cunit_znd_assert_int_equal("opts.be", (uint64_t)opts.be, 0);
    cunit_znd_assert_int_equal("opts.nsid", opts.nsid, 0);
    cunit_znd_assert_int_equal("opts_read.direct", opts_read.direct, 0);
    cunit_znd_assert_int_equal("opts_read.rdwr", opts_read.rdwr, 0);
    cunit_znd_assert_int_equal("opts_read.rdonly", opts_read.rdonly, 1);
    cunit_znd_assert_int_equal("opts_read.wronly", opts_read.wronly, 0);
    cunit_znd_assert_string_equal("opts_read.async", (char *)opts_read.async,
                                  "thrpool");
    cunit_znd_assert_int_equal("opts_read.be", (uint64_t)opts_read.be, 0);
    cunit_znd_assert_int_equal("opts_read.nsid", opts_read.nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    opts      = xnvme_opts_default();
    opts_read = xnvme_opts_default();
    strncpy(dev_name, "/dev/nvme2n1?be=thrpool", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    znd_opt_assign(&opts, &opts_read, &opt_info);
    cunit_znd_assert_int_equal("opts.direct", opts.direct, 1);
    cunit_znd_assert_int_equal("opts.rdwr", opts.rdwr, 0);
    cunit_znd_assert_int_equal("opts.rdonly", opts.rdonly, 0);
    cunit_znd_assert_int_equal("opts.wronly", opts.wronly, 1);
    cunit_znd_assert_string_equal("opts.async", (char *)opts.async, "thrpool");
    cunit_znd_assert_int_equal("opts.be", (uint64_t)opts.be, 0);
    cunit_znd_assert_int_equal("opts.nsid", opts.nsid, 0);
    cunit_znd_assert_int_equal("opts_read.direct", opts_read.direct, 0);
    cunit_znd_assert_int_equal("opts_read.rdwr", opts_read.rdwr, 0);
    cunit_znd_assert_int_equal("opts_read.rdonly", opts_read.rdonly, 1);
    cunit_znd_assert_int_equal("opts_read.wronly", opts_read.wronly, 0);
    cunit_znd_assert_string_equal("opts_read.async", (char *)opts_read.async,
                                  "thrpool");
    cunit_znd_assert_int_equal("opts_read.be", (uint64_t)opts_read.be, 0);
    cunit_znd_assert_int_equal("opts_read.nsid", opts_read.nsid, 0);

    memset(&opt_info, 0, sizeof(struct znd_opt_info));
    opts      = xnvme_opts_default();
    opts_read = xnvme_opts_default();
    strncpy(dev_name, "pci:0000:01:00.0?nsid=1", sizeof(dev_name));
    cunit_znd_assert_int("znd_opt_parse", znd_opt_parse(dev_name, &opt_info));
    znd_opt_assign(&opts, &opts_read, &opt_info);
    cunit_znd_assert_int_equal("opts.direct", opts.direct, 1);
    cunit_znd_assert_int_equal("opts.rdwr", opts.rdwr, 1);
    cunit_znd_assert_int_equal("opts.rdonly", opts.rdonly, 0);
    cunit_znd_assert_int_equal("opts.wronly", opts.wronly, 0);
    cunit_znd_assert_string_equal("opts.async", (char *)opts.async, "nvme");
    cunit_znd_assert_string_equal("opts.be", (char *)opts.be, "spdk");
    cunit_znd_assert_int_equal("opts.nsid", opts.nsid, 1);
    cunit_znd_assert_int_equal("opts_read.direct", opts_read.direct, 0);
    cunit_znd_assert_int_equal("opts_read.rdwr", opts_read.rdwr, 1);
    cunit_znd_assert_int_equal("opts_read.rdonly", opts_read.rdonly, 0);
    cunit_znd_assert_int_equal("opts_read.wronly", opts_read.wronly, 0);
    cunit_znd_assert_int_equal("opts_read.async", (uint64_t)opts_read.async, 0);
    cunit_znd_assert_int_equal("opts_read.be", (uint64_t)opts_read.be, 0);
    cunit_znd_assert_int_equal("opts_read.nsid", opts_read.nsid, 0);
}

static void test_znd_media_register(void) {
    cunit_znd_assert_int("znd_media_register", znd_media_register(*devname));
}

static void test_znd_media_init(void) {
    cunit_znd_assert_int("xztl_media_init", xztl_media_init());
}

static void test_znd_media_exit(void) {
    cunit_znd_assert_int("xztl_media_exit", xztl_media_exit());
}

static void test_znd_report(void) {
    struct xztl_zn_mcmd          cmd;
    struct xnvme_znd_report     *report;
    struct xnvme_spec_znd_descr *zinfo;
    int                          ret, zi, zone, nzones;
    uint32_t                     znlbas;
    struct xztl_core            *core;
    get_xztl_core(&core);

    zone            = 0;
    cmd.opcode      = XZTL_ZONE_MGMT_REPORT;
    cmd.addr.g.zone = 0;
    cmd.nzones = nzones = core->media->geo.zn_dev;

    ret = xztl_media_submit_zn(&cmd);
    cunit_znd_assert_int("xztl_media_submit_zn:report", ret);

    if (!ret) {
        znlbas = core->media->geo.sec_zn;
        for (zi = zone; zi < zone + nzones; zi++) {
            report = (struct xnvme_znd_report *)cmd.opaque;
            zinfo  = XNVME_ZND_REPORT_DESCR(report, zi);
            cunit_znd_assert_int_equal("xztl_media_submit_zn:report:szlba",
                                       zinfo->zslba, zi * znlbas);
        }
    }

    /* Free structure allocated by xnvme */
    xnvme_buf_virt_free(cmd.opaque);
}

static void test_znd_manage_single(uint8_t op, uint8_t devop, uint32_t zone,
                                   char *name) {
    struct xztl_zn_mcmd          cmd;
    struct xnvme_znd_report     *report;
    struct xnvme_spec_znd_descr *zinfo;
    int                          ret;

    cmd.opcode      = op;
    cmd.addr.addr   = 0;
    cmd.addr.g.zone = zone;

    ret = xztl_media_submit_zn(&cmd);
    cunit_znd_assert_int(name, ret);

    /* Verify log page */
    cmd.opcode = XZTL_ZONE_MGMT_REPORT;
    cmd.nzones = 1;

    ret = xztl_media_submit_zn(&cmd);
    cunit_znd_assert_int("xztl_media_submit_zn:report", cmd.status);

    if (!ret) {
        report = (struct xnvme_znd_report *)cmd.opaque;
        zinfo  = XNVME_ZND_REPORT_DESCR(report, cmd.addr.g.zone);
        cunit_znd_assert_int_equal(name, zinfo->zs, devop);
    }

    /* Free structure allocated by xnvme */
    xnvme_buf_virt_free(cmd.opaque);
}

static void test_znd_op_cl_fi_re(void) {
    uint32_t zone = 10;

    test_znd_manage_single(XZTL_ZONE_MGMT_RESET, XNVME_SPEC_ZND_STATE_EMPTY,
                           zone, "xztl_media_submit_znm:reset");
    test_znd_manage_single(XZTL_ZONE_MGMT_OPEN, XNVME_SPEC_ZND_STATE_EOPEN,
                           zone, "xztl_media_submit_znm:open");
    test_znd_manage_single(XZTL_ZONE_MGMT_CLOSE, XNVME_SPEC_ZND_STATE_CLOSED,
                           zone, "xztl_media_submit_znm:close");
    test_znd_manage_single(XZTL_ZONE_MGMT_FINISH, XNVME_SPEC_ZND_STATE_FULL,
                           zone, "xztl_media_submit_znm:finish");
    test_znd_manage_single(XZTL_ZONE_MGMT_RESET, XNVME_SPEC_ZND_STATE_EMPTY,
                           zone, "xztl_media_submit_znm:reset");
}

static void test_znd_asynch_ctx(void) {
    struct xztl_misc_cmd    cmd;
    struct xztl_mthread_ctx tctx;
    int                     ret;

    cmd.opcode       = XZTL_MISC_ASYNCH_INIT;
    cmd.asynch.depth = 128;

    /* This command takes a pointer to a pointer */
    cmd.asynch.ctx_ptr = &tctx;

    /* Create the context */
    ret = xztl_media_submit_misc(&cmd);
    cunit_znd_assert_int("xztl_media_submit_misc:asynch-init", ret);
    cunit_znd_assert_ptr("xztl_media_submit_misc:asynch-init:check",
                         tctx.queue);

    if (tctx.queue) {
        /* Get outstanding commands */
        /* cmd.asynch.count will contain the number of outstanding commands */
        cmd.opcode = XZTL_MISC_ASYNCH_OUTS;
        /* This command takes a direct pointer */
        cmd.asynch.ctx_ptr = &tctx;
        ret                = xztl_media_submit_misc(&cmd);
        cunit_znd_assert_int("xztl_media_submit_misc:asynch-outs", ret);

        /* Poke the context */
        /* cmd.asynch.count will contain the number of processed commands */
        cmd.opcode         = XZTL_MISC_ASYNCH_POKE;
        cmd.asynch.ctx_ptr = &tctx;
        cmd.asynch.limit   = 0;
        ret                = xztl_media_submit_misc(&cmd);
        cunit_znd_assert_int("xztl_media_submit_misc:asynch-poke", ret);

        /* Wait for completions */
        /* cmd.asynch.count will contain the number of processed commands */
        cmd.opcode         = XZTL_MISC_ASYNCH_WAIT;
        cmd.asynch.ctx_ptr = &tctx;
        ret                = xztl_media_submit_misc(&cmd);
        cunit_znd_assert_int("xztl_media_submit_misc:asynch-wait", ret);

        /* Destroy the context */
        /* Stops the completion thread */
        cmd.opcode = XZTL_MISC_ASYNCH_TERM;
        ret        = xztl_media_submit_misc(&cmd);
        cunit_znd_assert_int("xztl_media_submit_misc:asynch-term", ret);
    }
}

static void test_znd_dma_memory(void) {
    void *buf;

    buf = xztl_media_dma_alloc(1024);
    cunit_znd_assert_ptr("xztl_media_dma_alloc", buf);

    if (buf) {
        xztl_media_dma_free(buf);
        CU_PASS("xztl_media_dma_free (buf);");
    }
}

static int  outstanding;
static void test_znd_callback(void *arg) {
    struct xztl_io_mcmd *cmd;

    cmd = (struct xztl_io_mcmd *)arg;
    cunit_znd_assert_int("xztl_media_submit_io:cb", cmd->status);
    outstanding--;
}

static void test_znd_poke_ctx(struct xztl_mthread_ctx *tctx) {
    struct xztl_misc_cmd misc;
    misc.opcode         = XZTL_MISC_ASYNCH_POKE;
    misc.asynch.ctx_ptr = tctx;
    misc.asynch.limit   = 0;
    misc.asynch.count   = 0;

    int ret = xztl_media_submit_misc(&misc);
    cunit_znd_assert_int("xztl_media_submit_misc", ret);
    /* if (!xztl_media_submit_misc(&misc)) {
        if (!misc.asynch.count) {
        }
    } */
}

static void test_znd_append_zone(void) {
    struct xztl_mp_entry    *mp_cmd;
    struct xztl_io_mcmd     *cmd;
    struct xztl_mthread_ctx *tctx;
    uint16_t                 tid, ents, nlbas, zone;
    uint64_t                 bsize;
    void                    *wbuf;
    int                      ret;
    struct xztl_core        *core;
    get_xztl_core(&core);

    tid   = 0;
    ents  = 128;
    nlbas = 16;
    zone  = 0;
    bsize = nlbas * core->media->geo.nbytes * 1UL;

    /* Initialize mempool module */
    ret = xztl_mempool_init();
    cunit_znd_assert_int("xztl_mempool_init", ret);
    if (ret)
        return;

    /* Initialize thread media context */
    tctx = xztl_ctx_media_init(ents);
    cunit_znd_assert_ptr("xztl_ctx_media_init", tctx);
    if (!tctx)
        goto MP;

    /* Reset the zone before appending */
    test_znd_manage_single(XZTL_ZONE_MGMT_RESET, XNVME_SPEC_ZND_STATE_EMPTY,
                           zone, "xztl_media_submit_znm:reset");

    /* Allocate DMA memory */
    wbuf = xztl_media_dma_alloc(bsize);
    cunit_znd_assert_ptr("xztl_media_dma_alloc", wbuf);
    if (!wbuf)
        goto CTX;

    /* Get media command entry */
    xztl_mempool_create(XZTL_MEMPOOL_MCMD, 0, 16, sizeof(struct xztl_io_mcmd),
                        NULL, NULL);
    mp_cmd = xztl_mempool_get(XZTL_MEMPOOL_MCMD, tid);
    cunit_znd_assert_ptr("xztl_mempool_get", mp_cmd);
    if (!mp_cmd)
        goto DMA;

    /* Fill up command structure */
    cmd = (struct xztl_io_mcmd *)mp_cmd->opaque;
    memset(cmd, 0x0, sizeof(struct xztl_io_mcmd));

    cmd->opcode    = (XZTL_WRITE_APPEND) ? XZTL_ZONE_APPEND : XZTL_CMD_WRITE;
    cmd->synch     = 0;
    cmd->async_ctx = tctx;
    cmd->prp[0]    = (uint64_t)wbuf;
    cmd->nsec[0]   = nlbas;
    cmd->callback  = test_znd_callback;

    cmd->addr[0].g.zone = zone;
    cmd->addr[0].g.sect = zone * core->media->geo.sec_zn * 1UL;

    /* Submit append */
    outstanding = 1;
    ret         = xztl_media_submit_io(cmd);
    cunit_znd_assert_int("xztl_media_submit_io", ret);

    /* Wait for completions */
    while (outstanding) {
        test_znd_poke_ctx(tctx);
    }

    /* Clear up */
    xztl_mempool_put(mp_cmd, XZTL_MEMPOOL_MCMD, tid);
DMA:
    xztl_media_dma_free(wbuf);
CTX:
    ret = xztl_ctx_media_exit(tctx);
    cunit_znd_assert_int("xztl_ctx_media_exit", ret);
MP:
    ret = xztl_mempool_exit();
    cunit_znd_assert_int("xztl_mempool_exit", ret);
}

static void test_znd_read_zone(void) {
    struct xztl_mp_entry    *mp_cmd;
    struct xztl_io_mcmd     *cmd;
    struct xztl_mthread_ctx *tctx;
    uint16_t                 tid, ents, nlbas, zone;
    uint64_t                 bsize;
    void                    *wbuf;
    int                      ret;
    struct xztl_core        *core;
    get_xztl_core(&core);

    tid   = 0;
    ents  = 128;
    nlbas = 16;
    zone  = 0;
    bsize = nlbas * core->media->geo.nbytes * 1UL;

    /* Initialize mempool module */
    ret = xztl_mempool_init();
    cunit_znd_assert_int("xztl_mempool_init", ret);
    if (ret)
        return;

    /* Initialize thread media context */
    tctx = xztl_ctx_media_init(ents);
    cunit_znd_assert_ptr("xztl_ctx_media_init", tctx);
    if (!tctx)
        goto MP;

    /* Allocate DMA memory */
    wbuf = xztl_media_dma_alloc(bsize);
    cunit_znd_assert_ptr("xztl_media_dma_alloc", wbuf);
    if (!wbuf)
        goto CTX;

    /* Get media command entry */
    xztl_mempool_create(XZTL_MEMPOOL_MCMD, 0, 16, sizeof(struct xztl_io_mcmd),
                        NULL, NULL);
    mp_cmd = xztl_mempool_get(XZTL_MEMPOOL_MCMD, tid);
    cunit_znd_assert_ptr("xztl_mempool_get", mp_cmd);
    if (!mp_cmd)
        goto DMA;

    /* Fill up command structure */
    cmd = (struct xztl_io_mcmd *)mp_cmd->opaque;
    memset(cmd, 0x0, sizeof(struct xztl_io_mcmd));

    cmd->opcode    = XZTL_CMD_READ;
    cmd->synch     = 0;
    cmd->async_ctx = tctx;
    cmd->prp[0]    = (uint64_t)wbuf;
    cmd->nsec[0]   = nlbas;
    cmd->callback  = test_znd_callback;

    /* We currently use sector only read addresses */
    cmd->addr[0].g.sect = zone * core->media->geo.sec_zn * 1UL;

    /* Submit read */
    outstanding = 1;
    ret         = xztl_media_submit_io(cmd);
    cunit_znd_assert_int("xztl_media_submit_io", ret);

    /* Wait for completions */
    while (outstanding) {
        test_znd_poke_ctx(tctx);
    }

    /* Clear up */
    xztl_mempool_put(mp_cmd, XZTL_MEMPOOL_MCMD, tid);
DMA:
    xztl_media_dma_free(wbuf);
CTX:
    ret = xztl_ctx_media_exit(tctx);
    cunit_znd_assert_int("xztl_ctx_media_exit", ret);
MP:
    ret = xztl_mempool_exit();
    cunit_znd_assert_int("", ret);
}

int main(int argc, const char **argv) {
    int failed;

    if (argc < 2) {
        printf("Please provide the device path. e.g. liou:/dev/nvme0n2\n");
        return -1;
    }

    devname = &argv[1];
    printf("Device: %s\n", *devname);

    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
        return CU_get_error();

    pSuite = CU_add_suite("Suite_zn_media", cunit_znd_media_init,
                          cunit_znd_media_exit);
    if (pSuite == NULL) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    if ((CU_add_test(pSuite, "Parse the opt param", test_znd_opt_parse) ==
         NULL) ||
        (CU_add_test(pSuite, "Assign opt param", test_znd_opt_assign) ==
         NULL) ||
        (CU_add_test(pSuite, "Set the znd_media layer",
                     test_znd_media_register) == NULL) ||
        (CU_add_test(pSuite, "Initialize media", test_znd_media_init) ==
         NULL) ||
        (CU_add_test(pSuite, "Get/Check Log Page report", test_znd_report) ==
         NULL) ||
        (CU_add_test(pSuite, "Open/Close/Finish/Reset a zone",
                     test_znd_op_cl_fi_re) == NULL) ||
        (CU_add_test(pSuite, "Init/Term asynchronous context",
                     test_znd_asynch_ctx) == NULL) ||
        (CU_add_test(pSuite, "Allocate/Free DMA aligned buffer",
                     test_znd_dma_memory) == NULL) ||
        (CU_add_test(pSuite, "Append/Write 16 sectors to a zone",
                     test_znd_append_zone) == NULL) ||
        (CU_add_test(pSuite, "Read 16 sectors from a zone",
                     test_znd_read_zone) == NULL) ||
        (CU_add_test(pSuite, "Close media", test_znd_media_exit) == NULL)) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    failed = CU_get_number_of_tests_failed();
    CU_cleanup_registry();

    return failed;
}
