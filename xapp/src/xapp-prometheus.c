#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <xapp.h>

extern struct xapp_core core;

struct xapp_prometheus_stats {
    uint64_t written_bytes;
    uint64_t read_bytes;
    uint64_t io_count;
    uint64_t user_write_bytes;
    uint64_t zns_write_bytes;

    struct timespec ts_s;
    struct timespec ts_e;
    uint64_t us_s;
    uint64_t us_e;
};

static pthread_t th_flush;
static struct xapp_prometheus_stats pr_stats;
static uint8_t xapp_pr_running;

static void xapp_prometheus_file_int64 (const char *fname, uint64_t val)
{
    FILE *fp;

    fp = fopen(fname, "w+");
    if (fp) {
	fprintf(fp, "%lu", val);
	fclose(fp);
    }
}

static void xapp_prometheus_file_double (const char *fname, double val)
{
    FILE *fp;

    fp = fopen(fname, "w+");
    if (fp) {
	fprintf(fp, "%.6lf", val);
	fclose(fp);
    }
}

static void xapp_prometheus_reset (void)
{
    uint64_t write, read, io;
    double thput_w, thput_r, thput, wa;

    GET_MICROSECONDS(pr_stats.us_s, pr_stats.ts_s);

    write = pr_stats.written_bytes;
    read  = pr_stats.read_bytes;
    io    = pr_stats.io_count;

    xapp_atomic_int64_update (&pr_stats.written_bytes, 0);
    xapp_atomic_int64_update (&pr_stats.read_bytes, 0);
    xapp_atomic_int64_update (&pr_stats.io_count, 0);

    thput_w = (double) write / (double) 1048576;
    thput_r = (double) read / (double) 1048576;
    thput   = thput_w + thput_r;

    if (pr_stats.user_write_bytes) {
	wa  = (double) pr_stats.zns_write_bytes /
	      (double) pr_stats.user_write_bytes;
    } else {
	wa  = 1;
    }

    xapp_prometheus_file_double("/tmp/ztl_prometheus_thput_w", thput_w);
    xapp_prometheus_file_double("/tmp/ztl_prometheus_thput_r", thput_r);
    xapp_prometheus_file_double("/tmp/ztl_prometheus_thput", thput);
    xapp_prometheus_file_int64 ("/tmp/ztl_prometheus_iops", io);
    xapp_prometheus_file_double("/tmp/ztl_prometheus_wamp_ztl", wa);
}

void *xapp_prometheus_flush (void *arg)
{
    double wa;

    GET_MICROSECONDS(pr_stats.us_s, pr_stats.ts_s);

    xapp_pr_running = 1;
    while (xapp_pr_running) {
	usleep(1);
	GET_MICROSECONDS(pr_stats.us_e, pr_stats.ts_e);

	if (pr_stats.us_e - pr_stats.us_s >= 1000000) {
	    xapp_prometheus_reset();
	}
    }

    while (pr_stats.us_e - pr_stats.us_s < 1000000) {
	GET_MICROSECONDS(pr_stats.us_e, pr_stats.ts_e);
    }
    xapp_prometheus_reset();
    usleep(1200000);

    wa = (double) pr_stats.zns_write_bytes /
	 (double) pr_stats.user_write_bytes;
    xapp_prometheus_file_double("/tmp/ztl_prometheus_thput_w", 0);
    xapp_prometheus_file_double("/tmp/ztl_prometheus_thput_r", 0);
    xapp_prometheus_file_double("/tmp/ztl_prometheus_thput", 0);
    xapp_prometheus_file_int64 ("/tmp/ztl_prometheus_iops", 0);
    xapp_prometheus_file_double("/tmp/ztl_prometheus_wamp_ztl", 1);

    return NULL;
}

void xapp_prometheus_add_io (struct xapp_io_mcmd *cmd)
{
    uint32_t nsec = 0, i;

    for (i = 0; i < cmd->naddr; i++)
	nsec += cmd->nsec[i];

    switch (cmd->opcode) {
	case XAPP_ZONE_APPEND:
	    xapp_atomic_int64_update (&pr_stats.written_bytes,
				      pr_stats.written_bytes +
				      (nsec * core.media->geo.nbytes));
	    break;
	case XAPP_CMD_READ:
	    xapp_atomic_int64_update (&pr_stats.read_bytes,
				      pr_stats.read_bytes +
				      (nsec * core.media->geo.nbytes));
	    break;
	default:
	    return;
    }

    xapp_atomic_int64_update (&pr_stats.io_count, pr_stats.io_count + 1);
}

void xapp_prometheus_add_wa (uint64_t user_writes, uint64_t zns_writes)
{
    xapp_atomic_int64_update (&pr_stats.user_write_bytes, user_writes);
    xapp_atomic_int64_update (&pr_stats.zns_write_bytes, zns_writes);
}

void xapp_prometheus_exit (void)
{
    xapp_pr_running = 0;
    pthread_join(th_flush, NULL);
}

int xapp_prometheus_init (void) {
    memset(&pr_stats, 0, sizeof(struct xapp_prometheus_stats));

    if (pthread_create(&th_flush, NULL, xapp_prometheus_flush, NULL)) {
	log_err("xapp-prometheus: Flushing thread not started.");
	return -1;
    }

    while (!xapp_pr_running) {}

    return 0;
}
