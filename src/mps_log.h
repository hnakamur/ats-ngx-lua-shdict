#ifndef _MPS_LOG_H_INCLUDED_
#define _MPS_LOG_H_INCLUDED_

#include <inttypes.h>

#define MPS_LOG_TAG "mps_shdict"

#if !defined(mps_printflike)
#if defined(__GNUC__) || defined(__clang__)
#define mps_printflike(fmt_index, arg_index)                                   \
    __attribute__((format(printf, fmt_index, arg_index)))
#else
#define mps_printflike(fmt_index, arg_index)
#endif
#endif

#if MPS_LOG_ATS

#include "tslog.h"
#define mps_log_debug(tag, ...) TSDebug((tag), __VA_ARGS__)
#define mps_log_status(...) TSStatus(__VA_ARGS__)
#define mps_log_note(...) TSNote(__VA_ARGS__)
#define mps_log_warning(...) TSWarning(__VA_ARGS__)
#define mps_log_error(...) TSError(__VA_ARGS__)

#elif MPS_LOG_NGX

#include <ngx_config.h>
#include <ngx_core.h>
void mps_log_ngx_core(ngx_uint_t level, ngx_log_t *log, const char *fmt, ...)
    mps_printflike(3, 4);
void mps_log_ngx_debug(ngx_log_t *log, const char *func, const char *file,
                       int line, const char *tag, const char *fmt, ...)
    mps_printflike(6, 7);
extern volatile ngx_cycle_t *ngx_cycle;
#define mps_log_debug(tag, ...)                                                \
    if (ngx_cycle->log->log_level & NGX_LOG_DEBUG)                             \
    mps_log_ngx_debug(ngx_cycle->log, __func__, __FILE__, __LINE__, tag,       \
                      __VA_ARGS__)
#define mps_log_status(...)                                                    \
    if (ngx_cycle->log->log_level & NGX_LOG_INFO)                              \
    mps_log_ngx_core(NGX_LOG_INFO, ngx_cycle->log, __VA_ARGS__)
#define mps_log_note(...)                                                      \
    if (ngx_cycle->log->log_level & NGX_LOG_NOTICE)                            \
    mps_log_ngx_core(NGX_LOG_NOTICE, ngx_cycle->log, __VA_ARGS__)
#define mps_log_warning(...)                                                   \
    if (ngx_cycle->log->log_level & NGX_LOG_WARN)                              \
    mps_log_ngx_core(NGX_LOG_WARN, ngx_cycle->log, __VA_ARGS__)
#define mps_log_error(...)                                                     \
    if (ngx_cycle->log->log_level & NGX_LOG_ERR)                               \
    mps_log_ngx_core(NGX_LOG_ERR, ngx_cycle->log, __VA_ARGS__)

#elif MPS_LOG_NOP

#define mps_log_debug(tag, ...)
#define mps_log_status(...)
#define mps_log_note(...)
#define mps_log_warning(...)
#define mps_log_error(...)

#else

void mps_log_stderr(const char *level, const char *fmt, ...)
    mps_printflike(2, 3);
void mps_log_stderr_debug(const char *func, const char *file, int line,
                          const char *tag, const char *fmt, ...)
    mps_printflike(5, 6);
#define mps_log_debug(tag, ...)                                                \
    mps_log_stderr_debug(__func__, __FILE__, __LINE__, tag, __VA_ARGS__)
#define mps_log_status(...) mps_log_stderr("STATUS", __VA_ARGS__)
#define mps_log_note(...) mps_log_stderr("NOTE", __VA_ARGS__)
#define mps_log_warning(...) mps_log_stderr("WARNING", __VA_ARGS__)
#define mps_log_error(...) mps_log_stderr("ERROR", __VA_ARGS__)

#endif

#endif /* _MPS_LOG_H_INCLUDED_ */
