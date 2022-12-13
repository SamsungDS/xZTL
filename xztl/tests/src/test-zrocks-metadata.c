/* rocksdb: Page cache manager
 *
 * Copyright 2019 Samsung Electronics
 */

#include <omp.h>
#include <stdint.h>
#include <stdlib.h>
#include <xztl.h>
#include <libzrocks.h>
#include <xztl-metadata.h>

#include "CUnit/Basic.h"

extern struct ztl_metadata metadata;

#define WRITE_TBUFFER_SZ        (32 * 1024)  // 32K
#define READ_MAX_SZ_PER_COMMAND (32 * 1024)  // 32K
#define SECTOR_SIZE             512

static const char **devname;
static uint64_t     buffer_sz_file_metadata = WRITE_TBUFFER_SZ * 1000;  // 32M

static void cunit_zrocks_metadata_assert_ptr(char *fn, void *ptr) {
    CU_ASSERT((uint64_t)ptr != 0);

    if (!ptr)
        printf("\n %s: ptr %p\n", fn, ptr);
}

static void cunit_zrocks_metadata_assert_int(char *fn, uint64_t status) {
    CU_ASSERT(status == 0);

    if (status)
        printf("\n %s: %lx\n", fn, status);
}

static void cunit_zrocks_metadata_assert_equal(void *buf_write,
                                               void *buf_read) {
    CU_ASSERT(strcmp(buf_write, buf_read) == 0);

    if (strcmp(buf_write, buf_read))
        printf("buf_write != buf_read\n");
}

static int cunit_zrocks_metadata_init(void) {
    return 0;
}

static int cunit_zrocks_metadata_exit(void) {
    return 0;
}

static void test_zrocks_metadata_init(void) {
    int ret;
    ret = zrocks_init(*devname);
    cunit_zrocks_metadata_assert_int("zrocks_init\n", ret);
}

static void test_zrocks_metadata_exit(void) {
    zrocks_exit();
}

static void test_zrocks_metadata_fill_buffer(void *buf, uint64_t buffer_sz) {
    uint32_t byte;
    uint8_t  value = 0x1;

    for (byte = 0; byte < buffer_sz; byte += 16) {
        value += 0x1;
        memset(buf, value, 16);
    }
}

static void test_zrocks_buffer_read(uint64_t slba, void *buf_read,
                                    uint64_t size) {
    int               ret;
    uint64_t          read;
    struct xztl_core *core;
    get_xztl_core(&core);
    read = 0;
    while (size) {
        ret = zrocks_read_metadata(slba,
                                   (unsigned char *)buf_read + read,  // NOLINT
                                   READ_MAX_SZ_PER_COMMAND);
        cunit_zrocks_metadata_assert_int("zrocks_read:", ret);
        slba += READ_MAX_SZ_PER_COMMAND / core->media->geo.nbytes;
        read += READ_MAX_SZ_PER_COMMAND;
        size -= READ_MAX_SZ_PER_COMMAND;
    }
}

static void test_zrocks_file_metadata(void) {
    printf("\n");
    struct ztl_metadata *metadata;
    void                *buf_write, *buf_read;
    uint64_t             size, slba;
    metadata = get_ztl_metadata();
    slba     = metadata->file_slba;
    size     = buffer_sz_file_metadata;

    buf_write = zrocks_alloc(size);
    buf_read  = zrocks_alloc(size);
    cunit_zrocks_metadata_assert_ptr("buf:alloc", buf_write);
    cunit_zrocks_metadata_assert_ptr("buf:alloc", buf_read);

    if (!buf_write || !buf_read)
        goto FREE;
    test_zrocks_metadata_fill_buffer(buf_write, size);

    int ret;
    ret = zrocks_write_file_metadata(buf_write, size);
    cunit_zrocks_metadata_assert_int("zrocks_file_metadata:", ret);

    test_zrocks_buffer_read(slba, buf_read, size);
    cunit_zrocks_metadata_assert_equal(buf_write, buf_read);

FREE:
    zrocks_free(buf_write);
    zrocks_free(buf_read);
}

static void test_zrocks_reset_file_metadata_zone(void) {
    printf("\n");
    struct ztl_metadata *metadata;
    void                *buf_write, *buf_read;
    struct xztl_core    *core;
    get_xztl_core(&core);
    uint64_t size, zone_size;
    metadata  = get_ztl_metadata();
    size      = buffer_sz_file_metadata;
    zone_size = core->media->geo.nbytes_zn;

    buf_write = zrocks_alloc(size);
    buf_read  = zrocks_alloc(size);
    cunit_zrocks_metadata_assert_ptr("zrocks_write:alloc", buf_write);
    cunit_zrocks_metadata_assert_ptr("zrocks_write:alloc", buf_read);
    if (!buf_write || !buf_read)
        goto FREE;
    test_zrocks_metadata_fill_buffer(buf_write, size);
    int ret;

    while (metadata->file_slba * core->media->geo.nbytes + size < zone_size) {
        ret = zrocks_write_file_metadata(buf_write, size);
        cunit_zrocks_metadata_assert_int("zrocks_write:write", ret);
    }

    uint64_t slba_before;
    slba_before = metadata->file_slba;
    ret         = zrocks_write_file_metadata(buf_write, size);
    cunit_zrocks_metadata_assert_int("zrocks_write:write", ret);
    CU_ASSERT(metadata->file_slba < slba_before);

    test_zrocks_buffer_read(0, buf_read, size);
    cunit_zrocks_metadata_assert_equal(buf_write, buf_read);

FREE:
    zrocks_free(buf_write);
    zrocks_free(buf_read);
}

int main(int argc, const char **argv) {
    int failed = -1;

    if (argc < 2 || !memcmp(argv[1], "--help\0", strlen(argv[1]))) {
        printf(" Usage: zrocks-test-metadata <DEV_PATH>\n");
        printf("\n   e.g.: test-zrocks-metadata liou:/dev/nvme0n1 \n");
        return 0;
    }

    devname = &argv[1];
    printf("Device: %s\n", *devname);

    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
        return failed;

    pSuite = CU_add_suite("Suite_zrocks_metadata", cunit_zrocks_metadata_init,
                          cunit_zrocks_metadata_exit);

    if (pSuite == NULL) {
        CU_cleanup_registry();
        return failed;
    }

    if ((CU_add_test(pSuite, "Initialize ZNS ZRocks",
                     test_zrocks_metadata_init) == NULL) ||
        (CU_add_test(pSuite, "Write and read file metadata",
                     test_zrocks_file_metadata) == NULL) ||
        (CU_add_test(pSuite, "reset file metadata zone",
                     test_zrocks_reset_file_metadata_zone) == NULL) ||
        //|| (CU_add_test(pSuite, "Read  metadata failed",
        //  test_zrocks_metadata_read_failed) == NULL)
        (CU_add_test(pSuite, "Close ZRocks", test_zrocks_metadata_exit) ==
         NULL)) {
        failed = 1;
        CU_cleanup_registry();
        return failed;
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    failed = CU_get_number_of_tests_failed();
    CU_cleanup_registry();
    return failed;
}
