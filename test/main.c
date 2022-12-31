#include "unity.h"
#include <ngx_murmurhash.h>
#include "mps_shdict.h"
#include "mps_log.h"

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

        mps_log_debug("mps_shdict_test", "stat failed: %s\n", strerror(errno));
        exit(1);
    }

    mps_log_debug("mps_shdict_test",
                  "Please delete shm file \"%s\" before running tests.\n",
                  SHM_PATH);
    exit(1);
}

static mps_shdict_t *open_shdict()
{
    return mps_shdict_open_or_create(
        DICT_NAME, DICT_SIZE, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR);
}

void delete_shm_file(const char *name)
{
    if (unlink(name) == -1 && errno != ENOENT) {
        mps_log_debug("mps_shdict_test", "unlink shm file: %s\n",
                      strerror(errno));
    }
}

static void sleep_till_next_ms()
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        mps_log_error("clock_gettime, %s", strerror(errno));
        return;
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 1000000 - (ts.tv_nsec % 1000000);
    if (ts.tv_nsec <= 0) {
        return;
    }
    if (nanosleep(&ts, NULL) == -1) {
        mps_log_error("nanosleep err: %s\n", strerror(errno));
    }
}

static void sleep_ms(size_t msec)
{
    struct timespec ts;

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;
    if (nanosleep(&ts, NULL) == -1) {
        mps_log_error("nanosleep err: %s\n", strerror(errno));
    }
}

void setUp(void)
{
    verify_shm_not_exist(SHM_PATH);
}

void tearDown(void)
{
    delete_shm_file(SHM_PATH);
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

    sleep_till_next_ms();

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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1, value);

    long got_ttl_ms = mps_shdict_get_ttl(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT64(init_ttl, got_ttl_ms);

    long init_ttl2 = 10;
    rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                         init_ttl2, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_DOUBLE(2, value);

    got_ttl_ms = mps_shdict_get_ttl(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT64(init_ttl, got_ttl_ms);

    sleep_ms(2);

    init_ttl = 0;
    forcible = -1;
    value = 1;
    rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
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

    sleep_till_next_ms();

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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1, value);

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    init_ttl = 1;
    rc = mps_shdict_incr(dict, key2, key2_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1, value);

    sleep_ms(2);

    init_ttl = 0;
    forcible = -1;
    init = 1;
    value = 3;
    rc = mps_shdict_incr(dict, key2, key2_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_DOUBLE(3 + 1, value);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    mps_shdict_close(dict);
}

void test_incr_reuse_expired_boolean(void)
{
    mps_shdict_t *dict = open_shdict();

    sleep_till_next_ms();

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    int user_flags = 0, forcible = 0;
    char *err = NULL;
    double num_value = 1;
    long exptime = 0;
    int rc = mps_shdict_set(dict, key2, key2_len, MPS_SHDICT_TBOOLEAN, NULL, 0,
                            num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    const u_char *key1 = (const u_char *)"key1";
    size_t key1_len = strlen((const char *)key1);
    exptime = 1;
    rc = mps_shdict_set(dict, key1, key1_len, MPS_SHDICT_TBOOLEAN, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    sleep_ms(2);

    forcible = -1;
    int has_init = 1, init_ttl = 0;
    double value = 3, init = 1;
    rc = mps_shdict_incr(dict, key1, key1_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_DOUBLE(3 + 1, value);
    TEST_ASSERT_EQUAL_INT(0, forcible);

    mps_shdict_close(dict);
}

void test_incr_remove_expired_list(void)
{
    mps_shdict_t *dict = open_shdict();

    sleep_till_next_ms();

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    int n;
    char *err = NULL;
    double num_value;

    num_value = 3;
    n = mps_shdict_lpush(dict, key2, key2_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    const u_char *key = (const u_char *)"key";
    size_t key_len = strlen((const char *)key);

    num_value = 1.5;
    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    num_value = 1.5;
    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(2, n);

    long exptime = 1;
    int rc = mps_shdict_set_expire(dict, key, key_len, exptime);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    sleep_ms(2);

    int has_init = 1, init_ttl = 0, forcible = -1;
    double value = 3, init = 1;
    rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

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
        key_len = sprintf(key_buf, "key%d", i);
        value = 3;
        rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                             init_ttl, &forcible);
        TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
        TEST_ASSERT_EQUAL_DOUBLE(1 + 3, value);
        TEST_ASSERT_EQUAL_INT(0, forcible);
    }

    key_len = sprintf(key_buf, "key%d", i);
    value = 3;

    rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_DOUBLE(1 + 3, value);
    TEST_ASSERT_EQUAL_INT(1, forcible);

    mps_shdict_close(dict);
}

void test_incr_no_memory(void)
{
    mps_shdict_t *dict = open_shdict();

    char key_buf[64];
    const u_char *key = (const u_char *)key_buf;
    size_t key_len;
    int has_init = 1, init_ttl = 0, forcible = 0, i, rc;
    double value, init = 1;
    char *err = NULL;
    for (i = 0; i < 63; i++) {
        key_len = sprintf(key_buf, "key%d", i);
        value = 3;
        rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                             init_ttl, &forcible);
        TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
        TEST_ASSERT_EQUAL_DOUBLE(1 + 3, value);
        TEST_ASSERT_EQUAL_INT(0, forcible);
    }

    key_len = 63;
    memset(key_buf, 'k', key_len);
    key_buf[key_len] = '\0';
    value = 3;

    rc = mps_shdict_incr(dict, key, key_len, &value, &err, has_init, init,
                         init_ttl, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_EQUAL_STRING("no memory", err);

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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* get the true value */
    value_type = -1;
    num_value = 0;
    user_flags = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TBOOLEAN, value_type);
    TEST_ASSERT_EQUAL_UINT64(1, str_value_len);
    TEST_ASSERT_EQUAL_UINT8(1, str_value_ptr[0]);
    TEST_ASSERT_EQUAL_INT(0xbeaf, user_flags);

    /* replace with a false value */
    num_value = 0;
    rc = mps_shdict_replace(dict, key, key_len, value_type, str_value_ptr,
                            str_value_len, num_value, exptime, user_flags, &err,
                            &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* get the false value */
    value_type = -1;
    num_value = 1;
    user_flags = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TBOOLEAN, value_type);
    TEST_ASSERT_EQUAL_UINT64(1, str_value_len);
    TEST_ASSERT_EQUAL_UINT8(0, str_value_ptr[0]);
    TEST_ASSERT_EQUAL_INT(0xbeaf, user_flags);

    /* delete the value */
    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* verify the value is deleted */
    value_type = -1;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    /* It is OK to call delete for non existing key. */
    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    value_type = -1;
    num_value = -1;
    user_flags = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNUMBER, value_type);
    TEST_ASSERT_EQUAL_DOUBLE(23.5, num_value);
    TEST_ASSERT_EQUAL_INT(0xcafe, user_flags);

    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    value_type = -1;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    /* It is OK to call delete for non existing key. */
    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* get the value */
    value_type = -1;
    user_flags = 0;
    u_char *str_value_ptr2 = NULL;
    size_t str_value_len2 = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr2,
                        &str_value_len2, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TSTRING, value_type);
    TEST_ASSERT_EQUAL_UINT64(str_value_len, str_value_len2);
    TEST_ASSERT_EQUAL_MEMORY(str_value_ptr, str_value_ptr2, str_value_len2);
    TEST_ASSERT_EQUAL_INT(0xcafe, user_flags);
    free(str_value_ptr2);

    /* delete the value */
    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* verify the value is deleted */
    value_type = -1;
    str_value_ptr2 = NULL;
    str_value_len2 = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr2,
                        &str_value_len2, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    /* It is OK to call delete for non existing key. */
    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    mps_shdict_close(dict);
}

