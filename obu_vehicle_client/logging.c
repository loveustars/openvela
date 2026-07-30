#include "logging.h"

#include <stdarg.h>
#include <stdio.h>

static void log_with_level(const char *level, const char *tag, const char *fmt, va_list args)
{
    printf("[%s][%s] ", level, tag);
    vprintf(fmt, args);
    printf("\n");
}

void log_info(const char *tag, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_with_level("INFO", tag, fmt, args);
    va_end(args);
}

void log_warn(const char *tag, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_with_level("WARN", tag, fmt, args);
    va_end(args);
}

void log_error(const char *tag, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_with_level("ERROR", tag, fmt, args);
    va_end(args);
}
