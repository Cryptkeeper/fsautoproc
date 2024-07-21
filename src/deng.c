#include "deng.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "index.h"
#include "log.h"
#include "sl.h"
#include "tp.h"

struct deng_state_s {
  slist_t* dirqueue;                /* processing directory queue */
  const struct deng_hooks_s* hooks; /* file event hook functions */
  struct index_s* lastmap;          /* previous index state */
  struct index_s* thismap;          /* current index state */
};

/// @brief Processes a file before the command execution stage to ensure all
/// files are indexed. This function may trigger new (NEW), modified (MOD),
/// and unmodified (NOP) events for each file in the directory tree.
/// @param fp The file path to process
/// @param udata The diff engine state context
/// @return 0 if successful, otherwise a non-zero error code.
static int stagepre(const char* fp, void* udata) {
  struct deng_state_s* mach = (struct deng_state_s*) udata;
  if (mach->hooks->filter_junk(fp)) return 0;

  struct inode_s finfo = {0};
  if ((finfo.fp = strdup(fp)) == NULL) return -1;
  if (fsstat(fp, &finfo.st)) return -1;

  // attempt to match file in previous index
  struct inode_s* prev = indexfind(mach->lastmap, fp);

  // lookup from previous iteration or insert new record and lookup
  struct inode_s* curr = indexfind(mach->thismap, fp);
  if (curr == NULL)
    if ((curr = indexput(mach->thismap, finfo)) == NULL) return -1;

  if (prev != NULL && !fsstateql(&prev->st, &curr->st)) {
    mach->hooks->mod(curr);
  } else if (prev != NULL) {
    mach->hooks->nop(curr);
  } else {
    mach->hooks->new (curr);
  }

  return 0;
}

/// @brief Processes a file after the command execution stage to ensure all
/// files are indexed. This function may trigger new (NEW) and modified (MOD)
/// events for each file in the directory tree.
/// @param fp The file path to process
/// @param udata The diff engine state context
/// @return 0 if successful, otherwise a non-zero error code.
static int stagepost(const char* fp, void* udata) {
  struct deng_state_s* mach = (struct deng_state_s*) udata;
  if (mach->hooks->filter_junk(fp)) return 0;

  struct inode_s* curr = indexfind(mach->thismap, fp);
  if (curr != NULL) {
    // check if the file was modified during the command execution
    struct fsstat_s mod = {0};
    if (fsstat(fp, &mod)) return -1;
    curr->st = mod;// update the file info in the current index
    return 0;
  }

  struct inode_s finfo = {0};
  if ((finfo.fp = strdup(fp)) == NULL) return -1;
  if (fsstat(fp, &finfo.st)) return -1;
  if ((curr = indexput(mach->thismap, finfo)) == NULL) return -1;
  mach->hooks->new (curr);

  return 0;
}

/// @brief Pushes a directory path onto the directory queue for processing.
/// @param fp The directory path to push
/// @param udata The diff engine state context
/// @return 0 if successful, otherwise a non-zero error code.
static int dqpush(const char* fp, void* udata) {
  struct deng_state_s* mach = (struct deng_state_s*) udata;
  int err;
  if ((err = sladd(&mach->dirqueue, fp)))
    log_error("error pushing directory `%s`", fp);
  return err;
}

/// @brief Resets the directory queue to the initial search path, and invokes
/// the `filefn` function for each file in the directory tree, recursively.
/// @param mach The diff engine state context
/// @param sd The initial search directory path
/// @param filefn The function to invoke for each file in the directory tree
/// @return 0 if successful, otherwise a non-zero error code.
static int execstage(struct deng_state_s* mach, const char* sd,
                     fswalkfn_t filefn) {
  slfree(mach->dirqueue);
  mach->dirqueue = NULL;
  if (sladd(&mach->dirqueue, sd)) return -1;

  char* dir;
  while ((dir = slpop(mach->dirqueue)) != NULL) {
    int err;
    if ((err = fswalk(dir, filefn, dqpush, (void*) mach))) {
      log_error("file func for `%s` returned %d", dir, err);
      return -1;
    }
    mach->hooks->notify_done();
    free(dir);
  }
  tpwait();

  return 0;
}

/// @brief Compares the current file system state with a previous index to
/// determine which files were removed. This function may trigger deleted (DEL)
/// events for each file in the previous index that is not present in the
/// current index.
static int checkremoved(struct deng_state_s* mach) {
  if (mach->lastmap->size == 0) return 0;// no previous map entries to check

  struct inode_s** lastlist;
  if ((lastlist = indexlist(mach->lastmap)) == NULL) return -1;
  for (long i = 0; i < mach->lastmap->size; i++) {
    struct inode_s* prev = lastlist[i];
    if (indexfind(mach->thismap, prev->fp) != NULL) continue;
    mach->hooks->del(prev);
  }
  free(lastlist);
  tpwait();

  return 0;
}

int dengsearch(const char* sd, const struct deng_hooks_s* hooks,
             struct index_s* old, struct index_s* new) {
  assert(sd != NULL);
  assert(hooks != NULL);
  assert(old != NULL);
  assert(new != NULL);

  struct deng_state_s mach = {NULL, hooks, old, new};
  int err;
  if ((err = execstage(&mach, sd, stagepre))) goto ret;
  if ((err = checkremoved(&mach))) goto ret;
  if ((err = execstage(&mach, sd, stagepost))) goto ret;
ret:
  slfree(mach.dirqueue);
  return err;
}
