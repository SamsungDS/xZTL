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

#include <CUnit/Basic.h>
#include <omp.h>
#include <pthread.h>
#include <xztl-mempool.h>
#include <xztl.h>
#include <xztl-media.h>

static const char **devname;

static void cunit_mempool_assert_ptr(char *fn, void *ptr) {
    CU_ASSERT((uint64_t)ptr != 0);
    if (!ptr)
        printf("\n %s: ptr %p\n", fn, ptr);
}

static void cunit_mempool_assert_int(char *fn, int status) {
    CU_ASSERT(status == 0);
    if (status)
        printf(" %s: %x\n", fn, status);
}

static int cunit_mempool_init(void) {
    return 0;
}

static int cunit_mempool_exit(void) {
    return 0;
}

static void test_mempool_init(void) {
    int ret;

    ret = znd_media_register(*devname);
    cunit_mempool_assert_int("znd_media_register", ret);
    if (ret)
        return;

    ret = xztl_media_init();
    cunit_mempool_assert_int("xztl_media_init", ret);
    if (ret)
        return;

    cunit_mempool_assert_int("xztl_mempool_init", xztl_mempool_init());
}

static void test_mempool_create(void) {
    uint16_t type, tid, ents;
    uint32_t ent_sz;

    type   = XZTL_MEMPOOL_MCMD;
    tid    = 0;
    ents   = 32;
    ent_sz = 1024;

    cunit_mempool_assert_int(
        "xztl_mempool_create",
        xztl_mempool_create(type, tid, ents, ent_sz, NULL, NULL));
}

static void test_mempool_destroy(void) {
    uint16_t type, tid;

    type = XZTL_MEMPOOL_MCMD;
    tid  = 0;

    cunit_mempool_assert_int("xztl_mempool_destroy",
                             xztl_mempool_destroy(type, tid));
}

static void test_mempool_create_mult(void) {
    uint16_t type, tid, ents;
    uint32_t ent_sz;

    type   = XZTL_MEMPOOL_MCMD;
    ents   = 32;
    ent_sz = 128;

#pragma omp parallel for
    for (tid = 0; tid < 8; tid++) {
        cunit_mempool_assert_int(
            "xztl_mempool_create",
            xztl_mempool_create(type, tid, ents, ent_sz, NULL, NULL));
    }
}

static void test_mempool_get_put(void) {
    uint16_t              ent_i, ents = 30;
    uint32_t              ent_sz = 128;
    struct xztl_mp_entry *ent[ents];

    /* Get entries */
    for (ent_i = 0; ent_i < ents; ent_i++) {
        ent[ent_i] = xztl_mempool_get(XZTL_MEMPOOL_MCMD, 0);
        cunit_mempool_assert_ptr("xztl_mempool_get", ent[ent_i]);
    }

    /* Modify entry bytes */
    for (ent_i = 0; ent_i < ents; ent_i++)
        memset(ent[ent_i]->opaque, 0x0, ent_sz);

    /* Put entries */
    for (ent_i = 0; ent_i < ents; ent_i++) {
        xztl_mempool_put(ent[ent_i], XZTL_MEMPOOL_MCMD, 0);
        CU_PASS("xztl_mempool_put");
    }

    /* Repeat the process */
    for (ent_i = 0; ent_i < ents; ent_i++) {
        ent[ent_i] = xztl_mempool_get(XZTL_MEMPOOL_MCMD, 0);
        cunit_mempool_assert_ptr("xztl_mempool_get", ent[ent_i]);
    }
    for (ent_i = 0; ent_i < ents; ent_i++)
        memset(ent[ent_i]->opaque, 0x0, ent_sz);
    for (ent_i = 0; ent_i < ents; ent_i++) {
        xztl_mempool_put(ent[ent_i], XZTL_MEMPOOL_MCMD, 0);
        CU_PASS("xztl_mempool_put");
    }
}

static void test_mempool_exit(void) {
    cunit_mempool_assert_int("xztl_mempool_exit", xztl_mempool_exit());
    cunit_mempool_assert_int("xztl_media_exit", xztl_media_exit());
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

    pSuite =
        CU_add_suite("Suite_mempool", cunit_mempool_init, cunit_mempool_exit);
    if (pSuite == NULL) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    if ((CU_add_test(pSuite, "Initialize", test_mempool_init) == NULL) ||
        (CU_add_test(pSuite, "Create a mempool", test_mempool_create) ==
         NULL) ||
        (CU_add_test(pSuite, "Destroy a mempool", test_mempool_destroy) ==
         NULL) ||
        (CU_add_test(pSuite, "Create parallel mempools",
                     test_mempool_create_mult) == NULL) ||
        (CU_add_test(pSuite, "Get and put entries", test_mempool_get_put) ==
         NULL) ||
        (CU_add_test(pSuite, "Closes the module", test_mempool_exit) == NULL)) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    failed = CU_get_number_of_tests_failed();
    CU_cleanup_registry();

    return failed;
}
