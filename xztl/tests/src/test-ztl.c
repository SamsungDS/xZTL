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

#include <xztl.h>
#include <libzrocks.h>
#include <xztl-media.h>
#include <xztl-mods.h>

#include "CUnit/Basic.h"

static const char **devname;

static void cunit_ztl_assert_int(char *fn, uint64_t status) {
    CU_ASSERT(status == 0);
    if (status)
        printf("\n %s: %lx\n", fn, status);
}

static void cunit_ztl_assert_int_equal(char *fn, int value, int expected) {
    CU_ASSERT_EQUAL(value, expected);
    if (value != expected)
        printf("\n %s: value %d != expected %d\n", fn, value, expected);
}

static void test_ztl_init(void) {
    int ret;

    ret = znd_media_register(*devname);
    cunit_ztl_assert_int("znd_media_register", ret);
    if (ret)
        return;

    ret = xztl_media_init();
    cunit_ztl_assert_int("xztl_media_init", ret);
    if (ret)
        return;

    /* Register ZTL modules */
    ztl_zmd_register();
    ztl_pro_register();
    ztl_mpe_register();
    ztl_map_register();
    ztl_io_register();

    ret = ztl_init();
    cunit_ztl_assert_int("ztl_init", ret);
    if (ret)
        return;
}

static void test_ztl_exit(void) {
    ztl_exit();
    xztl_media_exit();
}

static void test_ztl_map_upsert_read(void) {
    uint64_t id, val, count, interval, old;
    int      ret;

    count    = 256 * ZNS_ALIGMENT - 1;
    interval = 1;

    for (id = 1; id <= count; id++) {
        ret = ztl()->map->upsert_fn(id * interval, id * interval, &old, 0);
        cunit_ztl_assert_int("ztl()->map->upsert_fn", ret);
    }

    for (id = 1; id <= count; id++) {
        val = ztl()->map->read_fn(id * interval);
        cunit_ztl_assert_int_equal("ztl()->map->read", val, id * interval);
    }

    id  = 456;
    val = 457;
    old = 0;

    ret = ztl()->map->upsert_fn(id, val, &old, 0);
    cunit_ztl_assert_int("ztl()->map->upsert_fn", ret);
    cunit_ztl_assert_int_equal("ztl()->map->upsert_fn:old", old, id);

    old = ztl()->map->read_fn(id);
    cunit_ztl_assert_int_equal("ztl()->map->read", old, val);
}

static int cunit_ztl_init(void) {
    return 0;
}

static int cunit_ztl_exit(void) {
    return 0;
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

    pSuite = CU_add_suite("Suite_ztl", cunit_ztl_init, cunit_ztl_exit);
    if (pSuite == NULL) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    if ((CU_add_test(pSuite, "Initialize ZTL", test_ztl_init) == NULL) ||
        (CU_add_test(pSuite, "Upsert/Read mapping", test_ztl_map_upsert_read) ==
         NULL) ||
        (CU_add_test(pSuite, "Close ZTL", test_ztl_exit) == NULL)) {
        failed = 1;
        CU_cleanup_registry();
        return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    failed = CU_get_number_of_tests_failed();
    CU_cleanup_registry();

    return failed;
}
