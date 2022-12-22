#include <ngx_config.h>
#include <ngx_core.h>

ngx_int_t   ngx_ncpu;

ngx_uint_t  ngx_cacheline_size = NGX_CPU_CACHE_LINE;

#define NGX_TIME_SLOTS   64

static ngx_time_t        cached_time[NGX_TIME_SLOTS];

volatile ngx_time_t     *ngx_cached_time = &cached_time[0];
