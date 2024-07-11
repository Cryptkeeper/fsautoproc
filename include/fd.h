#ifndef FSAUTOPROC_FD_H
#define FSAUTOPROC_FD_H

/// @brief `fdset_t` contains a set of file descriptors for stdout and stderr.
struct fdset_s {
  int out;
  int err;
};

/// @brief `fdinit()` initializes the stdout/stderr files used for redirecting
/// output from a child process. The file descriptors are stored in the provided
/// file descriptor set. The files are opened in write-only mode and are created
/// if they do not exist. If either file fails to open, the function will return
/// -1 and both file descriptors will be set to -1.
/// @param fds The file descriptor set to initialize
/// @param id Unique identifier to append to the file name
/// @return 0 if both files were opened successfully, otherwise -1
int fdinit(struct fdset_s* fds, unsigned int id);

/// @brief `fdclose()` closes the stdout and stderr file descriptors in the
/// provided file descriptor set using `close(2)` of a file descriptor set
/// successfully initialized by `fdinit()`.
void fdclose(struct fdset_s* fds);

#endif//FSAUTOPROC_FD_H
