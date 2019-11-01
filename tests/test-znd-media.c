#include <unistd.h>
#include <pthread.h>
#include <xapp.h>
#include <xapp-media.h>
#include <ztl-media.h>
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

static void test_znd_manage_single (uint8_t op, uint8_t devop,
				    uint32_t zone, char *name)
{
    struct xapp_zn_mcmd cmd;
    struct znd_report *report;
    struct xnvme_spec_log_zinf_zinfo *zinfo;
    int ret;

    cmd.opcode = op;
    cmd.addr.g.zone = zone;

    ret = xapp_media_submit_zn (&cmd);
    cunit_znd_assert_int (name, ret);

//    if (!ret) {

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

	/* Free structure allocated by xnvme */
	xnvme_buf_virt_free (cmd.opaque);
 //   }
}

static void test_znd_op_cl_fi_re (void)
{
    uint32_t zone = 10;

    test_znd_manage_single (XAPP_ZONE_MGMT_RESET,
			    XNVME_SPEC_ZONE_COND_EMPTY,
			    zone,
			    "xapp_media_submit_znm:reset");
    test_znd_manage_single (XAPP_ZONE_MGMT_OPEN,
			    XNVME_SPEC_ZONE_COND_EOPEN,
			    zone,
			    "xapp_media_submit_znm:open");
    test_znd_manage_single (XAPP_ZONE_MGMT_CLOSE,
			    XNVME_SPEC_ZONE_COND_CLOSED,
			    zone,
			    "xapp_media_submit_znm:close");
    test_znd_manage_single (XAPP_ZONE_MGMT_FINISH,
			    XNVME_SPEC_ZONE_COND_FULL,
			    zone,
			    "xapp_media_submit_znm:finish");
    test_znd_manage_single (XAPP_ZONE_MGMT_RESET,
			    XNVME_SPEC_ZONE_COND_EMPTY,
			    zone,
			    "xapp_media_submit_znm:reset");
}

