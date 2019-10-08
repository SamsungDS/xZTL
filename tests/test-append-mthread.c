#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <xapp.h>
#include <xapp-media.h>
#include <xapp-mempool.h>
#include <znd-media.h>

#define TEST_THREAD  	8
#define TEST_SEC_CMD 	64
#define TEST_DEPTH   	64

#define TEST_NZONES  	512
#define TEST_NSECT   	4096

extern struct xapp_core core;

struct test_params {
    uint16_t tid;
    uint32_t zone_s;
    uint32_t nzones;
    uint32_t left;
    uint32_t errors;
    struct timespec ts_s;
    struct timespec ts_e;
    uint64_t start_ns;
    uint64_t end_ns;
};

static struct test_params th_params[TEST_THREAD];

/* Prints statistics at runtime */
static void test_append_print_runtime (void)
{
    int tid, secs, perc, left;

    printf ("\rProgress:[");
    for (tid = 0; tid < TEST_THREAD; tid++) {

	secs = (TEST_NSECT / TEST_SEC_CMD) * th_params[tid].nzones;
	perc = (secs - th_params[tid].left - 1) * 100 / secs;
	if (th_params[tid].left < 1)
	    perc = 100;

	left = th_params[tid].left;
	if (left < 0)
	    left = 0;

	printf ("%d%% %dL", perc, left);

	if (tid < TEST_THREAD - 1)
	    printf (" | ");
	else
	    printf ("]                           ");
    }
    fflush (stdin);
}

/* Prints statistics at the end */
static void test_append_print (void)
{
    double iops, data, mb, sec, divb = 1024, divt = 1000;
    uint64_t time, lbas, iocount, err;
    int tid;

    time = 0;
    err = 0;
    for (tid = 0; tid < TEST_THREAD; tid++) {
	err     += th_params[tid].errors;
	time += th_params[tid].end_ns - th_params[tid].start_ns;
    }

    iocount = (TEST_NSECT / TEST_SEC_CMD) * TEST_NZONES;
    lbas = TEST_NSECT * TEST_NZONES;
    data = lbas * core.media->geo.nbytes;
    mb = data / divb / divb;
    time = time / TEST_THREAD;

    sec = (double) time / divt / divt / divt;
    iops = iocount / sec;
    printf ("\n\n Time elapsed  : %.2lf ms\n", (double) time / divt / divt);
    printf (    " Written LBAs  : %lu\n", lbas);
    printf (    " Written data  : %.2lf MB\n", mb);
    printf (    " Throughput    : %.2lf MB/s\n", mb / sec);
    printf (    " IOPS          : %.1f\n", iops);
    printf (    " Block size    : %d KB\n", core.media->geo.nbytes / 1024);
    printf (    " I/O size      : %d KB\n",
			    (TEST_SEC_CMD * core.media->geo.nbytes) / 1024);
    printf (    " Issued I/Os   : %lu\n", iocount);
    printf (    " Failed I/Os   : %lu\n\n", err);
}

static void test_append_callback (void *arg)
{
    struct xapp_mp_entry *mp_cmd;
    struct xapp_io_mcmd  *cmd;

    cmd = (struct xapp_io_mcmd *) arg;
    mp_cmd = (struct xapp_mp_entry *) cmd->opaque;

    if (cmd->status) {
	th_params[mp_cmd->tid].errors++;
    }

    xapp_mempool_put (mp_cmd, XAPP_MEMPOOL_MCMD, mp_cmd->tid);

    th_params[mp_cmd->tid].left--;

    if (th_params[mp_cmd->tid].left == 1) {
	GET_NANOSECONDS(th_params[mp_cmd->tid].end_ns,
		        th_params[mp_cmd->tid].ts_e);
    }
}

static int test_append_reset_zone (uint32_t zid)
{
    struct xapp_zn_mcmd cmd;

    cmd.opcode = XAPP_ZONE_MGMT_RESET;
    cmd.addr.g.zone = zid;

    return xapp_media_submit_zn (&cmd);
}

