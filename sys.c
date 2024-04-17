#include "sys.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

void perrorf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  char* s;
  if ((s = malloc(n + 1)) == NULL) {
    perror(NULL);
    return;
  }
  va_start(args, fmt);
  vsnprintf(s, n + 1, fmt, args);
  va_end(args);
  log_error("%s: %s", s, strerror(errno));
}