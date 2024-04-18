#include "dq.h"

#include <pthread.h>
#include <stddef.h>

#include "sl.h"

static struct {
  char** sl;
  size_t i;
  size_t len;
} dirqueue;

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

char* dqnext(void) {
  char* dir = NULL;
  pthread_mutex_lock(&m);
  if (dirqueue.i < dirqueue.len) dir = dirqueue.sl[dirqueue.i];
  dirqueue.i++;
  pthread_mutex_unlock(&m);
  return dir;
}

void dqreset(void) {
  pthread_mutex_lock(&m);
  slfree(dirqueue.sl);
  dirqueue.sl = NULL;
  dirqueue.i = 0;
  dirqueue.len = 0;
  pthread_mutex_unlock(&m);
}

int dqpush(const char* dir) {
  char** r;
  pthread_mutex_lock(&m);
  if ((r = sladd(dirqueue.sl, dir)) != NULL) dirqueue.sl = r, dirqueue.len++;
  pthread_mutex_unlock(&m);
  return r == NULL ? -1 : 0;
}
