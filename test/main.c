#include "unity.h"
#include "mps_shdict.h"

#define DICT_SIZE (4096 * 3)
#define DICT_NAME "test_dict1"
#define SHM_PATH  "/dev/shm/" DICT_NAME

mps_shdict_t  *dict = NULL;

void setUp(void) {
    fprintf(stderr, "setUp\n");
    dict = mps_shdict_open_or_create(DICT_NAME, DICT_SIZE, S_IRUSR | S_IWUSR);
}

void tearDown(void) {
    fprintf(stderr, "tearDown\n");
    mps_shdict_close(dict);
    if (unlink(SHM_PATH) == -1) {
        fprintf(stderr, "unlink shm file: %s\n", strerror(errno));
    }
}

void test_capacity(void) {
    size_t  capacity;
    
    capacity = mps_shdict_capacity(dict);
    TEST_ASSERT_EQUAL_UINT64(DICT_SIZE, capacity);
}

void test_free_space(void) {
    size_t  free_space;
    
    free_space = mps_shdict_free_space(dict);
    TEST_ASSERT_EQUAL_UINT64(4096, free_space);
}

// not needed when using generate_test_runner.rb
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_capacity);
    RUN_TEST(test_free_space);
    return UNITY_END();
}
