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
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <xztl.h>
#include <libzrocks.h>
#include "CUnit/Basic.h"

/* Write Buffer Size */
#define WRITE_TBUFFER_SZ (1024 * 1024 * 2)  // 2 MB

/* Number of buffers to write */
#define WRITE_COUNT (1024 * 2)  // 4GB

/* Parallel reads */
#define READ_NTHREADS 64

/* Sector to read per zone */
#define READ_ZONE_SEC (1024 * 16)

/* Size of each read command */
#define READ_SZ (16 * ZNS_ALIGMENT * 8)

/* Read Iterations */
#define READ_ITERATIONS 16
#define WRITE_NTHREADS  64

static const char **devname;

static uint64_t buffer_sz = WRITE_TBUFFER_SZ;
static uint64_t nwrites   = WRITE_COUNT;
static uint32_t nthreads  = READ_NTHREADS;

struct tparams {
    void  *buf;
    size_t size;
    int    waiting;

    struct zrocks_map maps[2];
    uint16_t          map_len;
    int               level;
};

static struct tparams th_param[WRITE_NTHREADS];

static void cunit_zrocksrw_assert_ptr(char *fn, void *ptr) {
    CU_ASSERT((uint64_t)ptr != 0);
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
    uint8_t  value = 0x1;

    for (byte = 0; byte < buffer_sz; byte += 16) {
        value += 0x1;
        memset(buf, value, 16);
    }
}

static void *test_write_th(void *th_i) {
    int ret, i, th;
    th = (uint64_t)th_i;

    printf("\rStart... th:%d \r\n", th);
    for (i = 0; i < nwrites; i++) {
        ret = zrocks_write(th_param[th].buf, th_param[th].size,
                           th_param[th].level, th_param[th].maps,
                           &th_param[th].map_len);
        cunit_zrocksrw_assert_int("zrocksrw_write:write", ret);
        // printf("\rWriting... th:%d  node:%d\r\n", th_i,
        // th_param[th_i].maps[0].g.node_id);
    }

    th_param[th].waiting = 0;
    return NULL;
}

