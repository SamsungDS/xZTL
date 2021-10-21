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
#include <libzrocks.h>
#include <xztl.h>
#include "CUnit/Basic.h"

/* Write Buffer Size */
#define WRITE_TBUFFER_SZ (1024 * 1024 * 2) // 2 MB

/* Number of buffers to write */
#define WRITE_COUNT      (1024 * 2) // 4GB

/* Parallel reads */
#define READ_NTHREADS    16

/* Sector to read per zone */
#define READ_ZONE_SEC    (1024 * 16)

/* Size of each read command */
#define READ_SZ        (16 * ZNS_ALIGMENT)

/* Read Iterations */
#define READ_ITERATIONS  16

static const char **devname;

static uint64_t buffer_sz = WRITE_TBUFFER_SZ;
static uint64_t nwrites   = WRITE_COUNT;
static uint64_t nthreads  = READ_NTHREADS;

static struct zrocks_map **map = NULL;
static uint16_t *pieces = NULL;

static void cunit_zrocksrw_assert_ptr(char *fn, void *ptr) {
    CU_ASSERT((uint64_t) ptr != 0);
    if (!ptr)
        printf("\n %s: ptr %p\n", fn, ptr);
}

static void cunit_zrocksrw_assert_int(char *fn, uint64_t status) {
    CU_ASSERT(status == 0);
    if (status)
        printf("\n %s: %lx\n", fn, status);
}

static int cunit_zrocksrw_init(void) {
    return 0;
}

static int cunit_zrocksrw_exit(void) {
    return 0;
}

static void test_zrocksrw_init(void) {
    int ret;

    ret = zrocks_init(*devname);
    cunit_zrocksrw_assert_int("zrocks_init", ret);
}

static void test_zrocksrw_exit(void) {
    zrocks_exit();
}

static void test_zrockswr_fill_buffer(void *buf) {
    uint32_t byte;
    uint8_t value = 0x1;

    for (byte = 0; byte < buffer_sz; byte += 16) {
        value += 0x1;
        memset(buf, value, 16);
    }
}

static void test_zrocksrw_write(void) {
    if (nthreads == 0) {
        return;
    }
    void *buf[nthreads];
    int bufi, th_i;

    struct timespec ts_s;
    struct timespec ts_e;
    uint64_t start_ns;
    uint64_t end_ns;
    double seconds, mb;

    for (bufi = 0; bufi < nthreads; bufi++) {
        buf[bufi] = zrocks_alloc(buffer_sz);
        cunit_zrocksrw_assert_ptr("zrocksrw_write:alloc", buf[bufi]);
        if (!buf[bufi])
            goto FREE;

        test_zrockswr_fill_buffer(buf[bufi]);
    }

    GET_NANOSECONDS(start_ns, ts_s);

    printf("\n");
    for (th_i = 0; th_i < nwrites; th_i++) {
        int ret;

        ret = zrocks_write(buf[th_i % nthreads], buffer_sz, 0, &map[th_i], &pieces[th_i]);

        printf("\rWriting... %d/%lu", th_i, nwrites);
        cunit_zrocksrw_assert_int("zrocksrw_write:write", ret);
    }

    GET_NANOSECONDS(end_ns, ts_e);
    seconds = (double) (end_ns - start_ns) / (double) 1000000000; // NOLINT
    mb = ( (double) nwrites * (double) buffer_sz ) / (double) 1024 / (double) 1024; // NOLINT

    printf("\n");
    printf("Written data: %.2lf MB\n", mb);
    printf("Elapsed time: %.4lf sec\n", seconds);
    printf("Bandwidth: %.4lf MB/s\n", mb / seconds);

FREE:
    while (bufi) {
        bufi--;
        zrocks_free(buf[bufi]);
    }
}

