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

struct thrd_s {
  _Atomic bool rsrvd;   /* thread reservation/lock flag */
  _Atomic bool canwork; /* work request ready to process flag */
  struct tpreq_s work;  /* work request to process */
  pthread_t tid;        /* pthread work thread id */
  _Bool fdsopen;        /* file descriptor set open flag */
  struct fdset_s fds;   /* file descriptor set */
};

static struct thrd_s** thrds;  /* thread pool */
static _Atomic bool haltthrds; /* thread pool halt flag */
static _Atomic int thrdrc;     /* thread pool reference counter */

static void* tpentrypoint(void* arg) {
  struct thrd_s* self = arg;
  atomic_fetch_add(&thrdrc, 1);
  while (!atomic_load(&haltthrds)) {
    if (!atomic_load(&self->rsrvd)) continue; /* wait for work reservation */

    // spin while waiting for the main thread to set the work request
    // this avoids the atomic state of rsrvd being set before the calling thread
    // has a chance to lock and set the work request
    while (!atomic_load(&self->canwork)) continue;
    atomic_store(&self->canwork, false);// reset work confirmation flag

    const struct tpreq_s* req = &self->work;
    int err;
    if ((err = lcmdexec(req->cs, req->node, &self->fds, req->flags)))
      log_error("thread execution error: %d", err);
    if (req->flags & (LCTRIG_NEW | LCTRIG_MOD)) {
      if ((err = fsstat(req->node->fp, &req->node->st)))
        log_error("stat error: %d", err);
    }

    atomic_store(&self->rsrvd, false);// release the reservation
  }
  atomic_fetch_sub(&thrdrc, 1);
  return NULL;
}

static void tpinitthrd(struct thrd_s* t, const int flags) {
  int err;
  if (flags & TPOPT_LOGFILES) {
    if ((err = fdinit(&t->fds, 0))) {
      log_error("file descriptor set open error: %d", err);
    } else {
      t->fdsopen = true;
    }
  }
  if (!t->fdsopen) {
    t->fds.out = STDOUT_FILENO;// default to stdout/stderr
    t->fds.err = STDERR_FILENO;
  }
}

int tpinit(const int size, const int flags) {
  assert(thrds == NULL);
  assert(size > 0);

  // add one for the NULL sentinel
  if ((thrds = calloc(size + 1, sizeof(struct thrd_s*))) == NULL) goto fail;
  for (int i = 0; i < size; i++) {
    if ((thrds[i] = calloc(1, sizeof(struct thrd_s))) == NULL) goto fail;
    struct thrd_s* t = thrds[i];
    tpinitthrd(t, flags);
    int err;
    if ((err = pthread_create(&t->tid, NULL, tpentrypoint, t))) {
      log_error("cannot create thread: %s", strerror(err));
      return -1;
    }
  }
  return 0;
fail:
  for (int i = 0; i < size; i++) free(thrds[i]);
  free(thrds);
  return -1;
}

int tpqueue(const struct tpreq_s* req) {
  assert(thrds != NULL);
  assert(req != NULL);

findnext:
  for (size_t i = 0; thrds[i] != NULL; i++) {
    struct thrd_s* t = thrds[i];
    bool isrsrvd = false;
    if (!atomic_compare_exchange_strong(&t->rsrvd, &isrsrvd, true))
      continue; /* thread is already reserved */
    memcpy(&t->work, req, sizeof(*req));
    atomic_store(&t->canwork, true); /* release lock/allow thread to continue */
    return 0;
  }
  goto findnext;// spin while waiting for a thread to be available
}

void tpwait(void) {
  for (size_t i = 0; thrds != NULL && thrds[i] != NULL; i++) {
    while (atomic_load(&thrds[i]->rsrvd))// wait for thread to become idle
      ;
  }
}

void tpshutdown(void) {
  log_info("waiting for %d threads to exit...", atomic_load(&thrdrc));
  atomic_store(&haltthrds, true);// signal threads to exit
  while (atomic_load(&thrdrc) > 0)
    ;
  for (size_t i = 0; thrds != NULL && thrds[i] != NULL; i++) {
    struct thrd_s* t = thrds[i];
    pthread_join(t->tid, NULL);
    if (t->fdsopen) {
      t->fdsopen = false;
      fdclose(&t->fds);
    }
  }
}

void tpfree(void) {
  for (size_t i = 0; thrds != NULL && thrds[i] != NULL; i++) free(thrds[i]);
  free(thrds);
  thrds = NULL;
}
