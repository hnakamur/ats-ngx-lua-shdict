#ifndef _MPS_LOG_NGX_H_INCLUDED_
#define _MPS_LOG_NGX_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>

void mps_log_ngx_core(ngx_uint_t level, ngx_log_t *log, const char *fmt, ...);
void mps_log_ngx_debug(ngx_log_t *log, const char *func, const char *file,
                       int line, const char *tag, const char *fmt, ...);

#endif /* _MPS_LOG_NGX_H_INCLUDED_ */
