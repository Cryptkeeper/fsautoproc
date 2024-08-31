/// @file fl.c
/// @brief Basic file lock implementation.
#include "fl.h"

#include <assert.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/file.h>
#include <unistd.h>

/// @brief Opens the file at the given path and updates the file lock structure
/// with the file descriptor. If the file is already open, the file descriptor
/// is not updated and the function will return successfully. If the file cannot
/// be opened, the error code is returned via the file lock structure.
/// @param fl The file lock structure to update.
/// @return 0 if successful, otherwise a non-zero error code.
static int flopen(struct flock_s* fl) {
  assert(fl->path != NULL);
  if (fl->open) return 0;// file is already open
  fl->fd = open(fl->path, O_RDWR | O_CREAT | O_TRUNC, 0644);
  fl->open = fl->fd >= 0;
  return fl->open ? 0 : -1;
}

int fllock(struct flock_s* fl) {
  assert(fl->path != NULL);
  if (flopen(fl)) return -1;                // get or open file descriptor
  if (flock(fl->fd, LOCK_EX) < 0) return -2;// lock file descriptor
  return 0;
}

int flunlock(struct flock_s* fl) {
  assert(fl->path != NULL);
  if (!fl->open) return -1;                 // ensure file is open
  if (flock(fl->fd, LOCK_UN) < 0) return -2;// release lock
  close(fl->fd);                            // close file descriptor
  unlink(fl->path);                         // remove unlocked file
  fl->fd = -1;
  fl->open = 0;
  return 0;
}
