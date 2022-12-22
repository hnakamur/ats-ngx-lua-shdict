
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_TIMES_H_INCLUDED_
#define _NGX_TIMES_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct {
    time_t      sec;
    ngx_uint_t  msec;
    ngx_int_t   gmtoff;
} ngx_time_t;

extern volatile ngx_time_t  *ngx_cached_time;

#define ngx_timeofday()      (ngx_time_t *) ngx_cached_time

#endif /* _NGX_TIMES_H_INCLUDED_ */
