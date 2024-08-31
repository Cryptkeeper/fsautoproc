/// @file fd.h
/// @brief File descriptor output redirection for child processes.
#ifndef FSAUTOPROC_FD_H
#define FSAUTOPROC_FD_H

/// @struct fdset_s
/// @brief A set of file descriptors for redirecting writes to stdout and stderr
/// from child processes to log files.
struct fdset_s {
  int out; ///< File descriptor for writing to stdout
  int err; ///< File descriptor for writing to stderr
};

/// @brief Initializes stdout/stderr files used for redirecting output from a
/// child process. The file descriptors are stored in the provided file
/// descriptor set. The files are opened in write-only mode and are created if
/// they do not exist. If either file fails to open, the function will return -1
/// and both file descriptors will be set to -1.
/// @param fds The file descriptor set to initialize
/// @param id Unique identifier to append to the file name
/// @return 0 if both files were opened successfully, otherwise -1
int fdinit(struct fdset_s* fds, unsigned int id);

/// @brief Closes the stdout and stderr file descriptors in the provided file
/// descriptor set using `close(2)`. If either file descriptor is already
/// closed, the function will not attempt to close it again.
/// @param fds The file descriptor set to close
/// @note The file descriptors must be initialized with `fdinit` before calling
/// this function.
void fdclose(struct fdset_s* fds);

#endif//FSAUTOPROC_FD_H
