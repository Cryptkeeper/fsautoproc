#include "dq.h"

#include <stddef.h>

#include "sl.h"

static struct {
  char** sl;
  size_t i;
  size_t len;
} dirqueue;

char* dqnext(void) {
  if (dirqueue.i >= dirqueue.len) return NULL;
  return dirqueue.sl[dirqueue.i++];
}

void dqreset(void) {
  slfree(dirqueue.sl);
  dirqueue.sl = NULL;
  dirqueue.i = 0;
  dirqueue.len = 0;
}

int dqpush(const char* dir) {
  char** r;
  if ((r = sladd(dirqueue.sl, dir)) == NULL) return -1;
  dirqueue.sl = r, dirqueue.len++;
  return 0;
}