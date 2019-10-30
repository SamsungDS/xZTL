#include <xapp.h>
#include <xapp-media.h>
#include <xapp-ztl.h>
#include <lztl.h>
#include <unistd.h>

#define ZTL_MCMD_ENTS	 512
#define ZTL_WCA_SEC_MCMD 64

TAILQ_HEAD (ucmd_head, xapp_io_ucmd) ucmd_head;
static pthread_spinlock_t     	     ucmd_spin;
static struct xapp_mthread_ctx      *tctx;
static pthread_t		     wca_thread;
static uint8_t 			     wca_running;

static int ztl_wca_check_offset_seq (struct xapp_io_ucmd *ucmd)
{
    // Check offset sequence
    return 0;
}

static void ztl_wca_callback (struct xapp_io_mcmd *mcmd)
{
    struct xapp_io_ucmd *ucmd;

    ucmd = (struct xapp_io_ucmd *) mcmd->opaque;

    if (mcmd->status) {
	ucmd->status = mcmd->status;
    } else {
	ucmd->moffset[mcmd->sequence] = mcmd->paddr[0];
    }

    if (ucmd->completed == ucmd->nmcmd - 1) {

	/* If mapping is managed by the ZTL, we need to support multi-piece
	 * mapping objects. For now, we fail the I/O */
	if (!ucmd->app_md && ztl_wca_check_offset_seq (ucmd))
	    ucmd->status = XAPP_ZTL_APPEND_ERR;

	ucmd->completed++;

	if (ucmd->callback)
	    ucmd->callback (ucmd);

    } else {

	ucmd->completed++;

    }
}

static int ztl_wca_submit (struct xapp_io_ucmd *ucmd)
{
    pthread_spin_lock (&ucmd_spin);
    TAILQ_INSERT_TAIL (&ucmd_head, ucmd, entry);
    pthread_spin_unlock (&ucmd_spin);

    return 0;
}

static int ztl_wca_process_ucmd (struct xapp_io_ucmd *ucmd)
{
    /* Step 1: Support a single zone offset
     * Provisioning returns a single zone, for now
     * We submit many write to that zone
     * In the callback, we check the offset order
     * If the buffer is not written in the correct sequence, we fail the I/O
     *
     * Step 2:
     * If the buffer is out of order, the mapping must support multi-piece
     * objects. If the host is responsible for mapping, we return a list
     */

    // get mcmds from mempool
    // populate all mcmds
    // populate ucmd
    // submite all mcmds
    return 0;
}

static void *ztl_wca_write_th (void *arg)
{
    struct xapp_io_ucmd *ucmd;

    wca_running = 1;

    while (wca_running) {
NEXT:
	ucmd = TAILQ_FIRST (&ucmd_head);
	if (ucmd) {

	    pthread_spin_lock (&ucmd_spin);
	    TAILQ_REMOVE (&ucmd_head, ucmd, entry);
	    pthread_spin_unlock (&ucmd_spin);

	    ztl_wca_process_ucmd (ucmd);

	    goto NEXT;
	}

	sleep (1);
    }

    return NULL;
}

static int ztl_wca_init (void)
{
    TAILQ_INIT (&ucmd_head);

    /* Initialize thread media context
     * If more write threads are to be used, we need more contexts */
    tctx = xapp_ctx_media_init (0, ZTL_MCMD_ENTS);
    if (!tctx)
	return XAPP_ZTL_WCA_ERR;

    if (pthread_spin_init (&ucmd_spin, 0))
	goto TCTX;

    if (pthread_create (&wca_thread, NULL, ztl_wca_write_th, NULL))
	goto SPIN;

    return 0;

SPIN:
    pthread_spin_destroy (&ucmd_spin);
TCTX:
    xapp_ctx_media_exit (tctx);
    return XAPP_ZTL_WCA_ERR;
}

static void ztl_wca_exit (void)
{
    wca_running = 0;
    pthread_join (wca_thread, NULL);
    pthread_spin_destroy (&ucmd_spin);
    xapp_ctx_media_exit (tctx);
}

static struct app_wca_mod libztl_wca = {
    .mod_id      = LIBZTL_WCA,
    .name        = "LIBZTL-WCA",
    .init_fn     = ztl_wca_init,
    .exit_fn     = ztl_wca_exit,
    .submit_fn   = ztl_wca_submit,
    .callback_fn = ztl_wca_callback
};

void ztl_wca_register (void) {
    ztl_mod_register (ZTLMOD_WCA, LIBZTL_WCA, &libztl_wca);
}
