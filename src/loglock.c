#include "loglock.h"

#include <pthread.h>
#include <stdbool.h>

static pthread_mutex_t logmutex = PTHREAD_MUTEX_INITIALIZER;

static void loglock(const bool lock, void* udata) {
  (void) udata;
  if (lock) pthread_mutex_lock(&logmutex);
  else
    pthread_mutex_unlock(&logmutex);
}

log_LockFn loglockfn = loglock;
