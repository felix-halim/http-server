#include <cstdio>
#include <cstdarg>

#include "logger.h"

namespace simple_http {

#define LOG_FMT_STDERR(prefix)        \
  va_list args; va_start(args, fmt);  \
  fprintf(stderr, prefix);            \
  vfprintf(stderr, fmt, args);        \
  fprintf(stderr, "\n");              \
  va_end(args);

void Log::severe(const char *fmt, ... ) { LOG_FMT_STDERR("\e[00;31mSEVERE\e[00m ") }
void Log::warn(const char *fmt, ... ) { LOG_FMT_STDERR("\e[1;33mWARN\e[00m ") }
void Log::info(const char *fmt, ... ) { LOG_FMT_STDERR("\e[0;32mINFO\e[00m ") }

}
