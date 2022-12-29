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

void test_slab_alloc_32_bytes(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        SHM_NAME, SHM_SIZE, S_IRUSR | S_IWUSR, slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    void *p = mps_slab_alloc(pool, 32);
    TEST_ASSERT_NOT_NULL(p);

    mps_slab_free(pool, p);

    mps_slab_close(pool, SHM_SIZE);
    delete_shm_file("/dev/shm" SHM_NAME);
}

void test_slab_alloc_64_bytes(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        SHM_NAME, SHM_SIZE, S_IRUSR | S_IWUSR, slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    void *p = mps_slab_alloc(pool, 64);
    TEST_ASSERT_NOT_NULL(p);

    mps_slab_free(pool, p);

    mps_slab_close(pool, SHM_SIZE);
    delete_shm_file("/dev/shm" SHM_NAME);
}

void test_slab_alloc_two_pages(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        SHM_NAME, 4096 * 4, S_IRUSR | S_IWUSR, slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    void *p = mps_slab_alloc(pool, 4096 * 2);
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

typedef struct thread_info {
    pthread_t thread;
    int thread_num;
} thread_info;

static void *thread_start(void *arg)
{
    thread_info *tinfo = (thread_info *)arg;

    fprintf(stderr, "thread_start, thread_num=%d\n", tinfo->thread_num);

    mps_slab_pool_t *pool1 = mps_slab_open_or_create(
        SHM_NAME, SHM_SIZE, S_IRUSR | S_IWUSR, slab_on_init);
    fprintf(stderr, "thread_num=%d, pool1=%p\n", tinfo->thread_num, pool1);
    TEST_ASSERT_NOT_NULL(pool1);

    mps_slab_close(pool1, SHM_SIZE);

    return NULL;
}

void test_slab_open_or_create_multithread(void)
{
    const int num_threads = 2;
    thread_info tinfo[num_threads];
    pthread_attr_t attr;
    int err, i;

    err = pthread_attr_init(&attr);
    TEST_ASSERT_EQUAL_INT(0, err);

    for (i = 0; i < num_threads; i++) {
        tinfo[i].thread_num = i + 1;
        err = pthread_create(&tinfo[i].thread, &attr, &thread_start, &tinfo[i]);
        TEST_ASSERT_EQUAL_INT(0, err);
    }

    err = pthread_attr_destroy(&attr);
    TEST_ASSERT_EQUAL_INT(0, err);

    for (i = 0; i < num_threads; i++) {
        err = pthread_join(tinfo[i].thread, NULL);
        TEST_ASSERT_EQUAL_INT(0, err);
    }

    delete_shm_file("/dev/shm" SHM_NAME);
}
