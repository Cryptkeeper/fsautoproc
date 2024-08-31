/// @file fl.h
/// @brief Basic file lock API.
#ifndef FSAUTOPROC_FL_H
#define FSAUTOPROC_FL_H

/// @struct flock_s
/// @brief File lock structure for managing a single file path.
struct flock_s {
  const char* path; ///< File path to lock
  int fd;           ///< File descriptor once opened
  _Bool open;       ///< Open status of the file
};

/// @def flinit
/// @brief Initializes a unlocked, ready-to-use file lock structure with the
/// given target lock file path. The file descriptor is set to -1 and is not
/// opened until the file is locked with `fllock()`.
/// @param fp The file path to use for the lock.
/// @return The initialized file lock structure.
#define flinit(fp) ((struct flock_s){.path = (fp), .fd = -1, .open = 0})

/// @brief Locks the file at the given path. If a file descriptor cannot be
/// obtained, or the file cannot be locked, an error code is returned.
/// @note The lock must be initialized with `flinit()` before calling `fllock`.
/// @param fl The file lock structure.
/// @return 0 if successful, otherwise a non-zero error code.
int fllock(struct flock_s* fl);

/// @brief Unlocks the file at the given path. If the file is not open, or the
/// file cannot be unlocked, an error code is returned.
/// @param fl The file lock structure.
/// @return 0 if successful, otherwise a non-zero error code.
int flunlock(struct flock_s* fl);

#endif//FSAUTOPROC_FL_H
