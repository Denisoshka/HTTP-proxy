#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define COLOR_RESET     "\033[0m"
#define COLOR_WHITE     "\033[1;37m"
#define COLOR_CYAN      "\033[1;36m"
#define COLOR_GREEN     "\033[1;32m"
#define COLOR_YELLOW    "\033[1;33m"
#define COLOR_RED       "\033[1;31m"
#define COLOR_PINK      "\033[1;35m"

#define MAX_LOG_MESSAGE_LENGTH  1024

#define LOG_COMMON(format, log_level_, color)       \
    va_list args;                                   \
    va_start(args, format);                         \
    logCommon(log_level_, color, format, args);     \
    va_end(args)

static int log_level = LOG_LEVEL_DEFAULT;

static int checkLogLevel(int log_level_);

static void logCommon(const char *log_level_, const char *color,
                      const char *format, va_list         args);

void logSetLevel(int log_level_) {
  log_level = log_level_;
}

void logTrace(const char *format, ...) {
  if (!checkLogLevel(LOG_TRACE_LEVEL)) return;
  LOG_COMMON(format, LOG_TRACE_TEXT, COLOR_WHITE);
}

void logDebug(const char *format, ...) {
  if (!checkLogLevel(LOG_DEBUG_LEVEL)) return;
  LOG_COMMON(format, LOG_DEBUG_TEXT, COLOR_CYAN);
}

void logInfo(const char *format, ...) {
  if (!checkLogLevel(LOG_INFO_LEVEL)) return;
  LOG_COMMON(format, LOG_INFO_TEXT, COLOR_GREEN);
}

void logWarning(const char *format, ...) {
  if (!checkLogLevel(LOG_WARNING_LEVEL)) return;
  LOG_COMMON(format, LOG_WARNING_TEXT, COLOR_YELLOW);
}

void logError(const char *format, ...) {
  if (!checkLogLevel(LOG_ERROR_LEVEL)) return;
  LOG_COMMON(format, LOG_ERROR_TEXT, COLOR_RED);
}

void logFatal(const char *format, ...) {
  if (!checkLogLevel(LOG_FATAL_LEVEL)) return;
  LOG_COMMON(format, LOG_FATAL_TEXT, COLOR_PINK);
  exit(EXIT_FAILURE);
}

static int checkLogLevel(int log_level_) {
  return log_level >= log_level_;
}

static void logCommon(const char *log_level_, const char *color,
                      const char *format, va_list         args) {
  struct timeval tv;
  gettimeofday(&tv, 0);

  time_t     stamp_time = time(NULL);
  struct tm *tm = localtime(&stamp_time);

  char text[MAX_LOG_MESSAGE_LENGTH + 1] = {0};
  vsnprintf(text, MAX_LOG_MESSAGE_LENGTH, format, args);

  char thread_name[256];
  pthread_getname_np(pthread_self(), thread_name, 256);

  char msg[MAX_LOG_MESSAGE_LENGTH + 1];
  snprintf(msg, MAX_LOG_MESSAGE_LENGTH,
           "%d-%02d-%02d %02d:%02d:%02d.%03ld --- [%15s] %s%5s%s : %s",
           tm->tm_year + 1900,
           tm->tm_mon + 1,
           tm->tm_mday,
           tm->tm_hour,
           tm->tm_min,
           tm->tm_sec,
           tv.tv_usec / 1000 % 1000,
           thread_name,
           color,
           log_level_,
           COLOR_RESET,
           text);

  puts(msg);
  fflush(stdout);
}
