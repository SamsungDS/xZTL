#include <xapp.h>
#include <xapp-media.h>
#include <znd-media.h>
#include "CUnit/Basic.h"

static void cunit_zn_assert_int (char *fn, int status)
{
    CU_ASSERT (status == 0);
    if (status)
	printf (" %s: %x\n", fn, status);
}

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
    cunit_zn_assert_int ("zn_media_register", znd_media_register ());
}

static void test_zn_media_init (void)
{
    cunit_zn_assert_int ("xapp_media_init", xapp_media_init ());
}

static void test_zn_media_exit (void)
{
    cunit_zn_assert_int ("xapp_media_exit", xapp_media_exit ());
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
