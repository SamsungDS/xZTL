#include <stdio.h>
#include <xapp.h>
#include <xapp-media.h>
#include <stdlib.h>
#include "CUnit/Basic.h"

static int cunit_media_init (void)
{
    return 0;
}

static int cunit_media_exit (void)
{
    return 0;
}

static int test_media_init_fn (void)
{
    return 0;
}

static int test_media_exit_fn (void)
{
    return 0;
}

static int test_media_io_fn (struct xapp_io_mcmd *cmd)
{
    return 0;
}

static int test_media_zn_fn (struct xapp_zn_mcmd *cmd)
{
    return 0;
}

static void *test_media_dma_alloc (size_t size, uint64_t *phys)
{
    return malloc (size);
}

static void test_media_dma_free (void *ptr)
{
    free (ptr);
}

static void test_media_set (void)
{
    struct xapp_media media;

    media.init_fn   = test_media_init_fn;
    media.exit_fn   = test_media_exit_fn;
    media.submit_io = test_media_io_fn;
    media.zone_fn   = test_media_zn_fn;
    media.dma_alloc = test_media_dma_alloc;
    media.dma_free  = test_media_dma_free;

    media.geo.ngrps  	 = 8;
    media.geo.pu_grp 	 = 1;
    media.geo.zn_pu  	 = 512;
    media.geo.sec_zn 	 = 100000;
    media.geo.nbytes 	 = 512;
    media.geo.nbytes_oob = 0;

    CU_ASSERT (xapp_media_set (&media) == 0);
}

static void test_media_init (void)
{
    CU_ASSERT (xapp_media_init () == 0);
}

static void test_media_exit (void)
{
    CU_ASSERT (xapp_media_exit () == 0);
}

int main (void)
{
    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
	return CU_get_error();

    pSuite = CU_add_suite("Suite_media", cunit_media_init, cunit_media_exit);
    if (pSuite == NULL) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    if ((CU_add_test (pSuite, "Set the media layer", test_media_set) == NULL) ||
        (CU_add_test (pSuite, "Initialize media", test_media_init) == NULL) ||
	(CU_add_test (pSuite, "Close media", test_media_exit) == NULL)) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();

    return CU_get_error();
}