void test_safe_set(void)
{
    mps_shdict_t *dict = open_shdict();

    sleep_till_next_ms();

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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    sleep_ms(1);

    const u_char *key2 = (const u_char *)"a";
    size_t key2_len = strlen((const char *)key2);
    rc = mps_shdict_safe_set(dict, key2, key2_len, value_type, str_value_buf,
                             str_value_len, num_value, exptime, user_flags,
                             &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    mps_shdict_close(dict);
}

void test_safe_add(void)
{
    mps_shdict_t *dict = open_shdict();

    sleep_till_next_ms();

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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    sleep_ms(1);

    const u_char *key2 = (const u_char *)"a";
    size_t key2_len = strlen((const char *)key2);
    rc = mps_shdict_safe_add(dict, key2, key2_len, value_type, str_value_buf,
                             str_value_len, num_value, exptime, user_flags,
                             &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

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

    sleep_till_next_ms();

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

    sleep_till_next_ms();

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

    sleep_till_next_ms();

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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    get_stale = 1;
    is_stale = -1;
    value_type = -1;
    num_value = -1;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
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
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, is_stale);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNUMBER, value_type);
    TEST_ASSERT_EQUAL_DOUBLE(23.5, num_value);
    TEST_ASSERT_EQUAL_INT(0xcafe, user_flags);

    mps_shdict_close(dict);
}

void test_list_basics(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key), str_value_len;
    u_char str_value_buf[2048], *str_value_ptr;
    int value_type, n, rc;
    double num_value;
    char *err = NULL;

    num_value = 1.5;
    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TSTRING,
                         (const u_char *)"value1", strlen("value1"), 0, &err);
    TEST_ASSERT_EQUAL_INT(2, n);

    n = mps_shdict_rpush(dict, key, key_len, MPS_SHDICT_TSTRING,
                         (const u_char *)"value2", strlen("value2"), 0, &err);
    TEST_ASSERT_EQUAL_INT(3, n);

    n = mps_shdict_llen(dict, key, key_len, &err);
    TEST_ASSERT_EQUAL_INT(3, n);

    str_value_ptr = NULL;
    str_value_len = 0;
    rc = mps_shdict_lpop(dict, key, key_len, &value_type, &str_value_ptr,
                         &str_value_len, &num_value, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TSTRING, value_type);
    TEST_ASSERT_EQUAL_UINT64(strlen("value1"), str_value_len);
    TEST_ASSERT_EQUAL_MEMORY("value1", str_value_ptr, str_value_len);
    free(str_value_ptr);

    n = mps_shdict_llen(dict, key, key_len, &err);
    TEST_ASSERT_EQUAL_INT(2, n);

    str_value_ptr = str_value_buf;
    str_value_len = 2048;
    rc = mps_shdict_rpop(dict, key, key_len, &value_type, &str_value_ptr,
                         &str_value_len, &num_value, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TSTRING, value_type);
    TEST_ASSERT_EQUAL_UINT64(strlen("value2"), str_value_len);
    TEST_ASSERT_EQUAL_MEMORY("value2", str_value_ptr, str_value_len);

    n = mps_shdict_llen(dict, key, key_len, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    str_value_ptr = NULL;
    str_value_len = 0;
    rc = mps_shdict_lpop(dict, key, key_len, &value_type, &str_value_ptr,
                         &str_value_len, &num_value, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNUMBER, value_type);
    TEST_ASSERT_EQUAL_DOUBLE(1.5, num_value);

    n = mps_shdict_llen(dict, key, key_len, &err);
    TEST_ASSERT_EQUAL_INT(0, n);

    str_value_ptr = NULL;
    str_value_len = 0;
    rc = mps_shdict_lpop(dict, key, key_len, &value_type, &str_value_ptr,
                         &str_value_len, &num_value, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    mps_shdict_close(dict);
}

void test_list_delete(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key);
    int n, rc;
    double num_value;
    char *err = NULL;

    num_value = 1.5;
    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TSTRING,
                         (const u_char *)"value1", strlen("value1"), 0, &err);
    TEST_ASSERT_EQUAL_INT(2, n);

    rc = mps_shdict_delete(dict, key, key_len);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    mps_shdict_close(dict);
}

