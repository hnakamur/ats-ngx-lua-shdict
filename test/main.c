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

static void sleep_ms(size_t msec)
{
    struct timespec ts;

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;
    if (nanosleep(&ts, NULL) == -1) {
        fprintf(stderr, "nanosleep err: %s\n", strerror(errno));
    }
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

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key);
    double value = 1;
    char *err = NULL;
    int has_init = 1;
    double init = 0;
    long init_ttl = 1;
    int forcible = 0;
    int rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                             init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1, value);

    long got_ttl_ms = mps_shdict_get_ttl(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT64(init_ttl, got_ttl_ms);

    long init_ttl2 = 10;
    rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                         init_ttl2, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(2, value);

    got_ttl_ms = mps_shdict_get_ttl(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT64(init_ttl, got_ttl_ms);

    sleep_ms(2);

    init_ttl = 0;
    forcible = -1;
    value = 1;
    rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1, value);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    mps_shdict_close(dict);
}

void test_incr_not_found(void)
{
    mps_shdict_t *dict = open_shdict();

    const char *key = "key1";
    size_t key_len = strlen(key);
    double value = 1;
    char *err = NULL;
    int has_init = 0;
    double init = 0;
    long init_ttl = 1;
    int forcible = 0;
    int rc = mps_shdict_incr(dict, (const u_char *)key, key_len, &value, &err,
                             has_init, init, init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_EQUAL_STRING("not found", err);

    mps_shdict_close(dict);
}

void test_incr_reuse_expired_number(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    double value = 1;
    char *err = NULL;
    int has_init = 1;
    double init = 0;
    long init_ttl = 0;
    int forcible = 0;
    int rc = mps_shdict_incr(dict, key1, key1_len, &value, &err, has_init, init,
                             init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1, value);

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    init_ttl = 1;
    rc = mps_shdict_incr(dict, key2, key2_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1, value);

    sleep_ms(2);

    init_ttl = 0;
    forcible = -1;
    init = 1;
    value = 3;
    rc = mps_shdict_incr(dict, key2, key2_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(3 + 1, value);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    mps_shdict_close(dict);
}

void test_incr_reuse_expired_boolean(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    double num_value = 1;
    long exptime = 0;
    int rc = mps_shdict_set(dict, key2, key2_len, MPS_SHDICT_TBOOLEAN, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    exptime = 1;
    rc = mps_shdict_set(dict, key1, key1_len, MPS_SHDICT_TBOOLEAN, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep_ms(2);

    forcible = -1;
    int has_init = 1, init_ttl = 0;
    double value = 3, init = 1;
    rc = mps_shdict_incr(dict, key1, key1_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_DOUBLE(3 + 1, value);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    mps_shdict_close(dict);
}

void test_incr_not_a_number(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    double num_value = 1;
    long exptime = 0;
    int rc = mps_shdict_set(dict, key1, key1_len, MPS_SHDICT_TBOOLEAN, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    forcible = -1;
    int has_init = 1, init_ttl = 0;
    double value = 3, init = 1;
    rc = mps_shdict_incr(dict, key1, key1_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_EQUAL_STRING("not a number", err);

    mps_shdict_close(dict);
}

void test_incr_forcible(void)
{
    mps_shdict_t *dict = open_shdict();

    char key_buf[64];
    const u_char *key = (const u_char *)key_buf;
    size_t key_len;
    int has_init = 1, init_ttl = 0, forcible = 0, i, rc;
    double value, init = 1;
    char *err = NULL;
    for (i = 0; i < 63; i++) {
        if (i == 59) {
            verify_tree_enabled = 1;
        }
        key_len = sprintf(key_buf, "key%d", i);
        value = 3;
        rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                             init_ttl, &forcible);
        TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
        TEST_ASSERT_EQUAL_DOUBLE(1 + 3, value);
        TEST_ASSERT_EQUAL_INT(0, forcible);
        TSDebug(MPS_LOG_TAG,
                "test_incr_forcible i=%d before verify_tree "
                "==========================",
                i);
        verify_tree(dict->pool, &mps_shdict_tree(dict->pool)->rbtree);
        TSDebug(MPS_LOG_TAG,
                "test_incr_forcible i=%d after verify_tree "
                "==========================",
                i);
    }

    key_len = 63;
    memset(key_buf, 'k', key_len);
    key_buf[key_len] = '\0';
    value = 3;
    TSDebug(MPS_LOG_TAG,
            "test_incr_forcible calling incr for key=\"" LogLenStr "\"",
            (int)key_len, key);
    verify_tree(dict->pool, &mps_shdict_tree(dict->pool)->rbtree);

    rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1 + 3, value);
    TEST_ASSERT_EQUAL_INT(1, forcible);

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

void test_boolean_get_buf_short(void)
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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* get the true value */
    value_type = -1;
    num_value = 0;
    user_flags = 0;
    str_value_len = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_NULL(err);

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
    size_t key_len = strlen((const char *)key), str_value_len = 4000;
    u_char str_value_buf[4000];
    int value_type = MPS_SHDICT_TSTRING, user_flags = 0xcafe, forcible = 0;
    char *err = NULL;
    double num_value = 0;

    memset(str_value_buf, '\xa6', str_value_len);
    long exptime = 1;
    int rc = mps_shdict_safe_set(dict, key, key_len, value_type, str_value_buf,
                                 str_value_len, num_value, exptime, user_flags,
                                 &err, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep_ms(1);

    const u_char *key2 = (const u_char *)"a";
    size_t key2_len = strlen((const char *)key2);
    rc = mps_shdict_safe_set(dict, key2, key2_len, value_type, str_value_buf,
                             str_value_len, num_value, exptime, user_flags,
                             &err, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    mps_shdict_close(dict);
}

void test_safe_add(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key), str_value_len = 4000;
    u_char str_value_buf[4000];
    int value_type = MPS_SHDICT_TSTRING, user_flags = 0xcafe, forcible = 0;
    char *err = NULL;
    double num_value = 0;

    memset(str_value_buf, '\xa6', str_value_len);
    long exptime = 1;
    int rc = mps_shdict_add(dict, key, key_len, value_type, str_value_buf,
                            str_value_len, num_value, exptime, user_flags, &err,
                            &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep_ms(1);

    const u_char *key2 = (const u_char *)"a";
    size_t key2_len = strlen((const char *)key2);
    rc = mps_shdict_safe_add(dict, key2, key2_len, value_type, str_value_buf,
                             str_value_len, num_value, exptime, user_flags,
                             &err, &forcible);
    TEST_ASSERT_EQUAL_INT(0, rc);

    mps_shdict_close(dict);
}

void test_nil_add(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key);
    int user_flags = 0, forcible = 0;
    double num_value = 0;
    char *err = NULL;
    long exptime = 0;

    /* set a true value */
    int rc = mps_shdict_add(dict, key, key_len, MPS_SHDICT_TNIL, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_EQUAL_STRING("attempt to add or replace nil values", err);

    mps_shdict_close(dict);
}

void test_nil_set(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key);
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    long exptime = 0;

    /* set a true value */
    double num_value = 1;
    int rc = mps_shdict_set(dict, key, key_len, MPS_SHDICT_TBOOLEAN, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* set nil */
    rc = mps_shdict_set(dict, key, key_len, MPS_SHDICT_TNIL, NULL, 0, num_value,
                        exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    int value_type = -1, get_stale = 0, is_stale = 0;
    u_char *str_value_ptr = NULL;
    size_t str_value_len = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    mps_shdict_close(dict);
}

void test_add_exists(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    double num_value = 1;
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    long exptime = 0;
    int rc = mps_shdict_set(dict, key1, key1_len, MPS_SHDICT_TNUMBER, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    rc = mps_shdict_add(dict, key1, key1_len, MPS_SHDICT_TNUMBER, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_DECLINED, rc);
    TEST_ASSERT_EQUAL_STRING("exists", err);

    mps_shdict_close(dict);
}

void test_add_expired(void)
{
    mps_shdict_t *dict = open_shdict();

    /* This key is needed for mps_shdict_lookup to return NGX_DONE. */
    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    double num_value = 1;
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    long exptime = 0;
    int rc = mps_shdict_set(dict, key1, key1_len, MPS_SHDICT_TNUMBER, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    exptime = 1;
    rc = mps_shdict_set(dict, key2, key2_len, MPS_SHDICT_TNUMBER, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    sleep_ms(2);

    rc = mps_shdict_add(dict, key2, key2_len, MPS_SHDICT_TBOOLEAN, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    mps_shdict_close(dict);
}

void test_replace_expired(void)
{
    mps_shdict_t *dict = open_shdict();

    /* This key is needed for mps_shdict_lookup to return NGX_DONE. */
    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    double num_value = 1;
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    long exptime = 0;
    int rc = mps_shdict_set(dict, key1, key1_len, MPS_SHDICT_TNUMBER, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    exptime = 1;
    rc = mps_shdict_set(dict, key2, key2_len, MPS_SHDICT_TNUMBER, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    sleep_ms(2);

    num_value = 2;
    rc = mps_shdict_replace(dict, key2, key2_len, MPS_SHDICT_TNUMBER, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_DECLINED, rc);
    TEST_ASSERT_EQUAL_STRING("not found", err);

    mps_shdict_close(dict);
}

void test_replace_not_found(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key);
    double num_value = 1;
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    long exptime = 0;
    int rc =
        mps_shdict_replace(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                           num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_DECLINED, rc);
    TEST_ASSERT_EQUAL_STRING("not found", err);

    mps_shdict_close(dict);
}

void test_replace_with_ttl(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    double num_value = 1;
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    long exptime = 0;
    int rc = mps_shdict_set(dict, key1, key1_len, MPS_SHDICT_TNUMBER, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    num_value = 5;
    exptime = 1;
    rc = mps_shdict_replace(dict, key1, key1_len, MPS_SHDICT_TNUMBER, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    mps_shdict_close(dict);
}

void test_safe_set_no_key_no_mem(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    double num_value = 1;
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    long exptime = 0;
    size_t str_value_len = 4096;
    u_char str_value_buf[4096];
    memset(str_value_buf, '\xa6', str_value_len);
    int rc = mps_shdict_safe_set(dict, key1, key1_len, MPS_SHDICT_TSTRING,
                                 str_value_buf, str_value_len, num_value,
                                 exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_EQUAL_STRING("no memory", err);

    mps_shdict_close(dict);
}

void test_set_forcible(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    int user_flags = 0, forcible = 0;
    long exptime = 0;
    char *err = NULL;
    double num_value = 1;
    int rc = mps_shdict_set(dict, key1, key1_len, MPS_SHDICT_TBOOLEAN, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    size_t str_value_len = 2048;
    u_char str_value_buf[2048];
    memset(str_value_buf, '\xa6', str_value_len);
    rc = mps_shdict_set(dict, key2, key2_len, MPS_SHDICT_TSTRING, str_value_buf,
                        str_value_len, num_value, exptime, user_flags, &err,
                        &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    const u_char *key3 = (const u_char *)"key3";
    size_t key3_len = strlen((const char *)key3);
    rc = mps_shdict_set(dict, key3, key3_len, MPS_SHDICT_TSTRING, str_value_buf,
                        str_value_len, num_value, exptime, user_flags, &err,
                        &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, forcible);

    mps_shdict_close(dict);
}

void test_set_expire_no_mem(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    int user_flags = 0, forcible = 0;
    long exptime = 0;
    char *err = NULL;
    double num_value = 1;
    int rc = mps_shdict_set(dict, key1, key1_len, MPS_SHDICT_TBOOLEAN, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    size_t str_value_len = 2048;
    u_char str_value_buf[2048];
    memset(str_value_buf, '\xa6', str_value_len);
    rc = mps_shdict_set(dict, key2, key2_len, MPS_SHDICT_TSTRING, str_value_buf,
                        str_value_len, num_value, exptime, user_flags, &err,
                        &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    const u_char *key3 = (const u_char *)"key3";
    size_t key3_len = strlen((const char *)key3);
    size_t str_value2_len = 4096;
    u_char str_value2_buf[4096];
    memset(str_value2_buf, '\xa6', str_value2_len);
    rc = mps_shdict_set(dict, key3, key3_len, MPS_SHDICT_TSTRING,
                        str_value2_buf, str_value2_len, num_value, exptime,
                        user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_EQUAL_STRING("no memory", err);

    mps_shdict_close(dict);
}

void test_set_invalid_value_type(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key);
    double num_value = 0;
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    long exptime = 0;

    /* set a true value */
    int value_type = -234;
    int rc = mps_shdict_set(dict, key, key_len, value_type, NULL, 0, num_value,
                            exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_EQUAL_STRING("unsupported value type", err);

    mps_shdict_close(dict);
}

void test_get_ttl_set_expire(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1234";
    size_t key_len = strlen((const char *)key);
    int value_type = MPS_SHDICT_TNUMBER, user_flags = 0xcafe, get_stale = 0,
        is_stale = 0, forcible = 0;
    double num_value = 23.5;
    char *err = NULL;

    long got_ttl_ms = mps_shdict_get_ttl(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT64(NGX_DECLINED, got_ttl_ms);

    long exptime = 10;
    int rc = mps_shdict_set_expire(dict, key, key_len, exptime);
    TEST_ASSERT_EQUAL_INT(NGX_DECLINED, rc);

    exptime = 0;
    rc = mps_shdict_set(dict, key, key_len, value_type, NULL, 0, num_value,
                        exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    got_ttl_ms = mps_shdict_get_ttl(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT64(exptime, got_ttl_ms);

    exptime = 1;
    rc = mps_shdict_set_expire(dict, key, key_len, exptime);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    got_ttl_ms = mps_shdict_get_ttl(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT64(exptime, got_ttl_ms);

    sleep_ms(2);

    u_char *str_value_ptr = NULL;
    size_t str_value_len = 0;
    value_type = -1;
    num_value = -1;
    user_flags = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    get_stale = 1;
    is_stale = -1;
    value_type = -1;
    num_value = -1;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(1, is_stale);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNUMBER, value_type);
    TEST_ASSERT_EQUAL_DOUBLE(23.5, num_value);
    TEST_ASSERT_EQUAL_INT(0xcafe, user_flags);

    exptime = 0;
    rc = mps_shdict_set_expire(dict, key, key_len, exptime);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    value_type = -1;
    num_value = -1;
    user_flags = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(0, is_stale);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNUMBER, value_type);
    TEST_ASSERT_EQUAL_DOUBLE(23.5, num_value);
    TEST_ASSERT_EQUAL_INT(0xcafe, user_flags);

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

void test_memn2cmp(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ngx_memn2cmp((const u_char *)"foo",
                                           (const u_char *)"foobar", 3, 6));
    TEST_ASSERT_EQUAL_INT(
        0, ngx_memn2cmp((const u_char *)"foo", (const u_char *)"foo", 3, 3));
    TEST_ASSERT_EQUAL_INT(
        1, ngx_memn2cmp((const u_char *)"foobar", (const u_char *)"foo", 6, 3));
}

int main(void)
{
    UNITY_BEGIN();
    // RUN_TEST(test_add_exists);
    // RUN_TEST(test_add_expired);
    // RUN_TEST(test_nil_add);
    // RUN_TEST(test_nil_set);
    RUN_TEST(test_incr_forcible);
    // RUN_TEST(test_incr_happy);
    // RUN_TEST(test_incr_not_found);
    // RUN_TEST(test_incr_not_a_number);
    // RUN_TEST(test_incr_reuse_expired_number);
    // RUN_TEST(test_incr_reuse_expired_boolean);
    // RUN_TEST(test_boolean_happy);
    // RUN_TEST(test_boolean_get_buf_short);
    // RUN_TEST(test_number_happy);
    // RUN_TEST(test_string_happy);
    // RUN_TEST(test_safe_set);
    // RUN_TEST(test_safe_add);
    // RUN_TEST(test_get_ttl_set_expire);
    // RUN_TEST(test_flush_all);
    // RUN_TEST(test_capacity);
    // RUN_TEST(test_free_space);
    // RUN_TEST(test_set_invalid_value_type);
    // RUN_TEST(test_replace_expired);
    // RUN_TEST(test_replace_not_found);
    // RUN_TEST(test_replace_with_ttl);
    // RUN_TEST(test_set_forcible);
    // RUN_TEST(test_set_expire_no_mem);
    // RUN_TEST(test_memn2cmp);
    // RUN_TEST(test_safe_set_no_key_no_mem);
    return UNITY_END();
}