static void *test_append_th (void *args) {
    struct test_params      *par;
    struct xapp_mthread_ctx *tctx;
    struct xapp_mp_entry    *mp_cmd;
    struct xapp_io_mcmd     *cmd;
    size_t   cmd_sz;
    uint32_t cmd_i, ncmd, zone_i;
    uint64_t phys, sec_cmd;
    char    *wbuf;
    int ret;

    par     = (struct test_params *) args;
    cmd_sz  = TEST_SEC_CMD * core.media->geo.nbytes;
    ncmd    = TEST_NSECT / TEST_SEC_CMD;
    sec_cmd = TEST_SEC_CMD;

    /* Initialize thread media context */
    tctx = xapp_ctx_media_init (par->tid, TEST_DEPTH);
    if (!tctx)
	goto EXIT;

    /* Allocate DMA memory (+1 slot) */
    wbuf = xapp_media_dma_alloc (cmd_sz * ncmd + cmd_sz, &phys);
    if (!wbuf)
	goto CTX;

    par->left   += ncmd * par->nzones;
    par->errors = 0;

    GET_NANOSECONDS(par->start_ns, par->ts_s);

    for (zone_i = par->zone_s; zone_i < par->zone_s + par->nzones; zone_i++) {

	if (test_append_reset_zone (zone_i)) {
	    par->errors += ncmd;
	    par->left   -= ncmd;
	    printf (" Thread %d, Zone %d done. ERROR\n", par->tid, zone_i);
	    continue;
	}

	for (cmd_i = 0; cmd_i < ncmd; cmd_i++) {
RETRY:
	    mp_cmd = xapp_mempool_get (XAPP_MEMPOOL_MCMD, par->tid);
	    if (!mp_cmd) {
		usleep (10);
		goto RETRY;
	    }

	    cmd = (struct xapp_io_mcmd *) mp_cmd->opaque;
	    memset (cmd, 0x0, sizeof (struct xapp_io_mcmd));
	    cmd->opcode    = XAPP_ZONE_APPEND;
	    cmd->naddr	   = 1;
	    cmd->synch     = 0;
	    cmd->async_ctx = tctx;
	    cmd->prp[0]    = (uint64_t) (wbuf + cmd_sz * cmd_i);
	    cmd->nlba[0]   = sec_cmd;
	    cmd->callback  = test_append_callback;
	    cmd->opaque    = (void *) mp_cmd;

	    cmd->addr[0].g.zone = zone_i;
	    ret = xapp_media_submit_io (cmd);
	    if (ret) {
	 	par->errors++;
		par->left--;
		xapp_mempool_put (mp_cmd, XAPP_MEMPOOL_MCMD, par->tid);
	    }
	}
    }

    /* Wait until all commands complete */
    while (par->left > 1) {
	usleep (1);
    }

    xapp_media_dma_free (wbuf);
CTX:
    xapp_ctx_media_exit (tctx);
EXIT:
    par->left--;
    return NULL;
}

int main () {
    pthread_t thread_id[TEST_THREAD];
    uint32_t tid, zone_th, left;
    int ret, err;

    /* Register the ZNS media layer */
    znd_media_register ();

    /* Initialize media */
    ret = xapp_media_init ();
    if (ret)
	return -1;

    /* Initialize mempool module */
    ret = xapp_mempool_init ();
    if (ret)
	goto MEDIA;

    zone_th = TEST_NZONES / TEST_THREAD;

    printf ("\n-- Threads         : %d\n", TEST_THREAD);
    printf ("-- Zones per thread: %d\n", zone_th);
    printf ("\n Starting...\n");

    for (tid = 0; tid < TEST_THREAD; tid++) {
	th_params[tid].tid    = tid;
	th_params[tid].zone_s = tid * zone_th;
	th_params[tid].nzones = zone_th;
	th_params[tid].left   = 1;
	th_params[tid].errors = 0;

	ret = pthread_create (&thread_id[tid],
			NULL,
			test_append_th,
			(void *) &th_params[tid]);
	if (ret) {
	    printf ("Thread %d NOT started.\n", tid);
	}
    }

    printf ("\n");

    /* Wait until all threads complete */
    do {
	usleep (10000);
	left = 0;

	for (tid = 0; tid < TEST_THREAD; tid++)
	    left += th_params[tid].left;

	test_append_print_runtime ();

	if (!left)
	    break;
    } while (1);

    // print stats
    err = 0;
    for (tid = 0; tid < TEST_THREAD; tid++)
	err += th_params[tid].errors;
    printf ("\n\nDone. Errors: %d", err);
    test_append_print ();

    xapp_mempool_exit ();
MEDIA:
    xapp_media_exit ();
    return 0;
}