static void test_zrocksrw_read(void) {
    void *buf[nthreads];
    uint64_t bufi, th_i, it;

    struct timespec ts_s;
    struct timespec ts_e;
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t read_gl[nthreads];
    double seconds, mb;

    for (bufi = 0; bufi < nthreads; bufi++) {
        buf[bufi] = zrocks_alloc((uint64_t) READ_ZONE_SEC * (uint64_t) ZNS_ALIGMENT);
        cunit_zrocksrw_assert_ptr("zrocksrw_read:alloc", buf[bufi]);
        if (!buf[bufi])
            goto FREE;
    }

    memset (read_gl, 0x0, sizeof(uint64_t) * nthreads);
    GET_NANOSECONDS(start_ns, ts_s);

    printf("\n");

    #pragma omp parallel for num_threads(nthreads)
    for (th_i = 0; th_i < nthreads; th_i++) {

        for (it = 0; it < READ_ITERATIONS; it++) {
            int ret;
            uint64_t offset, size, read;

            size = (uint64_t) READ_ZONE_SEC * (uint64_t) ZNS_ALIGMENT;
            offset = th_i * size;
            read = 0;

            while (size) {
                ret = zrocks_read(offset, (char *) buf[th_i] + read, READ_SZ); // NOLINT
                cunit_zrocksrw_assert_int("zrocksrw_read:read", ret);

                offset += READ_SZ;
                read += READ_SZ;
                read_gl[th_i] += READ_SZ;
                size -= READ_SZ;
            }
        }
    }

    GET_NANOSECONDS(end_ns, ts_e);
    seconds = (double) (end_ns - start_ns) / (double) 1000000000; // NOLINT

    mb = 0;
    for (th_i = 0; th_i < nthreads; th_i++)
        mb += read_gl[th_i];

    mb = mb / (double) 1024 / (double) 1024; // NOLINT

    printf("\n");
    printf("Read data: %.2lf MB\n", mb);
    printf("Elapsed time: %.4lf sec\n", seconds);
    printf("Bandwidth: %.4lf MB/s\n", mb / seconds);

FREE:
    while (bufi) {
        bufi--;
        zrocks_free(buf[bufi]);
    }
}

uint64_t atoull(const char *args) {
    uint64_t ret = 0;
    while (*args) {
        ret = ret * 10 + (*args++ - '0');
    }
    return ret;
}
int main(int argc, const char **argv) {
    int failed = 1;

    if (argc < 2 || !memcmp(argv[1], "--help\0", strlen(argv[1]))) {
        printf(" Usage: zrocks-test-rw <DEV_PATH> <NUM_THREADS> "
                         "<BUFFER_SIZE_IN_MB> <NUM_OF_BUFFERS>\n");
        printf("\n   e.g.: test-zrocks-rw liou:/dev/nvme0n2 8 2 1024\n");
        printf("         This command uses 8 threads to read data and\n");
        printf("         writes 2 GB to the device\n");
        return 0;
    }

    if (argc >= 3) {
        nthreads = 1UL * atoi(argv[2]);
    }

    if (argc >= 5) {
        buffer_sz = (1024 * 1024) * atoull(argv[3]);
        nwrites = 1UL * atoi(argv[4]);
    }

    map = NULL;
    pieces = NULL;
    if (nwrites <= 0) {
        return failed;
    }
    map = malloc(sizeof(struct zrocks_map *) * nwrites);
    pieces = malloc(sizeof(uint16_t) * nwrites);

    devname = &argv[1];
    printf("Device: %s\n", *devname);

    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
        goto FREE;

    if (!map || !pieces)
        goto FREE;

    pSuite = CU_add_suite("Suite_zrocks_rw", cunit_zrocksrw_init, cunit_zrocksrw_exit);
    if (pSuite == NULL) {
        CU_cleanup_registry();
        goto FREE;
    }

    if ((CU_add_test(pSuite, "Initialize ZRocks",
              test_zrocksrw_init) == NULL) ||
        (CU_add_test(pSuite, "Write Bandwidth",
              test_zrocksrw_write) == NULL) ||
        (CU_add_test(pSuite, "Read Bandwidth",
              test_zrocksrw_read) == NULL) ||
        (CU_add_test(pSuite, "Close ZRocks",
              test_zrocksrw_exit) == NULL)) {
        failed = 1;
        CU_cleanup_registry();
        goto FREE;
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    failed = CU_get_number_of_tests_failed();
    CU_cleanup_registry();

FREE:
    if (map) free(map);
    if (pieces) free(pieces);
    return failed;
}
