#ifndef _TSLOG_STDERR_H_INCLUDED_
#define _TSLOG_STDERR_H_INCLUDED_

void mps_log_stderr(const char *level, int exits, const char *fmt, ...);
void mps_log_stderr_debug(const char *tag, const char *fmt, ...);

#define TSStatus(...)     mps_log_stderr("Status",    0, __VA_ARGS__)
#define TSNote(...)       mps_log_stderr("Note",      0, __VA_ARGS__)
#define TSWarning(...)    mps_log_stderr("Warning",   0, __VA_ARGS__)
#define TSError(...)      mps_log_stderr("Error",     0, __VA_ARGS__)
#define TSFatal(...)      mps_log_stderr("Fatal",     1, __VA_ARGS__)
#define TSAlert(...)      mps_log_stderr("Alert",     1, __VA_ARGS__)
#define TSEmergency(...)  mps_log_stderr("Emergency", 1, __VA_ARGS__)
#define TSDebug(...)      mps_log_stderr_debug(__VA_ARGS__)

#endif /* _TSLOG_STDERR_H_INCLUDED_ */
