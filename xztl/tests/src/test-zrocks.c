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

#include <omp.h>
#include <stdint.h>
#include <stdlib.h>
#include <xztl.h>
#include <libzrocks.h>

#include "CUnit/Basic.h"

/* Number of Objects */
#define TEST_N_BUFFERS 2

/* Number of random objects to read */
#define TEST_RANDOM_ID 2

/* Object Size */
#define TEST_BUFFER_SZ (1024 * 1024 * 16) /* 16 MB */

static uint8_t *wbuf[TEST_N_BUFFERS];
static uint8_t *rbuf[TEST_N_BUFFERS];

static const char **devname;

static void cunit_zrocks_assert_ptr(char *fn, void *ptr) {
    CU_ASSERT((uint64_t)ptr != 0);
    if (!ptr)
        printf("\n %s: ptr %p\n", fn, ptr);
}

static void cunit_zrocks_assert_int(char *fn, uint64_t status) {
    CU_ASSERT(status == 0);
    if (status)
        printf("\n %s: %lx\n", fn, status);
}

static int cunit_zrocks_init(void) {
    return 0;
}

static int cunit_zrocks_exit(void) {
    return 0;
}

static void test_zrocks_init(void) {
    int ret;

    ret = zrocks_init(*devname);
    cunit_zrocks_assert_int("zrocks_init", ret);
}

static void test_zrocks_exit(void) {
    zrocks_exit();
}

static void test_zrocks_fill_buffer(uint32_t id) {
    uint32_t byte;
    uint8_t  value = 0x1;

    for (byte = 0; byte < TEST_BUFFER_SZ; byte += 16) {
        value += 0x1;
        memset(&wbuf[id][byte], value, 16);
    }
}

static int test_zrocks_check_buffer(uint32_t id, uint32_t off, uint32_t size) {
    /*printf (" \nMem check:\n");
    for (int i = off; i < off + size; i++) {
        if (i % 16 == 0 && i)
            printf("\n %d-%d ", i - (i%16), (i - (i%16)) + 16);
            printf (" %x/%x", wbuf[id][i], rbuf[id][i]);
    }
    printf("\n");
  */
    return memcmp(wbuf[id], rbuf[id], size);
}

static void test_zrocks_new(void) {
    uint32_t ids;
    uint64_t id;
    uint32_t size;
    uint8_t  level;
    int      ret[TEST_N_BUFFERS];

    ids   = TEST_N_BUFFERS;
    size  = TEST_BUFFER_SZ;
    level = 0;

#pragma omp parallel for
    for (id = 0; id < ids; id++) {
        /* Allocate DMA memory */
        wbuf[id] = xztl_media_dma_alloc(size);
        cunit_zrocks_assert_ptr("xztl_media_dma_alloc", wbuf[id]);

        if (!wbuf[id])
            continue;

        test_zrocks_fill_buffer(id);

        ret[id] = zrocks_new(id + 1, wbuf[id], size, level);
        cunit_zrocks_assert_int("zrocks_new", ret[id]);
    }
}

static void test_zrocks_read(void) {
    uint32_t ids, offset;
    uint64_t id;
    int      ret[TEST_N_BUFFERS];
    size_t   read_sz, size;

    ids     = TEST_N_BUFFERS;
    read_sz = 1024 * 64; /* 64 KB */
    size    = TEST_BUFFER_SZ;

    for (id = 0; id < ids; id++) {
        /* Allocate DMA memory */
        rbuf[id] = xztl_media_dma_alloc(size);
        cunit_zrocks_assert_ptr("xztl_media_dma_alloc", rbuf[id]);
        if (!rbuf[id])
            continue;

        memset(rbuf[id], 0x0, size);

        offset = 0;
        while (offset < size) {
            ret[id] =
                zrocks_read_obj(id + 1, offset, rbuf[id] + offset, read_sz);
            cunit_zrocks_assert_int("zrocks_read_obj", ret[id]);
            if (ret[id])
                printf("Read error: ID %lu, offset %d, status: %x\n", id + 1,
                       offset, ret[id]);
            offset += read_sz;
        }

        ret[id] = test_zrocks_check_buffer(id, 0, TEST_BUFFER_SZ);
        cunit_zrocks_assert_int("zrocks_read_obj:check", ret[id]);
        if (ret[id])
            printf("Corruption: ID %lu, corrupted: %d bytes\n", id + 1,
                   ret[id]);

        xztl_media_dma_free(rbuf[id]);
    }
}

static void test_zrocks_random_read(void) {
    uint64_t id;
    uint64_t random_off[4] = {63, 24567, 175678, 267192};
    size_t   random_sz[4]  = {532, 53, 2695, 1561};
    // uint64_t random_off[1] = {24567};
    // size_t   random_sz[1]  = {53};
    int      readi, ret;
    uint8_t *buf, *woff;

    id = TEST_RANDOM_ID;

    buf = xztl_media_dma_alloc(1024 * 512);
    cunit_zrocks_assert_ptr("xztl_media_dma_alloc", buf);
    if (!buf)
        return;

    for (readi = 0; readi < 4; readi++) {
        memset(buf, 0x0, random_sz[readi]);
        ret = zrocks_read_obj(id, random_off[readi], buf, random_sz[readi]);
        cunit_zrocks_assert_int("zrocks_read_obj", ret);

        woff = &wbuf[id - 1][random_off[readi]];

        /* Uncomment for a detailed read check (per-byte print)
                printf (" \nMem check:\n");
                for (int i = 0; i < random_sz[readi] + 4096; i++) {
                    if (i % 16 == 0)
                        printf("\n %lu-%lu ",
                            (i+random_off[readi]) - ((i+random_off[readi]) % 16)
           + random_off[readi] % 16,
                            ((i+random_off[readi]) - ((i+random_off[readi]) %
           16)) + 16 + random_off[readi] % 16); printf (" %x/%x", woff[i],
           buf[i]);
                }
                printf("\n");
        */
        cunit_zrocks_assert_int("zrocks_read_obj:check",
                                memcmp(woff, buf, random_sz[readi]));
    }

    xztl_media_dma_free(buf);

    for (int i = 0; i < TEST_N_BUFFERS; i++) xztl_media_dma_free(wbuf[i]);
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

    pSuite = CU_add_suite("Suite_zrocks", cunit_zrocks_init, cunit_zrocks_exit);
    if (pSuite == NULL) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    if ((CU_add_test(pSuite, "Initialize ZRocks", test_zrocks_init) == NULL) ||
        (CU_add_test(pSuite, "ZRocks New", test_zrocks_new) == NULL) ||
        (CU_add_test(pSuite, "ZRocks Read", test_zrocks_read) == NULL) ||
        (CU_add_test(pSuite, "ZRocks Random Read", test_zrocks_random_read) ==
         NULL) ||
        (CU_add_test(pSuite, "Close ZRocks", test_zrocks_exit) == NULL)) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    failed = CU_get_number_of_tests_failed();
    CU_cleanup_registry();

    return failed;
}
