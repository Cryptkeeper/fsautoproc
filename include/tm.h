/// @file tm.h
/// @brief Time utility functions.
#ifndef FSAUTOPROC_TIME_H
#define FSAUTOPROC_TIME_H

#include <stdint.h>

/// @brief Get the current system time in milliseconds.
/// @return current time in milliseconds
uint64_t tmnow(void);

/// @enum sleepdur_t
/// @brief Predefined sleep durations in milliseconds for various polling cases.
enum sleepdur_t {
  DUR_QUEUE = 100,///< Poll rate when queueing new work to the thread pool
  DUR_WAIT = 250, ///< Poll rate when waiting for all thread pool work to finish
  DUR_HALT = 500, ///< Poll rate when waiting for thread pool shutdown
  DUR_IDLE_SHORT = 25,///< Poll rate when a single worker is waiting for work
  DUR_IDLE_LONG = 100,///< Poll rate when multiple worker threads are idle
};

/// @brief Sleep for the specified number of milliseconds.
/// @note This function will block the calling thread. The sleep duration may be
/// longer than the specified time due to system scheduling. The function may
/// fail internally via the underlying system call. This will be indicated by a
/// log message, but the function will not return an error code.
/// @param ms The number of milliseconds to sleep. Must be greater than 0.
void tmsleep(uint32_t ms);

#endif//FSAUTOPROC_TIME_H
