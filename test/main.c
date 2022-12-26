#include "unity.h"
#include "mps_shdict.h"

#define DICT_SIZE (4096 * 3)
#define DICT_NAME "test_dict1"
#define SHM_PATH "/dev/shm/" DICT_NAME
#define VALUE_BUF_SIZE 4096

static void verify_shm_not_exist(const char *pathname)
{
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

static mps_shdict_t *open_shdict()
{
    return mps_shdict_open_or_create(DICT_NAME, DICT_SIZE, S_IRUSR | S_IWUSR);
}

void setUp(void)
{
    verify_shm_not_exist(SHM_PATH);
}

void tearDown(void)
{
    if (unlink(SHM_PATH) == -1) {
        fprintf(stderr, "unlink shm file: %s\n", strerror(errno));
    }
}

void test_capacity(void)
{
    mps_shdict_t *dict = open_shdict();
    size_t capacity = mps_shdict_capacity(dict);
    TEST_ASSERT_EQUAL_UINT64(DICT_SIZE, capacity);
    mps_shdict_close(dict);
}

void test_free_space(void)
{
    mps_shdict_t *dict = open_shdict();
    size_t free_space = mps_shdict_free_space(dict);
    TEST_ASSERT_EQUAL_UINT64(4096, free_space);
    mps_shdict_close(dict);
}

void test_incr(void)
{
    mps_shdict_t *dict = open_shdict();

    const char *key = "key1";
    size_t key_len = strlen(key);
    double value = 1;
    char *err = NULL;
    int has_init = 1;
    double init = 0;
    long init_ttl = 0;
    int forcible = 0;
    int rc = mps_shdict_incr(dict, (const u_char *)key, key_len, &value, &err,
                             has_init, init, init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1, value);

    rc = mps_shdict_incr(dict, (const u_char *)key, key_len, &value, &err,
                         has_init, init, init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(2, value);

    value = 1;
    rc = mps_shdict_incr(dict, (const u_char *)key, key_len, &value, &err,
                         has_init, init, init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(3, value);

    mps_shdict_close(dict);
}

void test_boolean(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key), str_value_len = VALUE_BUF_SIZE;
    int value_type = MPS_SHDICT_TBOOLEAN, user_flags = 0xbeaf, get_stale = 0,
        is_stale = 0, forcible = 0;
    u_char str_value_buf[1], *str_value_ptr = str_value_buf;
    double num_value = 1;
    char *err = NULL;
    long exptime = 0;

    int rc = mps_shdict_set(dict, key, key_len, value_type, str_value_ptr,
                            str_value_len, num_value, exptime, user_flags, &err,
                            &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    user_flags = 0;
    str_value_len = 1;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT64(1, str_value_len);
    TEST_ASSERT_EQUAL_UINT8(1, str_value_ptr[0]);
    TEST_ASSERT_EQUAL_INT(0xbeaf, user_flags);

    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(0, rc);

    value_type = -1;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    /* It is OK to call delete for non existing key. */
    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(0, rc);

    mps_shdict_close(dict);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_incr);
    RUN_TEST(test_boolean);
    RUN_TEST(test_capacity);
    RUN_TEST(test_free_space);
    return UNITY_END();
}
