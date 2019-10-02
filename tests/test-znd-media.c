#include <xapp.h>
#include <xapp-media.h>
#include <znd-media.h>
#include <libznd.h>
#include <libxnvme_spec.h>
#include <xapp-mempool.h>
#include "CUnit/Basic.h"

extern struct xapp_core core;

static void cunit_znd_assert_ptr (char *fn, void *ptr)
{
    CU_ASSERT ((uint64_t) ptr != 0);
    if (!ptr)
	printf ("\n %s: ptr %p\n", fn, ptr);
}

static void cunit_znd_assert_int (char *fn, uint64_t status)
{
    CU_ASSERT (status == 0);
    if (status)
	printf ("\n %s: %lx\n", fn, status);
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
    cunit_znd_assert_int ("xapp_media_submit_zn:report", ret);

    if (!ret) {
	znlbas = core.media->geo.sec_zn;
	for (zi = zone; zi < zone + nzones; zi++) {
	    report = (struct znd_report *) cmd.opaque;
	    zinfo = ZND_REPORT_ZINFO(report, zi);
	    cunit_znd_assert_int_equal ("xapp_media_submit_zn:report:szlba",
					 zinfo->zslba, zi * znlbas);
	}
    }

    /* Free structure allocated by xnvme */
    xnvme_buf_virt_free (cmd.opaque);
}

static void test_znd_manage_single (uint8_t op, uint8_t devop, char *name)
{
    struct xapp_zn_mcmd cmd;
    struct znd_report *report;
    struct xnvme_spec_log_zinf_zinfo *zinfo;
    int ret;

    cmd.opcode = op;
    cmd.addr.g.zone = 10;

    ret = xapp_media_submit_zn (&cmd);
    cunit_znd_assert_int (name, ret);

    if (!ret) {

	/* Verify log page */
	cmd.opcode = XAPP_ZONE_MGMT_REPORT;
	cmd.nzones = 1;

	ret = xapp_media_submit_zn (&cmd);
	cunit_znd_assert_int ("xapp_media_submit_zn:report", ret);

	if (!ret) {
	    report = (struct znd_report *) cmd.opaque;
	    zinfo = ZND_REPORT_ZINFO(report, cmd.addr.g.zone);
	    cunit_znd_assert_int_equal (name,
					zinfo->zc, devop);
	}
    }

    /* Free structure allocated by xnvme */
    xnvme_buf_virt_free (cmd.opaque);
}

static void test_znd_op_cl_fi_re (void)
{
    test_znd_manage_single (XAPP_ZONE_MGMT_OPEN,
			    XNVME_SPEC_ZONE_COND_EOPEN,
			    "xapp_media_submit_znm:open");
    test_znd_manage_single (XAPP_ZONE_MGMT_CLOSE,
			    XNVME_SPEC_ZONE_COND_CLOSED,
			    "xapp_media_submit_znm:close");
    test_znd_manage_single (XAPP_ZONE_MGMT_FINISH,
			    XNVME_SPEC_ZONE_COND_FULL,
			    "xapp_media_submit_znm:finish");
    test_znd_manage_single (XAPP_ZONE_MGMT_RESET,
			    XNVME_SPEC_ZONE_COND_EMPTY,
			    "xapp_media_submit_znm:reset");
}

static void test_znd_asynch_ctx (void)
{
    struct xapp_misc_cmd cmd;
    void *ctx_ptr;
    int ret;

    cmd.opcode = XAPP_MISC_ASYNCH_INIT;
    cmd.asynch.depth   = 128;
    /* This command takes a pointer to a pointer */
    cmd.asynch.ctx_ptr = (uint64_t) &ctx_ptr;

    /* Create the context */
    ret = xapp_media_submit_misc (&cmd);
    cunit_znd_assert_int ("xapp_media_submit_misc:asynch-init", ret);
    cunit_znd_assert_ptr ("xapp_media_submit_misc:asynch-init:check",
			   ctx_ptr);

    if (ctx_ptr) {
	/* Get outstanding commands */
	cmd.opcode = XAPP_MISC_ASYNCH_OUTS;
	/* This command takes a direct pointer */
	cmd.asynch.ctx_ptr = (uint64_t) ctx_ptr;
	ret = xapp_media_submit_misc (&cmd);
	cunit_znd_assert_int ("xapp_media_submit_misc:asynch-outs", ret);

	/* Poke the context */
	cmd.opcode = XAPP_MISC_ASYNCH_POKE;
	cmd.asynch.limit  = cmd.asynch.depth;
	ret = xapp_media_submit_misc (&cmd);
	cunit_znd_assert_int ("xapp_media_submit_misc:asynch-poke", ret);

	/* Wait for completions (should be 0) */
	cmd.opcode = XAPP_MISC_ASYNCH_WAIT;
	ret = xapp_media_submit_misc (&cmd);
	cunit_znd_assert_int ("xapp_media_submit_misc:asynch-wait", ret);

	/* Destroy the context */
	cmd.opcode = XAPP_MISC_ASYNCH_TERM;
	ret = xapp_media_submit_misc (&cmd);
	cunit_znd_assert_int ("xapp_media_submit_misc:asynch-term", ret);
    }
}

static void test_znd_dma_memory (void)
{
    void *buf;
    uint64_t phys;

    buf = xapp_media_dma_alloc (1024, &phys);
    cunit_znd_assert_ptr ("xapp_media_dma_alloc", buf);
    cunit_znd_assert_ptr ("xapp_media_dma_alloc:check-phys", (void *) phys);

    if (buf) {
	xapp_media_dma_free (buf);
	CU_PASS ("xapp_media_dma_free (buf);");
    }
}

static void test_znd_append_zone (void)
{
    int ret;

    /* Create a command pool */

    uint16_t type, tid, ents;
    uint32_t ent_sz;

    type = XAPP_MEMPOOL_MCMD;
    tid = 0;
    ents = 128;
    ent_sz = sizeof (struct xapp_zn_mcmd);

    ret = xapp_mempool_init ();
    cunit_znd_assert_int ("xapp_mempool_init", ret);
    if (ret)
	return;

    ret = xapp_mempool_create (type, tid, ents, ent_sz);
    cunit_znd_assert_int ("xapp_mempool_create", ret);
    if (ret)
	goto EXIT;

    /* TODO: Send Append commands here*/

//DESTROY:
    ret = xapp_mempool_destroy (type, tid);
    cunit_znd_assert_int ("xapp_mempool_destroy", ret);
EXIT:
    ret = xapp_mempool_exit ();
    cunit_znd_assert_int ("xapp_mempool_exit", ret);
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
        (CU_add_test (pSuite, "Open/Close/Finish/Reset a zone",
		      test_znd_op_cl_fi_re) == NULL) ||
        (CU_add_test (pSuite, "Init/Term asynchronous context",
		      test_znd_asynch_ctx) == NULL) ||
        (CU_add_test (pSuite, "Allocate/Free DMA aligned buffer",
		      test_znd_dma_memory) == NULL) ||
        (CU_add_test (pSuite, "Fill up a zone (APPEND)",
		      test_znd_append_zone) == NULL) ||
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