static void test_zrocksrw_write(void) {
    if (nthreads == 0) {
        return;
    }
    void     *buf[nthreads];
    int       bufi, ret;
    uint64_t  th_i;
    pthread_t thread_id[nthreads];

    struct timespec ts_s;
    struct timespec ts_e;
    uint64_t        start_ns;
    uint64_t        end_ns;
    double          seconds, mb;

    for (bufi = 0; bufi < nthreads; bufi++) {
        buf[bufi] = zrocks_alloc(buffer_sz);
        cunit_zrocksrw_assert_ptr("zrocksrw_write:alloc", buf[bufi]);
        if (!buf[bufi])
            goto FREE;

        test_zrockswr_fill_buffer(buf[bufi]);
    }

    GET_NANOSECONDS(start_ns, ts_s);
    printf("\n");
    for (th_i = 0; th_i < nthreads; th_i++) {
        th_param[th_i].buf     = buf[th_i];
        th_param[th_i].size    = buffer_sz;
        th_param[th_i].waiting = 1;
        th_param[th_i].level   = th_i % 4;

        ret = pthread_create(&thread_id[th_i], NULL, test_write_th,
                             (void *)th_i);  // NOLINT
        if (ret) {
            printf("Thread %lu NOT started.\n", th_i);
        }
    }

    /* Wait until all threads complete */
    do {
        usleep(10);
        int left = 0;

        for (int tid = 0; tid < nthreads; tid++) left += th_param[tid].waiting;

        if (!left)
            break;
    } while (1);

    GET_NANOSECONDS(end_ns, ts_e);
    seconds = (double)(end_ns - start_ns) / (double)1000000000;  // NOLINT
    mb      = ((double)nwrites * (double)nthreads * (double)buffer_sz) /
         (double)1024 / (double)1024;  // NOLINT

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

static void *test_read_th(void *th_i) {
    int      ret, th;
    uint64_t offset;
    th = (uint64_t)th_i;

    printf("\rStart... th:%d \r\n", th);

    for (int j = 0; j < nwrites; j++) {
        offset = 0;
        for (int i = 0; i < buffer_sz / th_param[th].size; i++) {
            ret = zrocks_read(th_param[th].maps[0].g.node_id, 0,
                              th_param[th].buf, th_param[th].size);
            cunit_zrocksrw_assert_int("zrocks_read:read", ret);
            offset += th_param[th].size;
            // printf("\rReading... th:%d  node:%d\r\n", th_i,
            // th_param[th_i].maps[0].g.node_id);
        }
    }

    th_param[th].waiting = 0;
    return NULL;
}

static void test_zrocksrw_read(void) {
    void     *buf[nthreads];
    uint64_t  bufi, th_i;
    pthread_t thread_id[nthreads];

    struct timespec ts_s;
    struct timespec ts_e;
    uint64_t        start_ns;
    uint64_t        end_ns;
    double          seconds, mb;

    size_t data_buff = (4 * 1024) * 8 * 64;

    for (bufi = 0; bufi < nthreads; bufi++) {
        buf[bufi] = zrocks_alloc(data_buff);
        cunit_zrocksrw_assert_ptr("zrocksrw_read:alloc", buf[bufi]);
        if (!buf[bufi])
            goto FREE;
    }

    GET_NANOSECONDS(start_ns, ts_s);

    printf("\n");
    int ret;
    for (th_i = 0; th_i < nthreads; th_i++) {
        th_param[th_i].buf     = buf[th_i];
        th_param[th_i].size    = data_buff;
        th_param[th_i].waiting = 1;

        ret = pthread_create(&thread_id[th_i], NULL, test_read_th,
                             (void *)th_i);  // NOLINT
        if (ret) {
            printf("Thread %lu NOT started.\n", th_i);
        }
    }

    /* Wait until all threads complete */
    do {
        usleep(10);
        int left = 0;

        for (int tid = 0; tid < nthreads; tid++) left += th_param[tid].waiting;

        if (!left)
            break;
    } while (1);

    GET_NANOSECONDS(end_ns, ts_e);
    seconds = (double)(end_ns - start_ns) / (double)1000000000;  // NOLINT
    mb      = ((double)nwrites * (double)nthreads * (double)buffer_sz) /
         (double)1024 / (double)1024;  // NOLINT

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

/*static void *test_random_read_th(int th_i) {
    int      ret;
    uint64_t offset        = 0;
    size_t   size          = 4096;
    uint64_t readtime      = nwrites * 512;
    uint64_t totalwritesec = buffer_sz * nwrites / size;
    uint64_t randoffidx    = rand() % totalwritesec;
    // printf("\rStart... th:%d \r\n", th_i);
    for (int i = 0; i < readtime; i++) {
        randoffidx = rand() % totalwritesec;
        offset     = (randoffidx * size);

        ret = zrocks_read(*th_param[th_i].node_id, offset, th_param[th_i].buf,
                          size, th_param[th_i].xztl_tid);
        cunit_zrocksrw_assert_int("zrocks_read:read", ret);
    }
    zrocks_put_resource(th_param[th_i].xztl_tid);
    th_param[th_i].waiting = 0;
    return NULL;
}

static void test_zrocksrw_random_read(void) {
    void *    buf[nthreads];
    uint64_t  bufi, th_i, it;
    pthread_t thread_id[nthreads];

    struct timespec ts_s;
    struct timespec ts_e;
    uint64_t        start_ns;
    uint64_t        end_ns;
    uint64_t        read_gl[nthreads];
    double          seconds, mb;

    size_t data_buff = (1 * 1024 * 1024);

    for (bufi = 0; bufi < nthreads; bufi++) {
        buf[bufi] = zrocks_alloc(data_buff);
        cunit_zrocksrw_assert_ptr("zrocksrw_read:alloc", buf[bufi]);
        if (!buf[bufi])
            goto FREE;
    }

    memset(read_gl, 0x0, sizeof(uint64_t) * nthreads);
    GET_NANOSECONDS(start_ns, ts_s);

    printf("\n");
    int ret;
    for (th_i = 0; th_i < nthreads; th_i++) {
        th_param[th_i].buf     = buf[th_i];
        th_param[th_i].size    = data_buff;
        th_param[th_i].waiting = 1;
        // int a = *th_param[nthreads - 1].node_id + 1;
        // *th_param[th_i].node_id = th_i;
        *th_param[th_i].node_id = nodes[th_i];
        th_param[th_i].xztl_tid = zrocks_get_resource();

        ret = pthread_create(&thread_id[th_i], NULL, test_random_read_th,
                             th_i);  // NOLINT
        if (ret) {
            printf("Thread %d NOT started.\n", th_i);
        }
    }*/

/* Wait until all threads complete */
/*do {
    usleep(10);
    int left = 0;

    for (int tid = 0; tid < nthreads; tid++) left += th_param[tid].waiting;

    if (!left)
        break;
} while (1);

GET_NANOSECONDS(end_ns, ts_e);
seconds = (double)(end_ns - start_ns) / (double)1000000000;  // NOLINT
mb = ((double)(nwrites * 512) * (double)nthreads * (double)4096) / (double)1024
/ (double)1024;  // NOLINT

printf("\n");
printf("Read data: %.2lf MB\n", mb);
printf("Elapsed time: %.4lf sec\n", seconds);
printf("Bandwidth: %.4lf MB/s\n", mb / seconds);

FREE:
while (bufi) {
    bufi--;
    zrocks_free(buf[bufi]);
}
}*/

uint64_t atoull(const char *args) {
    uint64_t ret = 0;
    while (*args) {
        ret = ret * 10 + (*args++ - '0');
    }
    return ret;
}
int main(int argc, const char **argv) {
    int      failed = 1;
    uint64_t bsize  = 0;

    if (argc < 2 || !memcmp(argv[1], "--help\0", strlen(argv[1]))) {
        printf(
            " Usage: zrocks-test-rw <DEV_PATH> <NUM_THREADS> "
            "<BUFFER_SIZE_IN_MB> <NUM_OF_BUFFERS>\n");
        printf("\n   e.g.: test-zrocks-rw liou:/dev/nvme0n2 8 2 1024\n");
        printf("         This command uses 8 threads to read data and\n");
        printf("         writes 2 GB to the device\n");
        return 0;
    }

    if (argc >= 3) {
        nthreads = 1UL * atoi(argv[2]);
        if (nthreads > 1024) {
            printf("Error: thread number exceed 1024.");
            return failed;
        }
    }

    if (argc >= 5) {
        bsize = atoull(argv[3]);
        if (bsize > 256) {
            printf("Error: user buffer exceed 256M.");
            return failed;
        }

        buffer_sz = (1024 * 1024) * bsize;
        nwrites   = 1UL * atoi(argv[4]);
    }

    if (nwrites <= 0) {
        return failed;
    }

    devname = &argv[1];
    printf("Device: %s\n", *devname);

    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
        goto FREE;

    pSuite = CU_add_suite("Suite_zrocks_rw", cunit_zrocksrw_init,
                          cunit_zrocksrw_exit);
    if (pSuite == NULL) {
        CU_cleanup_registry();
        goto FREE;
    }

    if ((CU_add_test(pSuite, "Initialize ZRocks", test_zrocksrw_init) ==
         NULL) ||
        (CU_add_test(pSuite, "Write Bandwidth", test_zrocksrw_write) == NULL) ||
        (CU_add_test(pSuite, "Read Bandwidth", test_zrocksrw_read) == NULL) ||
        // (CU_add_test(pSuite, "Random Read Bandwidth",
        //             test_zrocksrw_random_read) == NULL) ||
        (CU_add_test(pSuite, "Close ZRocks", test_zrocksrw_exit) == NULL)) {
        failed = 1;
        CU_cleanup_registry();
        goto FREE;
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    failed = CU_get_number_of_tests_failed();
    CU_cleanup_registry();
FREE:
    return failed;
}
