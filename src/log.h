#ifndef PROXY_LOG_H
#define PROXY_LOG_H

#define LOG_ALL_TEXT     "ALL"
#define LOG_TRACE_TEXT   "TRACE"
#define LOG_DEBUG_TEXT   "DEBUG"
#define LOG_INFO_TEXT    "INFO"
#define LOG_WARNING_TEXT "WARN"
#define LOG_ERROR_TEXT   "ERROR"
#define LOG_FATAL_TEXT   "FATAL"
#define LOG_OFF_TEXT     "OFF"

#define LOG_ALL_LEVEL     6
#define LOG_TRACE_LEVEL   6
#define LOG_DEBUG_LEVEL   5
#define LOG_INFO_LEVEL    4
#define LOG_WARNING_LEVEL 3
#define LOG_ERROR_LEVEL   2
#define LOG_FATAL_LEVEL   1
#define LOG_OFF_LEVEL     0

#define LOG_LEVEL_DEFAULT LOG_INFO_LEVEL

void logSetLevel(int log_level);

void logTrace(const char *format, ...);

void logDebug(const char *format, ...);

void logInfo(const char *format, ...);

void logWarning(const char *format, ...);

void logError(const char *format, ...);

void logFatal(const char *format, ...);

#endif
