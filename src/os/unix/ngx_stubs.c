#include <ngx_config.h>
#include <ngx_core.h>

void
ngx_debug_point(void)
{
}

#if (NGX_HAVE_VARIADIC_MACROS)

void
ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, ...)

#else

void
ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, va_list args)

#endif
{
}

void ngx_set_pagesize(ngx_uint_t pagesize) {
    ngx_pagesize = pagesize;
}
