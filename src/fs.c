/// @file fs.c
/// @brief Filesystem walk and stat implementation.
#include "fs.h"

#include <assert.h>
#include <errno.h>
#include <fts.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "jemalloc/jemalloc.h"

#include "log.h"

/// @brief Internal helper function to process a single file entry from the FTS
/// walk. The file's last modified time and size are extracted from the stat
/// structure and passed to the user-defined callback function.
/// @param ent The FTSENT file entry to process
/// @param filefn The user-defined callback function to invoke for the file
/// @param udata User data pointer to pass to the callback function
/// @return The return value of the user-defined callback function
static int fsprocent(FTSENT* ent, fswalkfn_t filefn, void* udata) {
#if defined(__FreeBSD__) || defined(__APPLE__)
  const struct timespec ts = ent->fts_statp->st_mtimespec; /* last modified */
#else
  const struct timespec ts = ent->fts_statp->st_mtim; /* last modified */
#endif
  struct fsstat_s st = {0};
  st.lmod = ts.tv_sec * 1000 + ts.tv_nsec / 1000000; /* convert to millis */
  st.fsze = ent->fts_statp->st_size;                 /* copy file size */
  return filefn(ent->fts_path, &st, udata);
}

int fswalk(const char* dir, fswalkfn_t filefn, void* udata) {
  char* const paths[] = {(char*) dir, NULL};
  FTS* ftsp = fts_open(paths, FTS_LOGICAL, NULL);
  if (ftsp == NULL) {
    log_error("fts_open error on `%s`: %s", dir, strerror(errno));
    return -1;
  }
  int err = 0;
  errno = 0;
  FTSENT* ent;
  while ((ent = fts_read(ftsp)) != NULL) {
    if (ent->fts_info == FTS_ERR) {// handle error entries
      log_error("fts_read error on `%s`: %s", ent->fts_path,
                strerror(ent->fts_errno));
    } else if (ent->fts_info == FTS_F) {
      if ((err = fsprocent(ent, filefn, udata))) goto doret;
    }
  }
  if (errno) {// fts_read will set errno if an error occurred
    log_error("fts_read error on `%s`: %s", dir, strerror(errno));
    err = -1;
  }
doret:
  fts_close(ftsp);
  return err;
}

int fsstat(const char* fp, struct fsstat_s* s) {
  struct stat st = {0};
  if (stat(fp, &st)) return -1;
#if defined(__FreeBSD__) || defined(__APPLE__)
  const struct timespec ts = st.st_mtimespec; /* last modified */
#else
  const struct timespec ts = st.st_mtim; /* last modified */
#endif
  s->lmod = ts.tv_sec * 1000 + ts.tv_nsec / 1000000; /* convert to millis */
  s->fsze = st.st_size;                              /* copy file size */
  return 0;
}

char* fsjoin(const char* dir, const char* file) {
  assert(dir != NULL);
  assert(file != NULL);
  const size_t sum = strlen(dir) + strlen(file) + 2; /* +1 for '/' & '\0' */
  char* fp = je_malloc(sum);
  if (fp == NULL) return NULL;
  snprintf(fp, sum, "%s/%s", dir, file);
  return fp;
}
