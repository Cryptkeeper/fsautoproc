/// @file tp.c
/// @brief Thread pool implementation for executing work requests.
#include "tp.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "jemalloc/jemalloc.h"

#include "fd.h"
#include "fs.h"
#include "index.h"
#include "lcmd.h"
#include "log.h"
#include "tm.h"
#include "xx.h"

/// @struct thrd_s
/// @brief Initialized worker thread in the thread pool.
struct thrd_s {
  _Atomic bool initd;     ///< Thread initialized flag
  _Atomic bool rsrvd;     ///< Work request reservation flag
  _Atomic bool canwork;   ///< Work request ready to process flag
  struct tpreq_s work;    ///< Work request to process
  pthread_t tid;          ///< System thread identifier
  _Bool fdsopen;          ///< File descriptor set open flag
  struct fdset_s fds;     ///< Output file descriptor set
  uint8_t xxbuf[XXBUFSZE];///< Buffer for reading files to hash
};

static struct thrd_s** thrds; ///< Thread pool worker threads array
static _Atomic bool haltthrds;///< Thread pool halt flag
static _Atomic int thrdrc;    ///< Thread pool thread count
static _Atomic int idlerc;    ///< Thread pool idle thread count

/// @brief Thread pool worker thread entry point. The thread will spin lock
/// while waiting to be reserved. Once reserved, it spin locks while waiting
/// for the main thread to finalize the work request. Once ready, the work
/// request is executed and the thread is released back to the pool.
/// @param arg The thread self context
/// @return NULL in all cases
static void* tpentrypoint(void* arg) {
  struct thrd_s* self = arg;
  atomic_fetch_add(&thrdrc, 1);
  while (!atomic_load(&haltthrds)) {
    if (!atomic_load(&self->rsrvd)) {// wait for work reservation
      const enum sleepdur_t idle =
              atomic_fetch_add(&idlerc, 1) > 0 ? DUR_IDLE_LONG : DUR_IDLE_SHORT;
      tmsleep(idle);
      atomic_fetch_sub(&idlerc, 1);
      continue;
    }

    // spin while waiting for the main thread to set the work request
    // this avoids the atomic state of rsrvd being set before the calling thread
    // has a chance to lock and set the work request
    while (!atomic_load(&self->canwork)) continue;
    atomic_store(&self->canwork, false);// reset work confirmation flag

    const struct tpreq_s* req = &self->work;
    int err;
    if ((err = lcmdexec(req->cs, req->node->fp, self->fds, req->flags)))
      log_error("thread execution error: %d", err);
    if (req->flags & (LCTRIG_NEW | LCTRIG_MOD)) {
      struct fsstat_s st = {0};
      if ((err = fsstat(req->node->fp, &st))) {
        log_error("stat error: %d", err);
      } else {
        // check if the stats have changed, if so, re-hash
        struct xxreq_s xx = {0};
        xx.b = self->xxbuf;// use thread-local buffer
        xx.fp = req->node->fp;
        xx.pst = &req->node->st;
        xx.cst = &st;
        xx.pxx = req->node->xx;
        xx.cause = "stage=tp";
        req->node->xx = xxupdate(&xx);
        req->node->st = st;
      }
    }

    atomic_store(&self->rsrvd, false);// release the reservation
  }
  atomic_fetch_sub(&thrdrc, 1);
  return NULL;
}

/// @brief Configures an initialized thread with the specified options. If the
/// TPOPT_LOGFILES flag is set, the file descriptor set is initialized. If the
/// file descriptor set initialization fails, or the flag is not set, the file
/// descriptors will default to \p STDOUT_FILENO and \p STDERR_FILENO.
/// @param t The thread to configure
/// @param uid The thread pool thread ID for logging
/// @param flags The configuration flags
static void tpinitthrd(struct thrd_s* t, const int uid, const int flags) {
  int err;
  if (flags & TPOPT_LOGFILES) {
    if ((err = fdinit(&t->fds, uid))) {
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
  if ((thrds = je_calloc(size + 1, sizeof(struct thrd_s*))) == NULL) goto fail;
  for (int i = 0; i < size; i++) {
    if ((thrds[i] = je_calloc(1, sizeof(struct thrd_s))) == NULL) goto fail;
    tpinitthrd(thrds[i], i, flags);
  }
  return 0;
fail:
  for (int i = 0; i < size; i++) je_free(thrds[i]);
  je_free(thrds);
  return -1;
}

int tpqueue(const struct tpreq_s* req) {
  assert(thrds != NULL);
  assert(req != NULL);

findnext:
  for (size_t i = 0; thrds[i] != NULL; i++) {
    struct thrd_s* t = thrds[i];
    if (atomic_exchange(&t->rsrvd, true))
      continue;                              /* thread is already reserved */
    if (!atomic_exchange(&t->initd, true)) { /* lazy init pthread instance */
      int err;
      if ((err = pthread_create(&t->tid, NULL, tpentrypoint, t))) {
        log_error("cannot create thread: %s", strerror(err));
        return -1;
      }
    }
    memcpy(&t->work, req, sizeof(*req));
    atomic_store(&t->canwork, true); /* release lock/allow thread to continue */
    return 0;
  }
  tmsleep(DUR_QUEUE);
  goto findnext;// spin while waiting for a thread to be available
}

void tpwait(void) {
  for (size_t i = 0; thrds != NULL && thrds[i] != NULL; i++) {
    while (atomic_load(&thrds[i]->rsrvd))// wait for thread to become idle
      tmsleep(DUR_WAIT);
  }
}

void tpshutdown(void) {
  atomic_store(&haltthrds, true);// signal threads to exit
  while (atomic_load(&thrdrc) > 0) tmsleep(DUR_HALT);
  for (size_t i = 0; thrds != NULL && thrds[i] != NULL; i++) {
    struct thrd_s* t = thrds[i];
    if (atomic_exchange(&t->initd, false)) pthread_join(t->tid, NULL);
    if (t->fdsopen) {
      t->fdsopen = false;
      fdclose(&t->fds);
    }
  }
}

void tpfree(void) {
  for (size_t i = 0; thrds != NULL && thrds[i] != NULL; i++) je_free(thrds[i]);
  je_free(thrds);
  thrds = NULL;
}
