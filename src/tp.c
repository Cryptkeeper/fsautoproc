#include "tp.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fd.h"
#include "fs.h"
#include "index.h"
#include "lcmd.h"
#include "log.h"

struct tpool_s {
  _Atomic bool isbusy; /* thread busy flag for scheduler */
  pthread_t tid;       /* pthread work thread id */
  struct tpreq_s req;  /* work request to process */
  unsigned poolid;     /* index in pools array (for file naming) */
  _Bool fdsopen;       /* file descriptor set open flag */
  struct fdset_s fds;  /* file descriptor set */
};

static struct tpool_s** pools;

int tpinit(const int size, const int flags) {
  assert(pools == NULL);
  assert(size > 0);

  // add one for the NULL sentinel
  if ((pools = calloc(size + 1, sizeof(struct tpool_s*))) == NULL) goto err;
  for (int i = 0; i < size; i++) {
    if ((pools[i] = calloc(1, sizeof(struct tpool_s))) == NULL) goto err;
    struct tpool_s* t = pools[i];
    t->poolid = i;
    if (!(flags & TPOPT_LOGFILES)) {
      t->fds.out = STDOUT_FILENO;// default to stdout/stderr
      t->fds.err = STDERR_FILENO;
      t->fdsopen = true;
    }
  }
  return 0;
err:
  for (int i = 0; i < size; i++) free(pools[i]);
  free(pools);
  return -1;
}

static bool tpshouldrestat(const struct tpreq_s* req) {
  return req->flags & (LCTRIG_NEW | LCTRIG_MOD);
}

static void* tpentrypoint(void* arg) {
  struct tpool_s* self = arg;
  int err;
  if (!self->fdsopen) {
    if ((err = fdinit(&self->fds, self->poolid))) {
      log_error("file descriptor set open error: %d", err);
      self->fds.out = STDOUT_FILENO;// fallback to stdout/stderr
      self->fds.err = STDERR_FILENO;
    }
    self->fdsopen = true;
  }
  if ((err = lcmdexec(self->req.cs, self->req.node, &self->fds,
                      self->req.flags)))
    log_error("thread execution error: %d", err);
  if (tpshouldrestat(&self->req)) {
    struct inode_s* node = self->req.node;
    if ((err = fsstat(node->fp, &node->st))) log_error("stat error: %d", err);
  }
  atomic_store(&self->isbusy, false);
  return NULL;
}

int tpqueue(const struct tpreq_s* req) {
  assert(pools != NULL);
  assert(req != NULL);

findnext:
  for (size_t i = 0; pools[i] != NULL; i++) {
    struct tpool_s* t = pools[i];
    bool expected = false;
    if (!atomic_compare_exchange_strong(&t->isbusy, &expected, true)) continue;
    memcpy(&t->req, req, sizeof(*req));
    int err;
    if ((err = pthread_create(&t->tid, NULL, tpentrypoint, t))) {
      log_error("cannot create thread: %s", strerror(err));
      return -1;
    }
    return 0;
  }
  goto findnext;// spin while waiting for a thread to be available
}

int tpwait(void) {
  for (size_t i = 0; pools != NULL && pools[i] != NULL; i++) {
    struct tpool_s* t = pools[i];
    bool expected = true;
    if (!atomic_compare_exchange_strong(&t->isbusy, &expected, false)) continue;
    int err;
    if ((err = pthread_join(t->tid, NULL))) {
      log_error("cannot join thread: %s", strerror(err));
      return -1;
    }
  }
  return 0;
}

void tpfree(void) {
  for (size_t i = 0; pools != NULL && pools[i] != NULL; i++) {
    if (pools[i]->fdsopen) fdclose(&pools[i]->fds);
    free(pools[i]);
  }
  free(pools);
  pools = NULL;
}
