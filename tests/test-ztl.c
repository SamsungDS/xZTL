#include <ztl-media.h>
#include <xapp.h>
#include <xapp-ztl.h>
#include <lztl.h>
#include "CUnit/Basic.h"

static void cunit_ztl_assert_ptr (char *fn, void *ptr)
{
    CU_ASSERT ((uint64_t) ptr != 0);
    if (!ptr)
	printf ("\n %s: ptr %p\n", fn, ptr);
}

static void cunit_ztl_assert_int (char *fn, uint64_t status)
{
    CU_ASSERT (status == 0);
    if (status)
	printf ("\n %s: %lx\n", fn, status);
}

static void cunit_ztl_assert_int_equal (char *fn, int value, int expected)
{
    CU_ASSERT_EQUAL (value, expected);
    if (value != expected)
	printf ("\n %s: value %d != expected %d\n", fn, value, expected);
}

static void test_ztl_init (void)
{
    int ret;

    ret = znd_media_register ();
    cunit_ztl_assert_int ("znd_media_register", ret);
    if (ret)
	return;

    ret = xapp_media_init ();
    cunit_ztl_assert_int ("xapp_media_init", ret);
    if (ret)
	return;

    /* Register ZTL modules */
    ztl_zmd_register ();
    ztl_pro_register ();
    ztl_mpe_register ();

    ret = ztl_init ();
    cunit_ztl_assert_int ("ztl_init", ret);
    if (ret)
	return;
}

static void test_ztl_exit (void)
{
    ztl_exit();
    xapp_media_exit();
}


static void test_ztl_pro_new_free (void)
{
    struct app_pro_addr *proe;
    struct app_zmd_entry *zmde;
    uint32_t nsec = 128;

    proe = ztl()->pro->new_fn (nsec, ZTL_PRO_TUSER);
    cunit_ztl_assert_ptr ("ztl()->pro->new_fn", proe);
    if (!proe)
	return;

    zmde = ztl()->zmd->get_fn (proe->grp, proe->addr[0].g.zone);
    cunit_ztl_assert_int_equal ("ztl()->pro->new_fn:zmd:wptr",
	    zmde->wptr, zmde->addr.g.sect + nsec);

    ztl()->pro->free_fn (proe);
}

static int cunit_ztl_init (void)
{
    return 0;
}

static int cunit_ztl_exit (void)
{
    return 0;
}

int main (void)
{
    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
	return CU_get_error();

    pSuite = CU_add_suite("Suite_ztl", cunit_ztl_init, cunit_ztl_exit);
    if (pSuite == NULL) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    if ((CU_add_test (pSuite, "Initialize ZTL",
		      test_ztl_init) == NULL) ||
	(CU_add_test (pSuite, "New/Free prov offset",
		      test_ztl_pro_new_free ) == NULL) ||
        (CU_add_test (pSuite, "Close ZTL",
		      test_ztl_exit) == NULL)) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();

    return CU_get_error();
}
