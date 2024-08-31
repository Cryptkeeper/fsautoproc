/// @file prog.c
/// @brief Progress bar format and printing implementation.
#include "prog.h"

#include <stdio.h>

/// @def PROGBARLEN
/// @brief The fixed length of a formatted progress bar.
#define PROGBARLEN 15

void printprogbar(const long curr, const long max) {
  if (max == 0) return;
  const long barlen = (curr * PROGBARLEN) / max;
  static long lastbarlen = -1;
  if (lastbarlen >= 0 && barlen == lastbarlen) return;
  lastbarlen = barlen;
  static char progbar[PROGBARLEN + 3];
  long i = 0;
  progbar[i++] = '[';
  for (long j = 0; j < PROGBARLEN; j++) progbar[i++] = j <= barlen ? '#' : ' ';
  progbar[i++] = ']';
  printf("%s %ld\n", progbar, max - curr);
  fflush(stdout);
}