void test_list_removed_expired_not_list_value(void)
{
    mps_shdict_t *dict = open_shdict();

    sleep_till_next_ms();

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    int n, rc;
    double num_value;
    int user_flags = 0, forcible = 0;
    long exptime;
    char *err = NULL;

    num_value = 1.5;
    exptime = 0;
    rc = mps_shdict_set(dict, key2, key2_len, MPS_SHDICT_TNUMBER, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key);

    exptime = 1;
    rc = mps_shdict_set(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    sleep_ms(2);

    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    mps_shdict_close(dict);
}

void test_list_removed_expired_list_value(void)
{
    mps_shdict_t *dict = open_shdict();

    sleep_till_next_ms();

    const u_char *key2 = (const u_char *)"key2";
    size_t key2_len = strlen((const char *)key2);
    int n, rc;
    double num_value;
    int user_flags = 0, forcible = 0;
    long exptime;
    char *err = NULL;

    num_value = 1.5;
    exptime = 0;
    rc = mps_shdict_set(dict, key2, key2_len, MPS_SHDICT_TNUMBER, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key);
    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(2, n);

    exptime = 1;
    rc = mps_shdict_set_expire(dict, key, key_len, exptime);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    sleep_ms(2);

    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    mps_shdict_close(dict);
}

void test_list_get_err(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key);
    int n, rc;
    double num_value;
    char *err = NULL;

    num_value = 1.5;
    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TSTRING,
                         (const u_char *)"value1", strlen("value1"), 0, &err);
    TEST_ASSERT_EQUAL_INT(2, n);

    u_char *str_value_ptr = NULL;
    size_t str_value_len = 0;
    int value_type = -1;
    int user_flags = 0, get_stale = 0, is_stale = 0;
    num_value = -1;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &str_value_ptr,
                        &str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_EQUAL_STRING("value is a list", err);

    mps_shdict_close(dict);
}

void test_list_push_bad_type_err(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key);
    int n;
    char *err = NULL;

    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNIL, NULL, 0, 0, &err);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, n);
    TEST_ASSERT_EQUAL_STRING("unsupported value type", err);

    err = NULL;
    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TBOOLEAN, NULL, 0, 1,
                         &err);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, n);
    TEST_ASSERT_EQUAL_STRING("unsupported value type", err);
}

void test_list_pop_not_list_err(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key);
    int rc;
    double num_value;
    int user_flags = 0, forcible = 0;
    long exptime = 0;
    char *err = NULL;

    num_value = 1.5;
    rc = mps_shdict_set(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    int value_type = -1;
    u_char *str_value_ptr = NULL;
    size_t str_value_len = 0;
    rc = mps_shdict_lpop(dict, key, key_len, &value_type, &str_value_ptr,
                         &str_value_len, &num_value, &err);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, rc);
    TEST_ASSERT_EQUAL_STRING("value not a list", err);

    mps_shdict_close(dict);
}

void test_list_push_not_list_err(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key);
    int n, rc;
    double num_value;
    int user_flags = 0, forcible = 0;
    long exptime = 0;
    char *err = NULL;

    num_value = 1.5;
    rc = mps_shdict_set(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    err = NULL;
    num_value = 3.5;
    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, n);
    TEST_ASSERT_EQUAL_STRING("value not a list", err);

    mps_shdict_close(dict);
}

void test_list_llen_not_list_err(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key);
    int n, rc;
    double num_value;
    int user_flags = 0, forcible = 0;
    long exptime = 0;
    char *err = NULL;

    num_value = 1.5;
    rc = mps_shdict_set(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                        num_value, exptime, user_flags, &err, &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    n = mps_shdict_llen(dict, key, key_len, &err);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, n);
    TEST_ASSERT_EQUAL_STRING("value not a list", err);

    mps_shdict_close(dict);
}

void test_list_push_no_memory_for_new_list(void)
{
    mps_shdict_t *dict = open_shdict();

    const u_char *key = (const u_char *)"key1";
    size_t key_len = strlen((const char *)key), str_value_len = 4096;
    u_char str_value_buf[4096];
    int n;
    double num_value = 0;
    char *err = NULL;

    err = NULL;
    ngx_memset(str_value_buf, '\xa7', str_value_len);
    n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TSTRING, str_value_buf,
                         str_value_len, num_value, &err);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, n);
    TEST_ASSERT_EQUAL_STRING("no memory", err);

    mps_shdict_close(dict);
}

