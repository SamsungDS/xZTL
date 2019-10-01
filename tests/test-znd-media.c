#include <xapp.h>
#include <xapp-media.h>
#include <znd-media.h>
#include <libznd.h>
#include <libxnvme_spec.h>
#include "CUnit/Basic.h"

extern struct xapp_core core;

static void cunit_znd_assert_int (char *fn, int status)
{
    CU_ASSERT (status == 0);
    if (status)
	printf ("\n %s: %x\n", fn, status);
}

static void cunit_znd_assert_int_equal (char *fn, int value, int expected)
{
    CU_ASSERT_EQUAL (value, expected);
    if (value != expected)
	printf ("\n %s: value %d != expected %d\n", fn, value, expected);
}

static int cunit_znd_media_init (void)
{
    return 0;
}

static int cunit_znd_media_exit (void)
{
    return 0;
}

static void test_znd_media_register (void)
{
    cunit_znd_assert_int ("znd_media_register", znd_media_register ());
}

static void test_znd_media_init (void)
{
    cunit_znd_assert_int ("xapp_media_init", xapp_media_init ());
}

static void test_znd_media_exit (void)
{
    cunit_znd_assert_int ("xapp_media_exit", xapp_media_exit ());
}

static void test_znd_report (void)
{
    struct xapp_zn_mcmd cmd;
    struct znd_report *report;
    struct xnvme_spec_log_zinf_zinfo *zinfo;
    int ret, zi, zone, nzones;
    uint32_t znlbas;

    cmd.opcode = XAPP_ZONE_MGMT_REPORT;
    cmd.addr.g.zone = zone = 0;
    cmd.nzones = nzones = core.media->geo.zn_dev;

    ret = xapp_media_submit_zn (&cmd);
    cunit_znd_assert_int ("xapp_media_exit", ret);

    if (!ret) {
	znlbas = core.media->geo.sec_zn;
	for (zi = zone; zi < zone + nzones; zi++) {
	    report = (struct znd_report *) cmd.opaque;
	    zinfo = ZND_REPORT_ZINFO(report, zi);
	    cunit_znd_assert_int_equal ("xapp_media_submit_zn:szlba",
					 zinfo->zslba, zi * znlbas);
	}
    }
}

int main (void)
{
    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
	return CU_get_error();

    pSuite = CU_add_suite("Suite_zn_media", cunit_znd_media_init, cunit_znd_media_exit);
    if (pSuite == NULL) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    if ((CU_add_test (pSuite, "Set the znd_media layer",
		      test_znd_media_register) == NULL) ||
        (CU_add_test (pSuite, "Initialize media",
		      test_znd_media_init) == NULL) ||
        (CU_add_test (pSuite, "Get/Check Log Page report",
		      test_znd_report) == NULL) ||
	(CU_add_test (pSuite, "Close media",
		      test_znd_media_exit) == NULL)) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();

    return CU_get_error();
}
