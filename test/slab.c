#include "unity.h"
#include "mps_slab.h"

#define SHM_SIZE (4096 * 3)
#define SHM_NAME "/test_shm1"

extern void delete_shm_file(const char *name);

static void slab_on_init(mps_slab_pool_t *pool)
{
    pool->log_nomem = 0;
}

void test_slab_calloc_one_byte(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        SHM_NAME, SHM_SIZE, S_IRUSR | S_IWUSR, slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    void *p = mps_slab_calloc(pool, 1);
    TEST_ASSERT_NOT_NULL(p);

    mps_slab_free(pool, p);

    mps_slab_close(pool, SHM_SIZE);
    delete_shm_file("/dev/shm" SHM_NAME);
}

void test_slab_open_existing(void)
{
    mps_slab_pool_t *pool1 = mps_slab_open_or_create(
        SHM_NAME, SHM_SIZE, S_IRUSR | S_IWUSR, slab_on_init);
    TEST_ASSERT_NOT_NULL(pool1);

    mps_slab_pool_t *pool2 = mps_slab_open_or_create(
        SHM_NAME, SHM_SIZE, S_IRUSR | S_IWUSR, slab_on_init);
    TEST_ASSERT_NOT_NULL(pool2);

    mps_slab_close(pool2, SHM_SIZE);
    mps_slab_close(pool1, SHM_SIZE);

    delete_shm_file("/dev/shm" SHM_NAME);
}
