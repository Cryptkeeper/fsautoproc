#include "../include/dq.h"

#include <stddef.h>

#include "../include/sl.h"

static struct {
  slist_t sl;
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
  if (sladd(&dirqueue.sl, dir)) return -1;
  dirqueue.len++;
  return 0;
}
