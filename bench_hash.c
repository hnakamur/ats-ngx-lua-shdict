#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ngx_crc32.h"
#include "ngx_murmurhash.h"

double diff_timespec(const struct timespec *time1, const struct timespec *time0) {
  return (time1->tv_sec - time0->tv_sec)
          + (time1->tv_nsec - time0->tv_nsec) / 1000000000.0;
}

int main(int argc, char **argv) {
  int                   i, n;
  struct timespec       t1, t2, rt;
  uint32_t              hash_sum;
  const unsigned char  *test_key = "hello_this_is_test_key";
  size_t                test_key_len = strlen(test_key);

  if (argc < 2) {
    fprintf(stderr, "Usage: %s count\n", argv[0]);
    return 2;
  }

  n = atoi(argv[1]);
  if (n == 0) {
    fprintf(stderr, "count must be positive integer\n");
    return 2;
  }

  if (ngx_crc32_table_init() != NGX_OK) {
    fprintf(stderr, "ngx_crc32_table_init failed.\n");
    return 2;
  }

  if (clock_gettime(CLOCK_MONOTONIC, &t1)) {
    fprintf(stderr, "get time t1: %s\n", strerror(errno));
    return 1;
  }

  hash_sum = 0;
  for (i = 0; i < n; i++) {
    hash_sum += ngx_crc32_short(test_key, test_key_len);
  }

  if (clock_gettime(CLOCK_MONOTONIC, &t2)) {
    fprintf(stderr, "get time t2: %s\n", strerror(errno));
    return 1;
  }

  printf("crc32   elapsed=%g(s), hash_sum=%d\n", diff_timespec(&t2, &t1), hash_sum);

  if (clock_gettime(CLOCK_MONOTONIC, &t1)) {
    fprintf(stderr, "get time t1: %s\n", strerror(errno));
    return 1;
  }

  hash_sum = 0;
  for (i = 0; i < n; i++) {
    hash_sum += ngx_murmur_hash2(test_key, test_key_len);
  }

  if (clock_gettime(CLOCK_MONOTONIC, &t2)) {
    fprintf(stderr, "get time t2: %s\n", strerror(errno));
    return 1;
  }

  printf("murmur2 elapsed=%g(s), hash_sum=%d\n", diff_timespec(&t2, &t1), hash_sum);

  return 0;
}
