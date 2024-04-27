#include "tp.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_pthread/_pthread_t.h>

#include "log.h"

#include "lcmd.h"

struct tpool_s {
  _Atomic bool busy;
  pthread_t tid;
  struct tpreq_s req;
};

static struct tpool_s** pools;

int tpinit(const int size) {
  assert(pools == NULL);
  assert(size > 0);

  // add one for the NULL sentinel
  if ((pools = calloc(size + 1, sizeof(struct tpool_s*))) == NULL) goto err;
  for (int i = 0; i < size; i++)
    if ((pools[i] = calloc(1, sizeof(struct tpool_s))) == NULL) goto err;
  return 0;
err:
  for (int i = 0; i < size; i++) free(pools[i]);
  free(pools);
  return -1;
}

static void* tpentrypoint(void* arg) {
  struct tpool_s* self = arg;
  int err;
  if ((err = lcmdexec(self->req.cs, self->req.node, self->req.flags)))
    log_error("thread execution error: %d", err);
  self->busy = false;
  return NULL;
}

int tpqueue(const struct tpreq_s* req) {
  assert(pools != NULL);
  assert(req != NULL);

findnext:
  for (size_t i = 0; pools[i] != NULL; i++) {
    struct tpool_s* t = pools[i];
    if (t->busy) continue;
    memcpy(&t->req, req, sizeof(*req));
    int err;
    if ((err = pthread_create(&t->tid, NULL, tpentrypoint, t))) {
      log_error("cannot create thread: %s", strerror(err));
      return -1;
    }
    t->busy = true;
    return 0;
  }
  goto findnext;// spin while waiting for a thread to be available
}

int tpwait(void) {
  for (size_t i = 0; pools != NULL && pools[i] != NULL; i++) {
    struct tpool_s* t = pools[i];
    if (!t->busy) continue;
    int err;
    if ((err = pthread_join(t->tid, NULL))) {
      log_error("cannot join thread %lu: %s", t->tid, strerror(err));
      return -1;
    }
    t->busy = false;
  }
  return 0;
}
