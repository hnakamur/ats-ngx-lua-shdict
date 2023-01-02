#include <ngx_config.h>
#include <ngx_core.h>

#include "mps_log.h"

void mps_log_ngx_core(ngx_uint_t level, ngx_log_t *log, const char *fmt, ...)
{
    char buf[NGX_MAX_ERROR_STR];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(buf, NGX_MAX_ERROR_STR, fmt, args);
    va_end(args);

    ngx_log_error_core(level, log, 0, "%*s", n, buf);
}

void mps_log_ngx_debug(ngx_log_t *log, const char *func, const char *file,
                       int line, const char *tag, const char *fmt, ...)
{
    char buf[NGX_MAX_ERROR_STR];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(buf, NGX_MAX_ERROR_STR, fmt, args);
    va_end(args);

    ngx_log_error_core(NGX_LOG_DEBUG, log, 0, "<%s:%d (%s)> (%s) %*s\n",
                       basename(file), line, func, tag, n, buf);
}
