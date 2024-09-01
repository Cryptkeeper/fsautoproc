/// @file deng.c
/// @brief Differential file search implementation
#include "deng.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "index.h"
#include "log.h"

#define SL_IMPL
#include "sl.h"

/// @struct deng_state_s
/// @brief Search state context provided to the diff engine as user data which
/// is passed to the file event hook functions.
struct deng_state_s {
  slist_t* dirqueue;                ///< Processing directory queue
  deng_filter_t ffn;                ///< File filter function
  const struct deng_hooks_s* hooks; ///< File event hook functions
  const struct index_s* lastmap;    ///< Previous index state
  struct index_s* thismap;          ///< Current index state
};

/// @def invokehook
/// @brief Invokes a file event hook function if it is not NULL.
/// @param mach The diff engine state context
/// @param name The member of the hook function to invoke
/// @param arg The argument to pass to the hook function
#define invokehook(mach, name, arg)                                            \
  do {                                                                         \
    if ((mach)->hooks->name != NULL) (mach)->hooks->name(arg);                 \
  } while (0)

/// @def notifyhook
/// @brief Invokes the notify hook function if it is not NULL.
/// @param mach The diff engine state context
/// @param type The notification type to pass to the hook
#define notifyhook(mach, type)                                                 \
  do {                                                                         \
    if ((mach)->hooks->notify != NULL) (mach)->hooks->notify(type);            \
  } while (0)

/// @brief Processes a file before the command execution stage to ensure all
/// files are indexed. This function may trigger new (NEW), modified (MOD),
/// and unmodified (NOP) events for each file in the directory tree.
/// @param fp The file path to process
/// @param udata The diff engine state context
/// @return 0 if successful, otherwise a non-zero error code.
static int stagepre(const char* fp, void* udata) {
  struct deng_state_s* mach = (struct deng_state_s*) udata;
  if (mach->ffn != NULL && mach->ffn(fp)) return 0;

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
    invokehook(mach, mod, curr);
  } else if (prev != NULL) {
    invokehook(mach, nop, curr);
  } else {
    invokehook(mach, new, curr);
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
  if (mach->ffn != NULL && mach->ffn(fp)) return 0;

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
  invokehook(mach, new, curr);

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
    notifyhook(mach, DENG_NOTIF_DIR_DONE);
    free(dir);
  }
  notifyhook(mach, DENG_NOTIF_STAGE_DONE);

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
    invokehook(mach, del, prev);
  }
  free(lastlist);
  notifyhook(mach, DENG_NOTIF_STAGE_DONE);

  return 0;
}

int dengsearch(const char* sd, deng_filter_t filter,
               const struct deng_hooks_s* hooks, const struct index_s* old,
               struct index_s* new) {
  assert(sd != NULL);
  assert(hooks != NULL);
  assert(old != NULL);
  assert(new != NULL);

  struct deng_state_s mach = {NULL, filter, hooks, old, new};
  int err;
  if ((err = execstage(&mach, sd, stagepre))) goto ret;
  if ((err = checkremoved(&mach))) goto ret;
  if ((err = execstage(&mach, sd, stagepost))) goto ret;
ret:
  slfree(mach.dirqueue);
  return err;
}
