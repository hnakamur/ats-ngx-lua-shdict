#include "unity.h"
#include "mps_shdict.h"

#define DICT_SIZE (4096 * 3)
#define DICT_NAME "test_dict1"
#define SHM_PATH "/dev/shm/" DICT_NAME

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

void test_incr_happy(void)
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

void test_boolean_happy(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key), str_value_len = 1;
    int value_type = MPS_SHDICT_TBOOLEAN, user_flags = 0xbeaf, get_stale = 0,
        is_stale = 0, forcible = 0;
    u_char str_value_buf[1], *str_value_ptr = str_value_buf;
    double num_value = 1;
    char *err = NULL;
    long exptime = 0;

    /* set a true value */
    int rc = mps_shdict_set(dict, key, key_len, value_type, str_value_ptr,
                            str_value_len, num_value, exptime, user_flags, &err,
                            &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* get the true value */
    value_type = -1;
    num_value = 0;
    user_flags = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TBOOLEAN, value_type);
    TEST_ASSERT_EQUAL_UINT64(1, str_value_len);
    TEST_ASSERT_EQUAL_UINT8(1, str_value_ptr[0]);
    TEST_ASSERT_EQUAL_INT(0xbeaf, user_flags);

    /* replace with a false value */
    num_value = 0;
    rc = mps_shdict_replace(dict, key, key_len, value_type, str_value_ptr,
                            str_value_len, num_value, exptime, user_flags, &err,
                            &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* get the false value */
    value_type = -1;
    num_value = 1;
    user_flags = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TBOOLEAN, value_type);
    TEST_ASSERT_EQUAL_UINT64(1, str_value_len);
    TEST_ASSERT_EQUAL_UINT8(0, str_value_ptr[0]);
    TEST_ASSERT_EQUAL_INT(0xbeaf, user_flags);

    /* delete the value */
    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* verify the value is deleted */
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

void test_number_happy(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key), str_value_len = 0;
    int value_type = MPS_SHDICT_TNUMBER, user_flags = 0xcafe, get_stale = 0,
        is_stale = 0, forcible = 0;
    u_char *str_value_ptr = NULL;
    double num_value = 23.5;
    char *err = NULL;
    long exptime = 0;

    int rc = mps_shdict_set(dict, key, key_len, value_type, str_value_ptr,
                            str_value_len, num_value, exptime, user_flags, &err,
                            &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    value_type = -1;
    num_value = -1;
    user_flags = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNUMBER, value_type);
    TEST_ASSERT_EQUAL_DOUBLE(23.5, num_value);
    TEST_ASSERT_EQUAL_INT(0xcafe, user_flags);

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

void test_string_happy(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    const u_char *str_value_ptr = (const u_char *)"Hello, world!";
    size_t key_len = strlen((const char *)key),
           str_value_len = strlen((const char *)str_value_ptr);
    int value_type = MPS_SHDICT_TSTRING, user_flags = 0xcafe, get_stale = 0,
        is_stale = 0, forcible = 0;
    double num_value = 0;
    char *err = NULL;
    long exptime = 0;

    /* set a string value */
    int rc = mps_shdict_set(dict, key, key_len, value_type, str_value_ptr,
                            str_value_len, num_value, exptime, user_flags, &err,
                            &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* get the value */
    value_type = -1;
    user_flags = 0;
    u_char *str_value_ptr2 = NULL;
    size_t str_value_len2 = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr2,
                        &str_value_len2, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TSTRING, value_type);
    TEST_ASSERT_EQUAL_STRING(str_value_ptr, str_value_ptr2);
    TEST_ASSERT_EQUAL_UINT64(str_value_len, str_value_len2);
    TEST_ASSERT_EQUAL_INT(0xcafe, user_flags);
    free(str_value_ptr2);

    /* delete the value */
    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* verify the value is deleted */
    value_type = -1;
    str_value_ptr2 = NULL;
    str_value_len2 = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr2,
                        &str_value_len2, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    /* It is OK to call delete for non existing key. */
    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(0, rc);

    mps_shdict_close(dict);
}

void test_safe_set(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key), str_value_len = 1973;
    u_char str_value_buf[1973];
    int value_type = MPS_SHDICT_TSTRING, user_flags = 0xcafe, forcible = 0;
    char *err = NULL;
    double num_value = 0;
    long exptime = 0;

    memset(str_value_buf, '\x5a', str_value_len);
    int rc = mps_shdict_safe_set(dict, key, key_len, value_type, str_value_buf,
                                 str_value_len, num_value, exptime, user_flags,
                                 &err, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    // rc = mps_shdict_safe_set(dict, key, key_len, value_type, str_value_buf,
    //                          str_value_len, num_value, exptime, user_flags,
    //                          &err, &forcible);
    // TEST_ASSERT_EQUAL_INT(-1, rc);
    // TEST_ASSERT_EQUAL_STRING("hoge", err);

    mps_shdict_close(dict);
}

void test_flush_all(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    const u_char *str_value_ptr = (const u_char *)"Hello, world!";
    size_t key_len = strlen((const char *)key),
           str_value_len = strlen((const char *)str_value_ptr);
    int value_type = MPS_SHDICT_TSTRING, user_flags = 0xcafe, get_stale = 0,
        is_stale = 0, forcible = 0;
    double num_value = 0;
    char *err = NULL;
    long exptime = 0;

    /* set a string value */
    int rc = mps_shdict_set(dict, key, key_len, value_type, str_value_ptr,
                            str_value_len, num_value, exptime, user_flags, &err,
                            &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* set another string value */
    const u_char *key2 = (const u_char *)"a";
    const u_char *str_value2_ptr = (const u_char *)"some value";
    size_t key2_len = strlen((const char *)key2),
           str_value2_len = strlen((const char *)str_value2_ptr);
    rc = mps_shdict_set(dict, key2, key2_len, value_type, str_value2_ptr,
                        str_value2_len, num_value, exptime, user_flags, &err,
                        &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = mps_shdict_flush_all(dict);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* verify key and key2 are deleted */
    value_type = -1;
    u_char *got_str_value = NULL;
    size_t got_str_value_len = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &got_str_value,
                        &got_str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    value_type = -1;
    got_str_value = NULL;
    got_str_value_len = 0;
    rc = mps_shdict_get(dict, key2, key2_len, &value_type, &got_str_value,
                        &got_str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    mps_shdict_close(dict);
}

int main(void)
{
    UNITY_BEGIN();
    // RUN_TEST(test_incr_happy);
    // RUN_TEST(test_boolean_happy);
    // RUN_TEST(test_number_happy);
    // RUN_TEST(test_string_happy);
    RUN_TEST(test_safe_set);
    // RUN_TEST(test_flush_all);
    // RUN_TEST(test_capacity);
    // RUN_TEST(test_free_space);
    return UNITY_END();
}
