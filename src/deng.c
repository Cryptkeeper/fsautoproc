/// @file deng.c
/// @brief Differential file search implementation
#include "deng.h"

#include <assert.h>
#include <stdio.h>

#include "jemalloc/jemalloc.h"

#include "fs.h"
#include "index.h"
#include "log.h"

/// @struct deng_state_s
/// @brief Search state context provided to the diff engine as user data which
/// is passed to the file event hook functions.
struct deng_state_s {
  deng_filter_t ffn;               ///< File filter function
  const struct deng_hooks_s* hooks;///< File event hook functions
  const struct index_s* lastmap;   ///< Previous index state
  struct index_s* thismap;         ///< Current index state
};

/// @def callevent
/// @brief Invokes a file event hook function if it is not NULL.
/// @param mach The diff engine state context
/// @param type The event type to pass to the hook function
/// @param arg The argument to pass to the hook function
#define callevent(mach, type, arg)                                             \
  do {                                                                         \
    if ((mach)->hooks->event != NULL) (mach)->hooks->event(type, arg);         \
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
/// @param st The file stat information
/// @param udata The diff engine state context
/// @return 0 if successful, otherwise a non-zero error code.
static int stagepre(const char* fp, const struct fsstat_s* st, void* udata) {
  struct deng_state_s* mach = (struct deng_state_s*) udata;
  notifyhook(mach, DENG_NOTIF_FILE_FOUND);

  if (mach->ffn != NULL && mach->ffn(fp)) return 0;// skip filtered files

  const uint64_t fphash = indexhash(fp);

  // attempt to match file in previous index
  struct inode_s* prev = indexfind(mach->lastmap, fp, fphash);

  // lookup from previous iteration or insert new record and lookup
  struct inode_s* curr = indexfind(mach->thismap, fp, fphash);
  if (curr == NULL)
    if ((curr = indexput(mach->thismap, fp, fphash, st)) == NULL) return -1;

  if (prev != NULL) {
    if (prev->st.lmod != st->lmod || prev->st.fsze != st->fsze)
      callevent(mach, DENG_FEVENT_MOD, curr);
  } else {
    callevent(mach, DENG_FEVENT_NEW, curr);
  }

  return 0;
}

/// @brief Processes a file after the command execution stage to ensure all
/// files are indexed. This function may trigger new (NEW) and modified (MOD)
/// events for each file in the directory tree.
/// @param fp The file path to process
/// @param st The file stat information
/// @param udata The diff engine state context
/// @return 0 if successful, otherwise a non-zero error code.
static int stagepost(const char* fp, const struct fsstat_s* st, void* udata) {
  struct deng_state_s* mach = (struct deng_state_s*) udata;
  notifyhook(mach, DENG_NOTIF_FILE_FOUND);

  if (mach->ffn != NULL && mach->ffn(fp)) return 0;// skip filtered files

  const uint64_t fphash = indexhash(fp);

  struct inode_s* curr = indexfind(mach->thismap, fp, fphash);
  if (curr != NULL) {
    curr->st = *st;// update the file info in the current index
    return 0;
  }

  if ((curr = indexput(mach->thismap, fp, fphash, st)) == NULL) return -1;
  callevent(mach, DENG_FEVENT_NEW, curr);

  return 0;
}

/// @brief Walks the directory tree starting at \p sd and invokes the provided
/// file function \p filefn for each file found. After the walk is complete,
/// the stage done notification is triggered.
/// @param mach The diff engine state context
/// @param sd The initial search directory path
/// @param filefn The function to invoke for each file in the directory tree
/// @return 0 if successful, otherwise a non-zero error code.
static int execstage(struct deng_state_s* mach, const char* sd,
                     fswalkfn_t filefn) {
  int err;
  if ((err = fswalk(sd, filefn, (void*) mach))) {
    log_error("file func for `%s` returned %d", sd, err);
    return -1;
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
    if (indexfind(mach->thismap, prev->fp, prev->fphash) != NULL) continue;
    callevent(mach, DENG_FEVENT_DEL, prev);
  }
  je_free(lastlist);
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

  struct deng_state_s mach = {filter, hooks, old, new};
  int err;
  if ((err = execstage(&mach, sd, stagepre))) goto ret;
  if ((err = checkremoved(&mach))) goto ret;
  if ((err = execstage(&mach, sd, stagepost))) goto ret;
ret:
  return err;
}
