#ifndef _TSLOG_H_INCLUDED_
#define _TSLOG_H_INCLUDED_

#define tsapi

#if !defined(TS_PRINTFLIKE)
#if defined(__GNUC__) || defined(__clang__)
#define TS_PRINTFLIKE(fmt_index, arg_index)                                    \
    __attribute__((format(printf, fmt_index, arg_index)))
#else
#define TS_PRINTFLIKE(fmt_index, arg_index)
#endif
#endif

// Log information
tsapi void TSStatus(const char *fmt, ...) TS_PRINTFLIKE(1, 2);

// Log significant information
tsapi void TSNote(const char *fmt, ...) TS_PRINTFLIKE(1, 2);

// Log concerning information
tsapi void TSWarning(const char *fmt, ...) TS_PRINTFLIKE(1, 2);

// Log operational failure, fail CI
tsapi void TSError(const char *fmt, ...) TS_PRINTFLIKE(1, 2);

// Log recoverable crash, fail CI, exit & restart
tsapi void TSFatal(const char *fmt, ...) TS_PRINTFLIKE(1, 2);

// Log recoverable crash, fail CI, exit & restart, Ops attention
tsapi void TSAlert(const char *fmt, ...) TS_PRINTFLIKE(1, 2);

// Log unrecoverable crash, fail CI, exit, Ops attention
tsapi void TSEmergency(const char *fmt, ...) TS_PRINTFLIKE(1, 2);

tsapi int TSIsDebugTagSet(const char *t);
tsapi void TSDebug(const char *tag, const char *format_str, ...)
    TS_PRINTFLIKE(2, 3);

#endif /* _TSLOG_H_INCLUDED_ */
