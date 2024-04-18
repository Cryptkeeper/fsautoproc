#ifndef FSAUTOPROC_TP_H
#define FSAUTOPROC_TP_H

#include "index.h"
#include "lcmd.h"

struct tpreq_s {
  struct lcmdset_s** cs;      /* cmdset to execute */
  const struct inode_s* node; /* file node to execute on */
  int flags;                  /* command trigger flags */
};

/// @brief Initializes a global worker thread pool of the given size.
/// @param size The number of threads to create, must be greater than 0.
/// @return 0 on success, -1 on failure.
int tpinit(int size);

/// @brief Allocates a work request to the first available thread in the global
/// pool. If no threads are available, the request will block until a thread
/// becomes available.
/// @param req The work request to allocate.
/// @return 0 on success, -1 on failure.
int tpqueue(const struct tpreq_s* req);

/// @brief Waits for all threads in the global pool to finish executing their
/// work requests.
/// @return 0 on success, -1 on failure.
int tpwait(void);

#endif//FSAUTOPROC_TP_H
