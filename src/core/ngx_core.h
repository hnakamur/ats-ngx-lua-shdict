
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CORE_H_INCLUDED_
#define _NGX_CORE_H_INCLUDED_


#include <ngx_config.h>


typedef struct ngx_module_s          ngx_module_t;
typedef struct ngx_conf_s            ngx_conf_t;
typedef struct ngx_cycle_s           ngx_cycle_t;
typedef struct ngx_pool_s            ngx_pool_t;
typedef struct ngx_chain_s           ngx_chain_t;
typedef struct ngx_log_s             ngx_log_t;
typedef struct ngx_open_file_s       ngx_open_file_t;
typedef struct ngx_command_s         ngx_command_t;
typedef struct ngx_file_s            ngx_file_t;
typedef struct ngx_event_s           ngx_event_t;
typedef struct ngx_event_aio_s       ngx_event_aio_t;
typedef struct ngx_connection_s      ngx_connection_t;
typedef struct ngx_thread_task_s     ngx_thread_task_t;
typedef struct ngx_ssl_s             ngx_ssl_t;
typedef struct ngx_proxy_protocol_s  ngx_proxy_protocol_t;
typedef struct ngx_ssl_connection_s  ngx_ssl_connection_t;
typedef struct ngx_udp_connection_s  ngx_udp_connection_t;

typedef void (*ngx_event_handler_pt)(ngx_event_t *ev);
typedef void (*ngx_connection_handler_pt)(ngx_connection_t *c);


#define  NGX_OK          0
#define  NGX_ERROR      -1
#define  NGX_AGAIN      -2
#define  NGX_BUSY       -3
#define  NGX_DONE       -4
#define  NGX_DECLINED   -5
#define  NGX_ABORT      -6

#include <pthread.h>

// #include <ngx_thread.h>
// #include <ngx_time.h>
// #include <ngx_socket.h>
#include <ngx_string.h>
// #include <ngx_shmem.h>


// #include <ngx_process.h>
// #include <ngx_user.h>
#include <ngx_log.h>
#include <ngx_crc32.h>
#include <ngx_times.h>
// #include <ngx_cycle.h>
// #include <ngx_os.h>

#define ngx_debug_point()
extern ngx_uint_t  ngx_cacheline_size;

#define LF     (u_char) '\n'
#define CR     (u_char) '\r'
#define CRLF   "\r\n"


#define ngx_abs(value)       (((value) >= 0) ? (value) : - (value))
#define ngx_max(val1, val2)  ((val1 < val2) ? (val2) : (val1))
#define ngx_min(val1, val2)  ((val1 > val2) ? (val2) : (val1))

// void ngx_cpuinfo(void);

#if (NGX_HAVE_OPENAT)
#define NGX_DISABLE_SYMLINKS_OFF        0
#define NGX_DISABLE_SYMLINKS_ON         1
#define NGX_DISABLE_SYMLINKS_NOTOWNER   2
#endif

#endif /* _NGX_CORE_H_INCLUDED_ */
