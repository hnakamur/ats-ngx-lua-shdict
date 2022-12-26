#include "unity.h"
#include "mps_shdict.h"

#define DICT_SIZE (4096 * 3)
#define DICT_NAME "test_dict1"
#define SHM_PATH  "/dev/shm/" DICT_NAME

static void verifyShmNotExist(const char *pathname) {
    struct stat st;

    if (lstat(pathname, &st) == -1) {
        if (errno == ENOENT) {
            return;
        }

        fprintf(stderr, "stat failed: %s\n", strerror(errno));
        exit(1);
    }

    fprintf(stderr, "Please delete shm file \"%s\" before running tests.\n",
        SHM_PATH);
    exit(1);
}

static mps_shdict_t *openShdict() {
    return mps_shdict_open_or_create(DICT_NAME, DICT_SIZE, S_IRUSR | S_IWUSR);
}

void setUp(void) {
    verifyShmNotExist(SHM_PATH);
}

void tearDown(void) {
    if (unlink(SHM_PATH) == -1) {
        fprintf(stderr, "unlink shm file: %s\n", strerror(errno));
    }
}

void test_capacity(void) {
    mps_shdict_t *dict = openShdict();
    size_t capacity = mps_shdict_capacity(dict);
    TEST_ASSERT_EQUAL_UINT64(DICT_SIZE, capacity);
    mps_shdict_close(dict);
}

void test_free_space(void) {
    mps_shdict_t *dict = openShdict();
    size_t free_space = mps_shdict_free_space(dict);
    TEST_ASSERT_EQUAL_UINT64(4096, free_space);
    mps_shdict_close(dict);
}

// not needed when using generate_test_runner.rb
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_capacity);
    RUN_TEST(test_free_space);
    return UNITY_END();
}
