#include <pthread.h>
#include <omp.h>
#include <xapp.h>
#include <xapp-mempool.h>
#include "CUnit/Basic.h"

static void cunit_mempool_assert_int (char *fn, int status)
{
    CU_ASSERT (status == 0);
    if (status)
	printf (" %s: %x\n", fn, status);
}

static int cunit_mempool_init (void)
{
    return 0;
}

static int cunit_mempool_exit (void)
{
    return 0;
}

static void test_mempool_init (void)
{
    cunit_mempool_assert_int ("xapp_mempool_init", xapp_mempool_init ());
}

static void test_mempool_create (void)
{
    uint16_t type, tid, ents;
    uint32_t ent_sz;

    type = XAPP_MEMPOOL_MCMD;
    tid = 0;
    ents = 128;
    ent_sz = 1024;

    cunit_mempool_assert_int ("xapp_mempool_create",
		xapp_mempool_create (type, tid, ents, ent_sz));
}

static void test_mempool_destroy (void)
{
    uint16_t type, tid;

    type = XAPP_MEMPOOL_MCMD;
    tid = 0;

    cunit_mempool_assert_int ("xapp_mempool_destroy",
			       xapp_mempool_destroy (type, tid));
}

static void test_mempool_create_mult (void)
{
    uint16_t type, tid, ents;
    uint32_t ent_sz;

    type = XAPP_MEMPOOL_MCMD;
    ents = 128;
    ent_sz = 1024;

    #pragma omp parallel for
    for (tid = 0; tid < 64; tid++) {
	cunit_mempool_assert_int ("xapp_mempool_create",
		xapp_mempool_create (type, tid, ents, ent_sz));
    }
}

static void test_mempool_exit (void)
{
    cunit_mempool_assert_int ("xapp_mempool_exit", xapp_mempool_exit ());
}

int main (void)
{
    CU_pSuite pSuite = NULL;

    if (CUE_SUCCESS != CU_initialize_registry())
	return CU_get_error();

    pSuite = CU_add_suite("Suite_mempool", cunit_mempool_init,
							cunit_mempool_exit);
    if (pSuite == NULL) {
	CU_cleanup_registry();
	return CU_get_error();
    }

    if ((CU_add_test (pSuite, "Initialize", test_mempool_init) == NULL) ||
        (CU_add_test (pSuite, "Create a mempool", test_mempool_create) == NULL) ||
	(CU_add_test (pSuite, "Destroy a mempool", test_mempool_destroy) == NULL) ||
	(CU_add_test (pSuite, "Create parallel mempools",
			       test_mempool_create_mult) == NULL) ||
	(CU_add_test (pSuite, "Closes the module", test_mempool_exit) == NULL)){
	CU_cleanup_registry();
	return CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();

    return CU_get_error();
}