void test_list_push_no_memory_for_list_entry(void)
{
    mps_shdict_t *dict = open_shdict();

    const size_t key_buf_size = 16, value_buf_size = 32;
    u_char key_buf[key_buf_size], value_buf[value_buf_size];
    int i, user_flags = 0xcafe, forcible = 0;
    double num_value = 0;
    long exptime = 0;
    char *err = NULL;

    for (i = 0; i < 63; i++) {
        mps_log_debug("mps_shdict_test", "i=%d ----------------\n", i);
        ngx_memset(value_buf, '\xa8', value_buf_size);
        size_t key_len = snprintf((char *)key_buf, key_buf_size, "key%d", i);
        int rc = mps_shdict_safe_set(dict, key_buf, key_len, MPS_SHDICT_TSTRING,
                                     value_buf, value_buf_size, num_value,
                                     exptime, user_flags, &err, &forcible);
        TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    }

    mps_log_debug("mps_shdict_test", "put key a----------------\n");
    const u_char *key = (const u_char *)"a";
    size_t key_len = strlen((const char *)key);
    int n = mps_shdict_lpush(dict, key, key_len, MPS_SHDICT_TNUMBER, NULL, 0,
                             num_value, &err);
    TEST_ASSERT_EQUAL_INT(NGX_ERROR, n);
    TEST_ASSERT_EQUAL_STRING("no memory", err);

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
        is_stale = 0, forcible = 0, n;
    double num_value = 0;
    char *err = NULL;
    long exptime = 0;

    /* set a string value */
    int rc = mps_shdict_set(dict, key, key_len, value_type, str_value_ptr,
                            str_value_len, num_value, exptime, user_flags, &err,
                            &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* set another string value */
    const u_char *key2 = (const u_char *)"a";
    const u_char *str_value2_ptr = (const u_char *)"some value";
    size_t key2_len = strlen((const char *)key2),
           str_value2_len = strlen((const char *)str_value2_ptr);
    rc = mps_shdict_set(dict, key2, key2_len, value_type, str_value2_ptr,
                        str_value2_len, num_value, exptime, user_flags, &err,
                        &forcible);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* set a list value */
    const u_char *key3 = (const u_char *)"q";
    size_t key3_len = strlen((const char *)key3);
    num_value = 1.5;
    n = mps_shdict_lpush(dict, key3, key3_len, MPS_SHDICT_TNUMBER, NULL, 0,
                         num_value, &err);
    TEST_ASSERT_EQUAL_INT(1, n);

    n = mps_shdict_lpush(dict, key3, key3_len, MPS_SHDICT_TSTRING,
                         (const u_char *)"value1", strlen("value1"), 0, &err);
    TEST_ASSERT_EQUAL_INT(2, n);

    /* Delete all keys */
    rc = mps_shdict_flush_all(dict);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);

    /* verify key and key2 are deleted */
    value_type = -1;
    u_char *got_str_value = NULL;
    size_t got_str_value_len = 0;
    rc = mps_shdict_get(dict, key, key_len, &value_type, &got_str_value,
                        &got_str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    value_type = -1;
    got_str_value = NULL;
    got_str_value_len = 0;
    rc = mps_shdict_get(dict, key2, key2_len, &value_type, &got_str_value,
                        &got_str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    value_type = -1;
    got_str_value = NULL;
    got_str_value_len = 0;
    rc = mps_shdict_get(dict, key3, key3_len, &value_type, &got_str_value,
                        &got_str_value_len, &num_value, &user_flags, get_stale,
                        &is_stale, &err);
    TEST_ASSERT_EQUAL_INT(NGX_OK, rc);
    TEST_ASSERT_EQUAL_INT(MPS_SHDICT_TNIL, value_type);

    mps_shdict_close(dict);
}

void test_open_multi(void)
{
    mps_shdict_t *dict1 = mps_shdict_open_or_create(
        DICT_NAME, DICT_SIZE, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR);
    mps_log_debug("mps_shdict_test", "dict1=%p\n", dict1);
    mps_shdict_t *dict2 = mps_shdict_open_or_create(
        "test_dict2", DICT_SIZE, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR);
    mps_log_debug("mps_shdict_test", "dict2=%p\n", dict2);
    mps_shdict_close(dict2);
    mps_shdict_close(dict1);

    // It is OK to close twice.
    mps_shdict_close(dict1);

    if (unlink("/dev/shm/test_dict2") == -1) {
        mps_log_debug("mps_shdict_test", "unlink shm file: %s\n",
                      strerror(errno));
    }
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

#define murmur_hash2(s) ngx_murmur_hash2((const u_char *)(s), strlen(s))

void test_murmur2_hash_collision(void)
{
    uint32_t h1 = murmur_hash2("xVS");
    uint32_t h2 = murmur_hash2("01Ji");
    mps_log_debug("mps_shdict_test", "h1=%x, h2=%x, h1%sh2\n", h1, h2,
                  h1 == h2 ? "==" : "!=");

    h1 = murmur_hash2("o3j");
    h2 = murmur_hash2("04B1");
    mps_log_debug("mps_shdict_test", "h1=%x, h2=%x, h1%sh2\n", h1, h2,
                  h1 == h2 ? "==" : "!=");

    h1 = murmur_hash2("WWn");
    h2 = murmur_hash2("07eh");
    mps_log_debug("mps_shdict_test", "h1=%x, h2=%x, h1%sh2\n", h1, h2,
                  h1 == h2 ? "==" : "!=");
}

// slab ---------------------------------
static mps_err_t slab_on_init(mps_slab_pool_t *pool)
{
    pool->log_nomem = 0;
    return 0;
}

void test_slab_alloc_one_byte_min_shift_one(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        "/test_shm1", 4096 * 3, 1, S_IRUSR | S_IWUSR, slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    void *p = mps_slab_calloc(pool, 1);
    TEST_ASSERT_NOT_NULL(p);

    int alloc_size = 2;
    void *p2 = NULL;
    for (int i = 1; i <= 4096 / alloc_size; i++) {
        p2 = mps_slab_calloc(pool, 1);
        TEST_ASSERT_NOT_NULL(p2);
    }

    mps_slab_free(pool, p);

    p = mps_slab_calloc(pool, 1);
    TEST_ASSERT_NOT_NULL(p);

    for (int i = 0; i <= 4096 / alloc_size; i++) {
        mps_slab_free(pool, (u_char *)p + i * alloc_size);
    }

    mps_slab_close(pool, 4096 * 3);
    delete_shm_file("/dev/shm"
                    "/test_shm1");
}

void test_slab_calloc_one_byte(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        "/test_shm1", 4096 * 3, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    void *p = mps_slab_calloc(pool, 1);
    TEST_ASSERT_NOT_NULL(p);

    mps_slab_free(pool, p);

    mps_slab_close(pool, 4096 * 3);
    delete_shm_file("/dev/shm"
                    "/test_shm1");
}

void test_slab_alloc_32_bytes(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        "/test_shm1", 4096 * 3, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    void *p1 = mps_slab_alloc(pool, 32);
    TEST_ASSERT_NOT_NULL(p1);

    void *p2 = mps_slab_alloc(pool, 32);
    TEST_ASSERT_NOT_NULL(p2);

    mps_slab_free(pool, p1);

    mps_slab_close(pool, 4096 * 3);
    delete_shm_file("/dev/shm"
                    "/test_shm1");
}

void test_slab_alloc_exact(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        "/test_shm1", 4096 * 3, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    int alloc_size = 64;
    void *p = mps_slab_calloc(pool, alloc_size);
    TEST_ASSERT_NOT_NULL(p);

    void *p2 = NULL;
    for (int i = 1; i <= 4096 / alloc_size; i++) {
        p2 = mps_slab_calloc(pool, alloc_size);
        TEST_ASSERT_NOT_NULL(p2);
    }

    mps_slab_free(pool, p);

    p = mps_slab_calloc(pool, alloc_size);
    TEST_ASSERT_NOT_NULL(p);

    for (int i = 0; i <= 4096 / alloc_size; i++) {
        mps_slab_free(pool, (u_char *)p + i * alloc_size);
    }

    mps_slab_close(pool, 4096 * 3);
    delete_shm_file("/dev/shm"
                    "/test_shm1");
}

void test_slab_alloc_big(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        "/test_shm1", 4096 * 3, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    int alloc_size = 128;
    void *p = mps_slab_calloc(pool, alloc_size);
    TEST_ASSERT_NOT_NULL(p);

    void *p2 = NULL;
    for (int i = 1; i <= 4096 / alloc_size; i++) {
        p2 = mps_slab_calloc(pool, alloc_size);
        TEST_ASSERT_NOT_NULL(p2);
    }

    mps_slab_free(pool, p);

    p = mps_slab_calloc(pool, alloc_size);
    TEST_ASSERT_NOT_NULL(p);

    for (int i = 0; i <= 4096 / alloc_size; i++) {
        mps_slab_free(pool, (u_char *)p + i * alloc_size);
    }

    mps_slab_close(pool, 4096 * 3);
    delete_shm_file("/dev/shm"
                    "/test_shm1");
}

void test_slab_alloc_two_pages(void)
{
    mps_slab_pool_t *pool = mps_slab_open_or_create(
        "/test_shm1", 4096 * 20, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        slab_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    int alloc_size = 4096 * 2;

    void *p = mps_slab_alloc(pool, alloc_size);
    TEST_ASSERT_NOT_NULL(p);
    for (int i = 1; i < 4; i++) {
        void *p2 = mps_slab_alloc(pool, alloc_size);
        TEST_ASSERT_NOT_NULL(p2);
    }

    for (int i = 0; i < 4; i++) {
        mps_slab_free(pool, (u_char *)p + i * alloc_size);
    }

    mps_slab_close(pool, 4096 * 3);
    delete_shm_file("/dev/shm"
                    "/test_shm1");
}

void test_slab_open_existing(void)
{
    mps_slab_pool_t *pool1 = mps_slab_open_or_create(
        "/test_shm1", 4096 * 3, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        slab_on_init);
    TEST_ASSERT_NOT_NULL(pool1);

    mps_slab_pool_t *pool2 = mps_slab_open_or_create(
        "/test_shm1", 4096 * 3, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        slab_on_init);
    TEST_ASSERT_NOT_NULL(pool2);

    mps_slab_close(pool2, 4096 * 3);
    mps_slab_close(pool1, 4096 * 3);

    delete_shm_file("/dev/shm"
                    "/test_shm1");
}

typedef struct thread_info {
    pthread_t thread;
    int thread_num;
} thread_info;

static void *thread_start(void *arg)
{
    thread_info *tinfo = (thread_info *)arg;

    mps_log_debug("mps_slab_test", "thread_start, thread_num=%d\n",
                  tinfo->thread_num);

    mps_slab_pool_t *pool1 = mps_slab_open_or_create(
        "/test_shm1", 4096 * 3, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        slab_on_init);
    mps_log_debug("mps_slab_test", "thread_num=%d, pool1=%p\n",
                  tinfo->thread_num, pool1);
    TEST_ASSERT_NOT_NULL(pool1);

    mps_slab_close(pool1, 4096 * 3);

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

    delete_shm_file("/dev/shm"
                    "/test_shm1");
}

// rbtree ---------------------------------------------

typedef struct {
    mps_rbtree_t rbtree;
    mps_rbtree_node_t sentinel;
} test_rbtree_t;

static mps_err_t rbtree_standard_on_init(mps_slab_pool_t *pool)
{
    test_rbtree_t *tree;
    mps_err_t err;

    mps_log_status("rbtree_standard_on_init start");
    tree = mps_slab_alloc(pool, sizeof(test_rbtree_t));
    if (!tree) {
        mps_log_error("rbtree_standard_on_init: mps_slab_alloc failed");
        return ENOMEM;
    }

    pool->data = mps_offset(pool, tree);
    err = mps_rbtree_init(pool, &tree->rbtree, &tree->sentinel,
                          MPS_RBTREE_INSERT_TYPE_ID_STANDARD);
    if (err != 0) {
        return err;
    }

    pool->log_nomem = 0;

    mps_log_status("rbtree_standard_on_init exit");
    return 0;
}

static mps_err_t rbtree_timer_on_init(mps_slab_pool_t *pool)
{
    test_rbtree_t *tree;
    mps_err_t err;

    mps_log_status("rbtree_timer_on_init start");
    tree = mps_slab_alloc(pool, sizeof(test_rbtree_t));
    if (!tree) {
        mps_log_error("rbtree_timer_on_init: mps_slab_alloc failed");
        return ENOMEM;
    }

    pool->data = mps_offset(pool, tree);
    err = mps_rbtree_init(pool, &tree->rbtree, &tree->sentinel,
                          MPS_RBTREE_INSERT_TYPE_ID_TIMER);
    if (err != 0) {
        return err;
    }

    pool->log_nomem = 0;

    mps_log_status("rbtree_timer_on_init exit");
    return 0;
}

static mps_err_t rbtree_bad_insert_type_on_init(mps_slab_pool_t *pool)
{
    test_rbtree_t *tree;
    mps_err_t err;

    mps_log_status("rbtree_bad_insert_type_on_init start");
    tree = mps_slab_alloc(pool, sizeof(test_rbtree_t));
    if (!tree) {
        mps_log_error("rbtree_bad_insert_type_on_init: mps_slab_alloc failed");
        return ENOMEM;
    }

    pool->data = mps_offset(pool, tree);
    mps_log_debug(
        "rbtree_test",
        "calling mps_rbtree_init with MPS_RBTREE_INSERT_TYPE_ID_COUNT");
    err = mps_rbtree_init(pool, &tree->rbtree, &tree->sentinel,
                          MPS_RBTREE_INSERT_TYPE_ID_COUNT);
    if (err != 0) {
        return err;
    }

    pool->log_nomem = 0;

    mps_log_status("rbtree_bad_insert_type_on_init exit");
    return 0;
}

static mps_rbtree_node_t *rbtree_insert(mps_slab_pool_t *pool,
                                        mps_rbtree_t *tree,
                                        mps_rbtree_key_t key, u_char data)
{
    mps_rbtree_node_t *node = mps_slab_calloc(pool, sizeof(mps_rbtree_node_t));
    mps_log_debug("rbtree_test", "node=%p (0x%lx)", node,
                  mps_offset(pool, node));
    if (node == NULL) {
        return NULL;
    }

    node->key = key;
    node->data = data;
    mps_rbtree_insert(pool, tree, node);
    return node;
}

static mps_rbtree_node_t *
rbtree_lookup(mps_slab_pool_t *pool, mps_rbtree_t *tree, mps_rbtree_key_t key)
{
    mps_rbtree_node_t *node, *sentinel;

    node = mps_rbtree_node(pool, tree->root);
    sentinel = mps_rbtree_node(pool, tree->sentinel);

    while (node != sentinel) {
        if (key < node->key) {
            node = mps_rbtree_node(pool, node->left);
            continue;
        }

        if (key > node->key) {
            node = mps_rbtree_node(pool, node->right);
            continue;
        }

        return node;
    }

    return NULL;
}

static void show_tree_node(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                           mps_rbtree_node_t *node);

void show_tree(mps_slab_pool_t *pool, mps_rbtree_t *tree)
{
    mps_rbtree_node_t *root;

    root = mps_rbtree_node(pool, tree->root);
    mps_log_status("show_tree start, root=%lx, sentinel=%lx", tree->root,
                   tree->sentinel);
    show_tree_node(pool, tree, root);
    mps_log_status("show_tree exit, root=%lx", tree->root);
}

static void show_tree_node(mps_slab_pool_t *pool, mps_rbtree_t *tree,
                           mps_rbtree_node_t *node)
{
    if (node->left == mps_nulloff) {
        mps_log_error("show_tree_node, node->left is null, node=%x",
                      mps_offset(pool, node));
    }
    if (node->right == mps_nulloff) {
        mps_log_error("show_tree_node, node->right is null, node=%x",
                      mps_offset(pool, node));
    }
    if (node->left == mps_nulloff || node->right == mps_nulloff) {
        return;
    }

    mps_log_status("show_tree_node, node=%lx, key=%ld, left=%lx, right=%lx",
                   mps_offset(pool, node), node->key, node->left, node->right);
    if (node->left != tree->sentinel) {
        show_tree_node(pool, tree, mps_rbtree_node(pool, node->left));
    }
    if (node->right != tree->sentinel) {
        show_tree_node(pool, tree, mps_rbtree_node(pool, node->right));
    }
}

static int index_of_nodes_by_key(mps_rbtree_node_t **nodes, int count,
                                 mps_rbtree_key_t key)
{
    for (int i = 0; i < count; i++) {
        if (nodes[i]->key == key) {
            return i;
        }
    }
    return -1;
}

void test_rbtree_standard(void)
{
    mps_slab_pool_t *pool;

    pool = mps_slab_open_or_create("/test_tree1", 4096 * 5,
                                   MPS_SLAB_DEFAULT_MIN_SHIFT,
                                   S_IRUSR | S_IWUSR, rbtree_standard_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    mps_rbtree_t *tree = mps_rbtree(pool, pool->data);

    const int node_count = 9;
    mps_rbtree_key_t keys[9] = {1, 3, 9, 7, 8, 4, 5, 2, 6};
    mps_rbtree_node_t *nodes[node_count];
    for (int i = 0; i < node_count; i++) {
        nodes[i] = rbtree_insert(pool, tree, keys[i], 'd');
        TEST_ASSERT_NOT_NULL(nodes[i]);
    }

    for (int i = 0; i < node_count; i++) {
        mps_rbtree_next(pool, tree, nodes[i]);
    }

    show_tree(pool, tree);
    //         5
    //        / \
    //       /   \
    //      /     \
    //     /       \
    //    3         8
    //   / \       / \
    //  /   \     /   \
    // 1     4   7     9
    //  \       /
    //   2     6

    mps_rbtree_key_t del_keys[9] = {5, 3, 4, 8, 6, 2, 1, 7, 9};
    for (int i = 0; i < node_count; i++) {
        int j = index_of_nodes_by_key(nodes, node_count, del_keys[i]);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, j);
        TEST_ASSERT_LESS_THAN_INT(node_count, j);
        mps_log_status("deleting key=%ld, j=%d -----------------\n",
                       del_keys[i], j);
        mps_rbtree_delete(pool, tree, nodes[j]);
    }

    mps_slab_close(pool, 4096 * 5);
    delete_shm_file("/dev/shm"
                    "/test_tree1");
}

void test_rbtree_standard_random(void)
{
    mps_slab_pool_t *pool;

    pool = mps_slab_open_or_create("/test_tree1", 4096 * 5,
                                   MPS_SLAB_DEFAULT_MIN_SHIFT,
                                   S_IRUSR | S_IWUSR, rbtree_standard_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    mps_rbtree_t *tree = mps_rbtree(pool, pool->data);

    uint32_t seed = 1234, h = seed, rnd, threshold = (1 << 31) + (1 << 18), key;
    const int iterations = 200;
    const int node_count_max = 100;
    int node_count = 0, j;
    mps_rbtree_node_t *nodes[node_count_max];
    ngx_memset(nodes, 0, sizeof(mps_rbtree_node_t *) * node_count_max);
    for (int i = 0; i < iterations;) {
        mps_log_debug("rbtree_test", "i=%d --------------------\n", i);
        if (node_count < node_count_max) {
            if (node_count > 0) {
                rnd = ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
                h = rnd;
            }
            if (node_count == 0 || rnd >= threshold) {
                do {
                    key =
                        ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
                    h = key;
                    mps_log_debug("rbtree_test", "key=%u", key);
                } while (rbtree_lookup(pool, tree, key) != NULL);
                mps_rbtree_node_t *node = rbtree_insert(pool, tree, key, 'd');
                if (node != NULL) {
                    nodes[node_count++] = node;
                    mps_log_debug("rbtree_test",
                                  "inserted node=%x, key=%x, node_count=%d",
                                  mps_offset(pool, node), key, node_count);
                    i++;
                    continue;
                }
            }
        }

        if (node_count == 0) {
            continue;
        }

        // This is not uniformly random, but it is OK for this test.
        rnd = ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
        h = rnd;
        j = rnd % node_count;

        mps_rbtree_delete(pool, tree, nodes[j]);
        mps_log_debug("rbtree_test", "deleted node=%x, key=%x, node_count=%d",
                      mps_offset(pool, nodes[j]), nodes[j]->key,
                      node_count - 1);
        nodes[j] = nodes[--node_count];
        i++;
    }

    mps_slab_close(pool, 4096 * 5);
    delete_shm_file("/dev/shm"
                    "/test_tree1");
}

void test_rbtree_timer_random(void)
{
    mps_slab_pool_t *pool;

    pool = mps_slab_open_or_create("/test_tree1", 4096 * 5,
                                   MPS_SLAB_DEFAULT_MIN_SHIFT,
                                   S_IRUSR | S_IWUSR, rbtree_timer_on_init);
    TEST_ASSERT_NOT_NULL(pool);

    mps_rbtree_t *tree = mps_rbtree(pool, pool->data);

    uint32_t seed = 1234, h = seed, rnd, threshold = (1 << 31) + (1 << 18), key;
    const int iterations = 200;
    const int node_count_max = 100;
    int node_count = 0, j;
    mps_rbtree_node_t *nodes[node_count_max];
    ngx_memset(nodes, 0, sizeof(mps_rbtree_node_t *) * node_count_max);
    for (int i = 0; i < iterations;) {
        mps_log_debug("rbtree_test", "i=%d --------------------\n", i);
        if (node_count < node_count_max) {
            if (node_count > 0) {
                rnd = ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
                h = rnd;
            }
            if (node_count == 0 || rnd >= threshold) {
                do {
                    key =
                        ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
                    h = key;
                    mps_log_debug("rbtree_test", "key=%u", key);
                } while (rbtree_lookup(pool, tree, key) != NULL);
                mps_rbtree_node_t *node = rbtree_insert(pool, tree, key, 'd');
                if (node != NULL) {
                    nodes[node_count++] = node;
                    mps_log_debug("rbtree_test",
                                  "inserted node=%x, key=%x, node_count=%d",
                                  mps_offset(pool, node), key, node_count);
                    i++;
                    continue;
                }
            }
        }

        if (node_count == 0) {
            continue;
        }

        // This is not uniformly random, but it is OK for this test.
        rnd = ngx_murmur_hash2((const u_char *)&h, sizeof(uint32_t));
        h = rnd;
        j = rnd % node_count;

        mps_rbtree_delete(pool, tree, nodes[j]);
        mps_log_debug("rbtree_test", "deleted node=%x, key=%x, node_count=%d",
                      mps_offset(pool, nodes[j]), nodes[j]->key,
                      node_count - 1);
        nodes[j] = nodes[--node_count];
        i++;
    }

    mps_slab_close(pool, 4096 * 5);
    delete_shm_file("/dev/shm"
                    "/test_tree1");
}

void test_rbtree_bad_insert_type(void)
{
    mps_slab_pool_t *pool;

    pool = mps_slab_open_or_create(
        "/test_tree1", 4096 * 5, MPS_SLAB_DEFAULT_MIN_SHIFT, S_IRUSR | S_IWUSR,
        rbtree_bad_insert_type_on_init);
    TEST_ASSERT_NULL(pool);

    delete_shm_file("/dev/shm"
                    "/test_tree1");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_murmur2_hash_collision);
    RUN_TEST(test_open_multi);
    RUN_TEST(test_add_exists);
    RUN_TEST(test_add_expired);
    RUN_TEST(test_nil_add);
    RUN_TEST(test_nil_set);
    RUN_TEST(test_incr_forcible);
    RUN_TEST(test_incr_no_memory);
    RUN_TEST(test_incr_happy);
    RUN_TEST(test_incr_not_found);
    RUN_TEST(test_incr_not_a_number);
    RUN_TEST(test_incr_remove_expired_list);
    RUN_TEST(test_incr_reuse_expired_number);
    RUN_TEST(test_incr_reuse_expired_boolean);
    RUN_TEST(test_boolean_happy);
    RUN_TEST(test_boolean_get_buf_short);
    RUN_TEST(test_number_happy);
    RUN_TEST(test_string_happy);
    RUN_TEST(test_safe_set);
    RUN_TEST(test_safe_add);
    RUN_TEST(test_get_ttl_set_expire);
    RUN_TEST(test_flush_all);
    RUN_TEST(test_capacity);
    RUN_TEST(test_free_space);
    RUN_TEST(test_set_invalid_value_type);
    RUN_TEST(test_replace_expired);
    RUN_TEST(test_replace_not_found);
    RUN_TEST(test_replace_with_ttl);
    RUN_TEST(test_set_forcible);
    RUN_TEST(test_set_expire_no_mem);
    RUN_TEST(test_memn2cmp);
    RUN_TEST(test_safe_set_no_key_no_mem);

    RUN_TEST(test_list_basics);
    RUN_TEST(test_list_delete);
    RUN_TEST(test_list_removed_expired_not_list_value);
    RUN_TEST(test_list_removed_expired_list_value);
    RUN_TEST(test_list_get_err);
    RUN_TEST(test_list_push_bad_type_err);
    RUN_TEST(test_list_pop_not_list_err);
    RUN_TEST(test_list_push_not_list_err);
    RUN_TEST(test_list_llen_not_list_err);
    RUN_TEST(test_list_push_no_memory_for_new_list);
    RUN_TEST(test_list_push_no_memory_for_list_entry);

    RUN_TEST(test_slab_alloc_one_byte_min_shift_one);
    RUN_TEST(test_slab_calloc_one_byte);
    RUN_TEST(test_slab_alloc_32_bytes);
    RUN_TEST(test_slab_alloc_exact);
    RUN_TEST(test_slab_alloc_big);
    RUN_TEST(test_slab_alloc_two_pages);
    RUN_TEST(test_slab_open_existing);
    RUN_TEST(test_slab_open_or_create_multithread);

    RUN_TEST(test_rbtree_standard);
    RUN_TEST(test_rbtree_standard_random);
    RUN_TEST(test_rbtree_timer_random);
    RUN_TEST(test_rbtree_bad_insert_type);

    return UNITY_END();
}
