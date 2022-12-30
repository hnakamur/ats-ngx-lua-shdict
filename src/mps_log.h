#ifndef _MPS_LOG_H_INCLUDED_
#define _MPS_LOG_H_INCLUDED_

#include <inttypes.h>

#define MPS_LOG_TAG "mps_shdict"

#if MPS_LOG_ATS

#include "tslog.h"
#define LogLenStr "%.*s"
#define mps_log_debug(tag, ...) TSDebug((tag), __VA_ARGS__)
#define mps_log_status(...) TSStatus(__VA_ARGS__)
#define mps_log_note(...) TSNote(__VA_ARGS__)
#define mps_log_warning(...) TSWarning(__VA_ARGS__)
#define mps_log_error(...) TSError(__VA_ARGS__)

#elif MPS_LOG_NGX

#include "ngx_log.h"
#define LogLenStr "%*s"
extern volatile ngx_cycle_t *ngx_cycle;
#define mps_log_debug(tag, ...)                                                \
    ngx_log_debug(NGX_LOG_DEBUG, ngx_cycle->log, 0, __VA_ARGS__)
#define mps_log_status(...)                                                    \
    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, __VA_ARGS__)
#define mps_log_note(...)                                                      \
    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0, __VA_ARGS__)
#define mps_log_warning(...)                                                   \
    ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0, __VA_ARGS__)
#define mps_log_error(...)                                                     \
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, __VA_ARGS__)

#elif MPS_LOG_NOP

#define LogLenStr
#define mps_log_debug(tag, ...)
#define mps_log_status(...)
#define mps_log_note(...)
#define mps_log_warning(...)
#define mps_log_error(...)

#else

#include "mps_log_stderr.h"
#define LogLenStr "%.*s"
#define mps_log_debug(tag, ...)                                                \
    mps_log_stderr_debug(__func__, __FILE__, __LINE__, tag, __VA_ARGS__)
#define mps_log_status(...) mps_log_stderr("STATUS", __VA_ARGS__)
#define mps_log_note(...) mps_log_stderr("NOTE", __VA_ARGS__)
#define mps_log_warning(...) mps_log_stderr("WARNING", __VA_ARGS__)
#define mps_log_error(...) mps_log_stderr("ERROR", __VA_ARGS__)

#endif

#endif /* _MPS_LOG_H_INCLUDED_ */
