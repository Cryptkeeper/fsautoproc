#ifndef FSAUTOPROC_DQ_H
#define FSAUTOPROC_DQ_H

/// @brief `dqnext()` returns the next directory in the queue and increments its
/// internal index. If the end of the queue is reached, NULL is returned on the
/// next call. `dqnext()` can be safely called prior to any `dqpush()` calls.
/// @return The next directory in the queue, otherwise NULL
char* dqnext(void);

/// @brief `dqreset()` resets the queue index and frees all memory allocated by
/// `dqpush()`. This function should be called when the queue is no longer needed.
/// This function is safe to call multiple times, even if the queue is empty.
/// @note This function is equivalent to `dqfree()`.
void dqreset(void);

#define dqfree dqreset

/// @brief `dqpush()` adds a directory to the queue. The directory is duplicated
/// and stored in the queue. The queue is dynamically resized as needed.
/// @param dir The directory to add to the queue
/// @return If successful, 0 is returned. Otherwise, -1 is returned and `errno`
/// is set.
int dqpush(const char* dir);

#endif//FSAUTOPROC_DQ_H
