#include <ngx_config.h>
#include <ngx_core.h>

volatile ngx_cycle_t  *ngx_cycle;

ngx_pid_t     ngx_pid;

ngx_int_t   ngx_ncpu;

ngx_uint_t  ngx_cacheline_size = NGX_CPU_CACHE_LINE;

volatile ngx_time_t     *ngx_cached_time;
