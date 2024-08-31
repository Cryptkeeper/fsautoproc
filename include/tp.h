/// @file tp.h
/// @brief Thread pool API for executing work requests.
#ifndef FSAUTOPROC_TP_H
#define FSAUTOPROC_TP_H

/// @struct tpreq_s
/// @brief Pending work request which contains a command set to execute on a
/// thread in the pool, using a file node as the target.
struct tpreq_s {
  struct lcmdset_s** cs; ///< command set to execute
  struct inode_s* node;  ///< File node to pass to the command set
  int flags;             ///< Trigger flags for the command set
};

/// @def TPOPT_LOGFILES
/// @brief Option bit flag for logging all stdout/stderr output to files.
#define TPOPT_LOGFILES 1

/// @brief Initializes a global worker thread pool of the given size.
/// @param size The number of threads to create, must be greater than 0.
/// @param flags The flags to use when creating the thread pool.
/// @return 0 on success, -1 on failure.
int tpinit(int size, int flags);

/// @brief Allocates a work request to the first available thread in the global
/// pool. If no threads are available, the request will block until a thread
/// becomes available.
/// @param req The work request to allocate.
/// @return 0 on success, -1 on failure.
int tpqueue(const struct tpreq_s* req);

/// @brief Waits for all threads in the global pool to finish executing their
/// work requests.
void tpwait(void);

/// @brief Waits for all threads in the global pool to finish executing their
/// work requests, and then shuts down the pool and exits its threads. This
/// function should be followed by a call to `tpfree()`.
void tpshutdown(void);

/// @brief Frees all memory allocated by `tpinit()`. This function should be
/// called at the end of the program. It is safe to call this function even if
/// `tpinit()` was not called. This function should be called directly after
/// `tpshutdown()`.
void tpfree(void);

#endif//FSAUTOPROC_TP_H
