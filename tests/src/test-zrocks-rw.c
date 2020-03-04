#include <omp.h>
#include <stdint.h>
#include <stdlib.h>
#include <libzrocks.h>
#include <xapp.h>
#include "CUnit/Basic.h"

#define TBUFFER_SZ (1024 * 1024 * 2) // 2 MB
#define NTHREADS   1
#define TNWRITES   (1024 * 4) // 8GB
//#define NWRITES   (128097 * 16) // full device

static const char **devname;

static uint64_t buffer_sz = TBUFFER_SZ;
static uint64_t nwrites   = TNWRITES;

static void cunit_zrocksrw_assert_ptr (char *fn, void *ptr)
{
    CU_ASSERT ((uint64_t) ptr != 0);
    if (!ptr)
	printf ("\n %s: ptr %p\n", fn, ptr);
}

static void cunit_zrocksrw_assert_int (char *fn, uint64_t status)
{
    CU_ASSERT (status == 0);
    if (status)
	printf ("\n %s: %lx\n", fn, status);
}

static int cunit_zrocksrw_init (void)
{
    return 0;
}

static int cunit_zrocksrw_exit (void)
{
    return 0;
}

static void test_zrocksrw_init (void)
{
    int ret;

    ret = zrocks_init (*devname);
    cunit_zrocksrw_assert_int ("zrocks_init", ret);
}

static void test_zrocksrw_exit (void)
{
    zrocks_exit ();
}

static void test_zrockswr_fill_buffer (void *buf)
{
    uint32_t byte;
    uint8_t value = 0x1;

    for (byte = 0; byte < buffer_sz; byte += 16) {
	value += 0x1;
	memset (buf, value, 16);
    }
}

static void test_zrocksrw_write (void)
{
    struct zrocks_map **map;
    uint16_t *pieces;
    void *buf[NTHREADS];
    int bufi, th_i;
    int *ret;

    struct timespec ts_s;
    struct timespec ts_e;
    uint64_t start_ns;
    uint64_t end_ns;
    double seconds, mb;

    for (bufi = 0; bufi < NTHREADS; bufi++) {
	buf[bufi] = zrocks_alloc (buffer_sz);
	cunit_zrocksrw_assert_ptr ("zrocksrw_write:alloc", buf[bufi]);
	if (!buf[bufi])
	    goto FREE;

	test_zrockswr_fill_buffer (buf[bufi]);
    }

    map = NULL;
    pieces = NULL;
    ret = NULL;
    map = malloc(sizeof(struct zrocks_map *) * nwrites);
    pieces = malloc(sizeof(uint16_t) * nwrites);
    ret = malloc(sizeof(int) * nwrites);

    if (!map || !pieces || !ret)
	goto FREE;

    GET_NANOSECONDS(start_ns, ts_s);

    //#pragma omp parallel for num_threads(NTHREADS)
    for (th_i = 0; th_i < nwrites; th_i++) {
	ret[th_i] = zrocks_write (buf[th_i % NTHREADS], buffer_sz, 0, &map[th_i], &pieces[th_i]);
	cunit_zrocksrw_assert_int ("zrocksrw_write:write", ret[th_i]);
	if (!ret[th_i])
	    zrocks_free (map[th_i]);
    }

    GET_NANOSECONDS(end_ns, ts_e);
    seconds = (double) (end_ns - start_ns) / (double) 1000000000;
    mb = ( (double) nwrites * (double) buffer_sz ) / (double) 1024 / (double) 1024;

    printf ("\n");
    printf ("Written data: %.2lf MB\n", mb);
    printf ("Elapsed time: %.4lf sec\n", seconds);
    printf ("Bandwidth: %.4lf MB/s\n", mb / seconds);

FREE:
    while (bufi) {
	bufi--;
	zrocks_free (buf[bufi]);
    }
    if (map) free(map);
    if (pieces) free(pieces);
    if (ret) free(ret);
}

int main (int argc, const char **argv)
{
    if (argc < 2 || !memcmp(argv[1], "--help\0", strlen(argv[1]))) {
	printf (" Usage: zrocks-test-rw <DEV_PATH> <BUFFER_SIZE_IN_MB> <NUM_OF_BUFFERS>\n");
	printf ("\n   e.g.: test-zrocks-rw liou:/dev/nvme0n2 2 1024\n");
	printf ("         This command writes 2 GB to the device\n");
	return 0;
    }

    if (argc >= 4) {
	buffer_sz = (1024 * 1024) * atoi(argv[2]);
	nwrites = atoi(argv[3]);
    }

    devname = &argv[1];
    printf ("Device: %s\n", *devname);

    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
	return CU_get_error();

    pSuite = CU_add_suite("Suite_zrocks_rw", cunit_zrocksrw_init, cunit_zrocksrw_exit);
    if (pSuite == NULL) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    if ((CU_add_test (pSuite, "Initialize ZRocks",
		      test_zrocksrw_init) == NULL) ||
        (CU_add_test (pSuite, "Write Bandwidth",
		      test_zrocksrw_write) == NULL) ||
        (CU_add_test (pSuite, "Close ZRocks",
		      test_zrocksrw_exit) == NULL)) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();

    return CU_get_error();
}
