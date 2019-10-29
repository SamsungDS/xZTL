#include <xapp.h>
#include <xapp-media.h>
#include <xapp-ztl.h>
#include <lztl.h>

static void ztl_wca_callback (struct xapp_io_ucmd *ucmd)
{

}

static int ztl_wca_submit (struct xapp_io_ucmd *ucmd)
{
    return 0;
}

static int ztl_wca_init (void)
{
    return 0;
}

static void ztl_wca_exit (void)
{

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
