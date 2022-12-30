#ifndef _MPS_LOG_STDERR_H_INCLUDED_
#define _MPS_LOG_STDERR_H_INCLUDED_

void mps_log_stderr(const char *level, const char *fmt, ...);
void mps_log_stderr_debug(const char *func, const char *file, int line,
                          const char *tag, const char *fmt, ...);

#endif /* _MPS_LOG_STDERR_H_INCLUDED_ */
