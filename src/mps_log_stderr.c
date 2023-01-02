#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* basename() */
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mps_log.h"

#define TSLOG_STDERR_BUFSIZE 2048

void mps_log_stderr(const char *level, const char *fmt, ...)
{
    va_list ap;
    char buf[TSLOG_STDERR_BUFSIZE];
    int n;
    size_t size;

    /* Determine required size */
    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        fprintf(stderr,
                "mps_log_stderr[%s] cannot determine required size: %s\n",
                level, strerror(errno));
        exit(1);
    }
    size = (size_t)n + 1; /* +1 for '\0' */
    if (size > TSLOG_STDERR_BUFSIZE) {
        fprintf(stderr, "mps_log_stderr[%s] message is too long\n", level);
        exit(1);
    }

    /* Print the message to the buffer. */
    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (n < 0) {
        fprintf(stderr,
                "mps_log_stderr[%s] cannot print message to buffer: %s\n",
                level, strerror(errno));
        exit(1);
    }

    /* Print the message with level and newline to stderr. */
    fprintf(stderr, "[%s] %s\n", level, buf);
}

void mps_log_stderr_debug(const char *func, const char *file, int line,
                          const char *tag, const char *fmt, ...)
{
    va_list ap;
    char buf[TSLOG_STDERR_BUFSIZE];
    int n;
    size_t size;

    /* Determine required size */
    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        fprintf(stderr, "TSDebug cannot determine required size: %s\n",
                strerror(errno));
        exit(1);
    }
    size = (size_t)n + 1; /* +1 for '\0' */
    if (size > TSLOG_STDERR_BUFSIZE) {
        fprintf(stderr, "TSDebug message is too long\n");
        exit(1);
    }

    /* Print the message to the buffer. */
    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (n < 0) {
        fprintf(stderr, "TSDebug cannot print message to buffer: %s\n",
                strerror(errno));
        exit(1);
    }

    /* Print the message with level and newline to stderr. */
    fprintf(stderr, "[DEBUG] <%s:%d (%s)> (%s) %s\n", basename(file), line,
            func, tag, buf);
}
