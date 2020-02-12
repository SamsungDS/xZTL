#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <xapp.h>

#define XAPP_STATS_IO_TYPES 11

extern struct xapp_core core;

struct xapp_stats_data {
    uint64_t io[XAPP_STATS_IO_TYPES];
};

static struct xapp_stats_data xapp_stats;

void xapp_stats_print_io (void)
{
    uint64_t tot_b, tot_b_w, tot_b_r;
    double wa;

    printf ("\n User I/O commands\n");
    printf ("   write  : %lu\n", xapp_stats.io[XAPP_STATS_APPEND_UCMD]);
    printf ("   read   : %lu\n", xapp_stats.io[XAPP_STATS_READ_UCMD]);

    printf ("\n Media I/O commands\n");
    printf ("   append : %lu\n", xapp_stats.io[XAPP_STATS_APPEND_MCMD]);
    printf ("   read   : %lu\n", xapp_stats.io[XAPP_STATS_READ_MCMD]);
    printf ("   reset  : %lu\n", xapp_stats.io[XAPP_STATS_RESET_MCMD]);

    tot_b_r = xapp_stats.io[XAPP_STATS_READ_BYTES_U];
    tot_b_w = xapp_stats.io[XAPP_STATS_APPEND_BYTES_U];
    tot_b = tot_b_w + tot_b_r;

    wa = tot_b_w;

    printf ("\n Data transferred (Application->ZTL): %.2f MB (%lu bytes)\n",
                            tot_b / (double) 1048576, (uint64_t) tot_b);
    printf ("   data written     : %10.2lf MB (%lu bytes)\n",
                    (double) tot_b_w / (double) 1048576, (uint64_t) tot_b_w);
    printf ("   data read        : %10.2lf MB (%lu bytes)\n",
                (double) tot_b_r / (double) 1048576, (uint64_t) tot_b_r);

    tot_b_r = xapp_stats.io[XAPP_STATS_READ_BYTES];
    tot_b_w = xapp_stats.io[XAPP_STATS_APPEND_BYTES];
    tot_b = tot_b_w + tot_b_r;

    wa = (double) tot_b_w / wa;

    printf ("\n Data transferred (ZTL->Media): %.2f MB (%lu bytes)\n",
                            tot_b / (double) 1048576, (uint64_t) tot_b);
    printf ("   data written     : %10.2lf MB (%lu bytes)\n",
                    (double) tot_b_w / (double) 1048576, (uint64_t) tot_b_w);
    printf ("   data read        : %10.2lf MB (%lu bytes)\n",
                (double) tot_b_r / (double) 1048576, (uint64_t) tot_b_r);

    printf ("\n Write Amplification: %.6lf\n", wa);
}

void xapp_stats_print_io_simple (void)
{
    uint64_t flush_w, app_w, padding_w;
    FILE *fp;

    flush_w = xapp_stats.io[XAPP_STATS_APPEND_BYTES];
    app_w = xapp_stats.io[XAPP_STATS_APPEND_BYTES_U];
    padding_w = flush_w - app_w;

    printf("\nZTL Application Writes : %.2f MB (%lu bytes)\n",
				    app_w / (double) 1048576, app_w);
    printf("ZTL Padding            : %.2f MB (%lu bytes)\n",
				    padding_w / (double) 1048576, padding_w);
    printf("ZTL Total Writes       : %.2f MB (%lu bytes)\n",
				    flush_w / (double) 1048576, flush_w);
    printf("ZTL write-amplification: %.6lf\n", (double) flush_w / (double) app_w);
    printf("\nRecycled Zones: %lu (%.2f MB, %lu bytes)\n",
	xapp_stats.io[XAPP_STATS_RECYCLED_ZONES],
	xapp_stats.io[XAPP_STATS_RECYCLED_BYTES] / (double) 1048576,
	xapp_stats.io[XAPP_STATS_RECYCLED_BYTES]);
    printf("Zone Resets   : %lu\n", xapp_stats.io[XAPP_STATS_RESET_MCMD]);
    printf("\n");

    fp = fopen ("/tmp/ztl_written_bytes", "w+");
    if (fp) {
	fprintf(fp, "%lu",flush_w);
	fclose(fp);
    }
    fp = fopen ("/tmp/app_written_bytes", "w+");
    if (fp) {
	fprintf(fp, "%lu",app_w);
	fclose(fp);
    }

}

void xapp_stats_add_io (struct xapp_io_mcmd *cmd)
{
    uint32_t nsec = 0, type_b, type_c, i;

    for (i = 0; i < cmd->naddr; i++)
	nsec += cmd->nsec[i];

    switch (cmd->opcode) {
	case XAPP_ZONE_APPEND:
	    type_b = XAPP_STATS_APPEND_BYTES;
	    type_c = XAPP_STATS_APPEND_MCMD;
	    break;
	case XAPP_CMD_READ:
	    type_b = XAPP_STATS_READ_BYTES;
	    type_c = XAPP_STATS_READ_MCMD;
	    break;
	default:
	    return;

    }

    xapp_atomic_int64_update (&xapp_stats.io[type_c],
					xapp_stats.io[type_c] + 1);
    xapp_atomic_int64_update (&xapp_stats.io[type_b],
		xapp_stats.io[type_b] + (nsec * core.media->geo.nbytes));

#if XAPP_PROMETHEUS
    /* Prometheus */
    xapp_prometheus_add_io (cmd);
#endif
}

void xapp_stats_inc (uint32_t type, uint64_t val)
{
    xapp_atomic_int64_update (&xapp_stats.io[type],
			       xapp_stats.io[type] + val);

#if XAPP_PROMETHEUS
    /* Prometheus */
    if (type == XAPP_STATS_APPEND_BYTES_U) {
	xapp_prometheus_add_wa(xapp_stats.io[XAPP_STATS_APPEND_BYTES_U],
			       xapp_stats.io[XAPP_STATS_APPEND_BYTES]);
    }
#endif
}

void xapp_stats_reset_io (void)
{
    uint32_t type_i;

    for (type_i = 0; type_i < XAPP_STATS_IO_TYPES; type_i++)
	xapp_atomic_int64_update (&xapp_stats.io[type_i], 0);
}

void xapp_stats_exit (void)
{
    xapp_prometheus_exit();
}

int xapp_stats_init (void)
{
    memset (xapp_stats.io, 0x0, sizeof(uint64_t) * XAPP_STATS_IO_TYPES);

    if (xapp_prometheus_init()) {
	log_err("xapp-stats: Prometheus not started.");
	return -1;
    }

    return 0;
}
