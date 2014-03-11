#ifndef SIMPLE_LOGGER_
#define SIMPLE_LOGGER_

namespace simple_http {
  // Global logging to standard error.
  struct Log {
    static int max_level;
    static void severe(const char *fmt, ... );
    static void warn(const char *fmt, ... );
    static void info(const char *fmt, ... );
  };
}

#endif
