#include <xapp.h>
#include <xapp-media.h>
#include <zn-media.h>
#include "CUnit/Basic.h"

static int cunit_zn_media_init (void)
{
    return 0;
}

static int cunit_zn_media_exit (void)
{
    return 0;
}

static void test_zn_media_register (void)
{
    CU_ASSERT (zn_media_register () == 0);
}

static void test_zn_media_init (void)
{
    CU_ASSERT (xapp_media_init () == 0);
}

static void test_zn_media_exit (void)
{
    CU_ASSERT (xapp_media_exit () == 0);
}

int main (void)
{
    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
	return CU_get_error();

    pSuite = CU_add_suite("Suite_zn_media", cunit_zn_media_init, cunit_zn_media_exit);
    if (pSuite == NULL) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    if ((CU_add_test (pSuite, "Set the zn_media layer", test_zn_media_register) == NULL) ||
        (CU_add_test (pSuite, "Initialize media", test_zn_media_init) == NULL) ||
	(CU_add_test (pSuite, "Close media", test_zn_media_exit) == NULL)) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();

    return CU_get_error();
}
