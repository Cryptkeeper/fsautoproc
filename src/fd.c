/// @file fd.c
/// @brief File descriptor output redirection implementation.
#include "fd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

/// @brief Opens, or creates, a truncated file using a formatted path derived
/// from the provided \p name and \p id arguments.
/// @param name The base name of the file to open
/// @param id The unique identifier to append to the file name
/// @return The file descriptor of the opened file, or -1 on error
/// @note Due to a fixed-size buffer, the formatted file path has a maximum
/// length of 31 characters.
static int openfd(const char* name, const unsigned int id) {
  char fp[32];
  snprintf(fp, sizeof(fp), "%s.%u.log", name, id);
  const int fno = open(fp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fno < 0) {
    log_error("cannot open %s file `%s`: %s", name, fp, strerror(errno));
    return -1;
  }
  return fno;
}

int fdinit(struct fdset_s* fds, const unsigned int id) {
  fds->out = fds->err = -1;
  if ((fds->out = openfd("stdout", id)) < 0) return -1;
  if ((fds->err = openfd("stderr", id)) < 0) {
    close(fds->out), fds->out = -1;
    return -1;
  }
  return 0;
}

void fdclose(struct fdset_s* fds) {
  if (fds->out >= 0) {
    close(fds->out);
    fds->out = -1;
  }
  if (fds->err >= 0) {
    close(fds->err);
    fds->err = -1;
  }
}
