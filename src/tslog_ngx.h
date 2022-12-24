#ifndef _TSLOG_NGX_H_INCLUDED_
#define _TSLOG_NGX_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>

extern volatile ngx_cycle_t  *ngx_cycle;

#if (NGX_HAVE_C99_VARIADIC_MACROS)

#if 1

#define TSEmergency(...) fprintf(stderr, __VA_ARGS__)
#define TSAlert(...) fprintf(stderr, __VA_ARGS__)
#define TSFatal(...) fprintf(stderr, __VA_ARGS__)
#define TSError(...) fprintf(stderr, __VA_ARGS__)
#define TSWarning(...) fprintf(stderr, __VA_ARGS__)
#define TSNote(...) fprintf(stderr, __VA_ARGS__)
#define TSStatus(...) fprintf(stderr, __VA_ARGS__)
#define TSDebug(tag, ...) fprintf(stderr, __VA_ARGS__)

#else

#define TSEmergency(...)                                                      \
    ngx_log_error(NGX_LOG_EMERG, ngx_cycle->log, 0, __VA_ARGS__)

#define TSAlert(...)                                                          \
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, __VA_ARGS__)

#define TSFatal(...)                                                          \
    ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, 0, __VA_ARGS__)

#define TSError(...)                                                          \
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, __VA_ARGS__)

#define TSWarning(...)                                                        \
    ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0, __VA_ARGS__)

#define TSNote(...)                                                           \
    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0, __VA_ARGS__)

#define TSStatus(...)                                                         \
    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, __VA_ARGS__)

#define TSDebug(tag, ...)                                                     \
    ngx_log_debug(NGX_LOG_DEBUG, ngx_cycle->log, 0, __VA_ARGS__)

#endif

#elif (NGX_HAVE_GCC_VARIADIC_MACROS)

#define TSEmergency(args...)                                                  \
    ngx_log_error(NGX_LOG_EMERG, ngx_cycle->log, 0, args)

#define TSAlert(args...)                                                      \
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, args)

#define TSFatal(args...)                                                      \
    ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, 0, args)

#define TSError(args...)                                                      \
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, args)

#define TSWarning(args...)                                                    \
    ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0, args)

#define TSNote(args...)                                                       \
    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0, args)

#define TSStatus(args...)                                                     \
    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, args)

#define TSDebug(tag, args...)                                                 \
    ngx_log_debug(NGX_LOG_DEBUG, ngx_cycle->log, 0, args)

#else /* no variadic macros */

#error not supported for no variadic macros environment

#endif

#endif /* _TSLOG_NGX_H_INCLUDED_ */