static void test_znd_asynch_ctx (void)
{
    struct xapp_misc_cmd cmd;
    struct xapp_mthread_ctx tctx;
    int ret;

    tctx.comp_active = 1;
    cmd.opcode = XAPP_MISC_ASYNCH_INIT;
    cmd.asynch.depth   = 128;

    /* This command takes a pointer to a pointer */
    cmd.asynch.ctx_ptr = &tctx;

    /* Create the context */
    ret = xapp_media_submit_misc (&cmd);
    cunit_znd_assert_int ("xapp_media_submit_misc:asynch-init", ret);
    cunit_znd_assert_ptr ("xapp_media_submit_misc:asynch-init:check",
			   tctx.asynch);

    if (tctx.asynch) {
	/* Get outstanding commands */
	/* cmd.asynch.count will contain the number of outstanding commands */
	cmd.opcode         = XAPP_MISC_ASYNCH_OUTS;
	/* This command takes a direct pointer */
	cmd.asynch.ctx_ptr = &tctx;
	ret = xapp_media_submit_misc (&cmd);
	cunit_znd_assert_int ("xapp_media_submit_misc:asynch-outs", ret);


	/* Poke the context */
	/* cmd.asynch.count will contain the number of processed commands */
	cmd.opcode         = XAPP_MISC_ASYNCH_POKE;
	cmd.asynch.ctx_ptr = &tctx;
	cmd.asynch.limit   = 0;
	ret = xapp_media_submit_misc (&cmd);
	cunit_znd_assert_int ("xapp_media_submit_misc:asynch-poke", ret);


	/* Wait for completions */
	/* cmd.asynch.count will contain the number of processed commands */
	cmd.opcode         = XAPP_MISC_ASYNCH_WAIT;
	cmd.asynch.ctx_ptr = &tctx;
	ret = xapp_media_submit_misc (&cmd);
	cunit_znd_assert_int ("xapp_media_submit_misc:asynch-wait", ret);


	/* Destroy the context */
	/* Stops the completion thread */
	tctx.comp_active = 0;
	cmd.opcode       = XAPP_MISC_ASYNCH_TERM;
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

static int outstanding;
static void test_znd_callback (void *arg)
{
   struct xapp_io_mcmd *cmd;

   cmd = (struct xapp_io_mcmd *) arg;
   cunit_znd_assert_int ("xapp_media_submit_io:cb", cmd->status);
   outstanding--;
}

static void test_znd_append_zone (void)
{
    struct xapp_mp_entry    *mp_cmd;
    struct xapp_io_mcmd     *cmd;
    struct xapp_mthread_ctx *tctx;
    uint16_t tid, ents, nlbas, zone;
    uint64_t phys, bsize;
    void *wbuf;
    int ret;

    tid     = 0;
    ents    = 128;
    nlbas   = 64;
    zone    = 510;
    bsize = nlbas * core.media->geo.nbytes;

    /* Initialize mempool module */
    ret = xapp_mempool_init ();
    cunit_znd_assert_int ("xapp_mempool_init", ret);
    if (ret)
	return;

    /* Initialize thread media context */
    tctx = xapp_ctx_media_init (tid, ents);
    cunit_znd_assert_ptr ("xapp_ctx_media_init", tctx);
    if (ret)
	goto MP;

    /* Reset the zone before appending */
    test_znd_manage_single (XAPP_ZONE_MGMT_RESET,
			    XNVME_SPEC_ZONE_COND_EMPTY,
			    zone,
			    "xapp_media_submit_znm:reset");

    /* Allocate DMA memory */
    wbuf = xapp_media_dma_alloc (bsize, &phys);
    cunit_znd_assert_ptr ("xapp_media_dma_alloc", wbuf);
    if (!wbuf)
	goto CTX;

    /* Get media command entry */
    mp_cmd = xapp_mempool_get (XAPP_MEMPOOL_MCMD, tid);
    cunit_znd_assert_ptr ("xapp_mempool_get", mp_cmd);
    if (!mp_cmd)
	goto DMA;

    /* Fill up command structure */
    cmd = (struct xapp_io_mcmd *) mp_cmd->opaque;
    cmd->opcode    = XAPP_ZONE_APPEND;
    cmd->synch     = 0;
    cmd->async_ctx = tctx;
    cmd->prp[0]    = (uint64_t) wbuf;
    cmd->nsec[0]   = nlbas;
    cmd->callback  = test_znd_callback;

    cmd->addr[0].g.zone = zone;

    /* Submit append */
    outstanding = 1;
    ret = xapp_media_submit_io (cmd);
    cunit_znd_assert_int ("xapp_media_submit_io", ret);

    /* Wait for completions */
    while (outstanding) {};

    /* Clear up */
    xapp_mempool_put (mp_cmd, XAPP_MEMPOOL_MCMD, tid);
DMA:
    xapp_media_dma_free (wbuf);
CTX:
    ret = xapp_ctx_media_exit (tctx);
    cunit_znd_assert_int ("xapp_ctx_media_exit", ret);
MP:
    ret = xapp_mempool_exit ();
    cunit_znd_assert_int ("xapp_mempool_exit", ret);
}

static void test_znd_read_zone (void)
{
    struct xapp_mp_entry    *mp_cmd;
    struct xapp_io_mcmd     *cmd;
    struct xapp_mthread_ctx *tctx;
    uint16_t tid, ents, nlbas, zone;
    uint64_t phys, bsize;
    void *wbuf;
    int ret;

    tid     = 0;
    ents    = 128;
    nlbas   = 64;
    zone    = 510;
    bsize = nlbas * core.media->geo.nbytes;

    /* Initialize mempool module */
    ret = xapp_mempool_init ();
    cunit_znd_assert_int ("xapp_mempool_init", ret);
    if (ret)
	return;

    /* Initialize thread media context */
    tctx = xapp_ctx_media_init (tid, ents);
    cunit_znd_assert_ptr ("xapp_ctx_media_init", tctx);
    if (ret)
	goto MP;

    /* Allocate DMA memory */
    wbuf = xapp_media_dma_alloc (bsize, &phys);
    cunit_znd_assert_ptr ("xapp_media_dma_alloc", wbuf);
    if (!wbuf)
	goto CTX;

    /* Get media command entry */
    mp_cmd = xapp_mempool_get (XAPP_MEMPOOL_MCMD, tid);
    cunit_znd_assert_ptr ("xapp_mempool_get", mp_cmd);
    if (!mp_cmd)
	goto DMA;

    /* Fill up command structure */
    cmd = (struct xapp_io_mcmd *) mp_cmd->opaque;
    cmd->opcode    = XAPP_CMD_READ;
    cmd->synch     = 0;
    cmd->async_ctx = tctx;
    cmd->prp[0]    = (uint64_t) wbuf;
    cmd->nsec[0]   = nlbas;
    cmd->callback  = test_znd_callback;

    /* We currently use sector only read addresses */
    cmd->addr[0].g.sect = zone * core.media->geo.sec_zn;

    /* Submit read */
    outstanding = 1;
    ret = xapp_media_submit_io (cmd);
    cunit_znd_assert_int ("xapp_media_submit_io", ret);

    /* Wait for completions */
    while (outstanding) {};

    /* Clear up */
    xapp_mempool_put (mp_cmd, XAPP_MEMPOOL_MCMD, tid);
DMA:
    xapp_media_dma_free (wbuf);
CTX:
    ret = xapp_ctx_media_exit (tctx);
    cunit_znd_assert_int ("xapp_ctx_media_exit", ret);
MP:
    ret = xapp_mempool_exit ();
    cunit_znd_assert_int ("", ret);
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
        (CU_add_test (pSuite, "Append 64 sectors to zone 510",
		      test_znd_append_zone) == NULL) ||
        (CU_add_test (pSuite, "Read 64 sectors from zone 510",
		      test_znd_read_zone) == NULL) ||
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
