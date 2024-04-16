#include "sys.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void perrorf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, ": %s\n", strerror(errno));
}