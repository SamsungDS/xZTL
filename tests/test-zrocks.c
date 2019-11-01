#include <omp.h>
#include <stdint.h>
#include <stdlib.h>
#include <libzrocks.h>
#include <xapp.h>
#include "CUnit/Basic.h"

static void cunit_zrocks_assert_ptr (char *fn, void *ptr)
{
    CU_ASSERT ((uint64_t) ptr != 0);
    if (!ptr)
	printf ("\n %s: ptr %p\n", fn, ptr);
}

static void cunit_zrocks_assert_int (char *fn, uint64_t status)
{
    CU_ASSERT (status == 0);
    if (status)
	printf ("\n %s: %lx\n", fn, status);
}

static int cunit_zrocks_init (void)
{
    return 0;
}

static int cunit_zrocks_exit (void)
{
    return 0;
}

static void test_zrocks_init (void)
{
    int ret;

    ret = zrocks_init ();
    cunit_zrocks_assert_int ("zrocks_init", ret);
}

static void test_zrocks_exit (void)
{
    zrocks_exit ();
}

static void test_zrocks_new (void)
{
    uint32_t ids = 128;
    uint64_t id, phys[ids];
    void *wbuf[ids];
    uint32_t size;
    uint8_t level;
    int ret[ids];

    size  = 1024 * 1024 * 1; /* 1 MB */
    level = 0;

    #pragma omp parallel for
    for (id = 1; id < ids; id++) {
	/* Allocate DMA memory */
	wbuf[id] = xapp_media_dma_alloc (size, &phys[id]);
	cunit_zrocks_assert_ptr ("xapp_media_dma_alloc", wbuf[id]);
	if (!wbuf[id])
	    continue;

	memset (wbuf[id], 0xc, size);
	ret[id] = zrocks_new (id, wbuf[id], size, level);
	cunit_zrocks_assert_int ("zrocks_new", ret[id]);

	xapp_media_dma_free (wbuf[id]);
    }
}

int main (void)
{
    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
	return CU_get_error();

    pSuite = CU_add_suite("Suite_zrocks", cunit_zrocks_init, cunit_zrocks_exit);
    if (pSuite == NULL) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    if ((CU_add_test (pSuite, "Initialize ZRocks",
		      test_zrocks_init) == NULL) ||
	(CU_add_test (pSuite, "ZRocks New",
		      test_zrocks_new) == NULL) ||
        (CU_add_test (pSuite, "Close ZRocks",
		      test_zrocks_exit) == NULL)) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();

    return CU_get_error();
}
