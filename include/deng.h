/// @file deng.h
/// @brief Differential file search comparison function and hook structure.
#ifndef FSAUTOPROC_DENG_H
#define FSAUTOPROC_DENG_H

#include <stdbool.h>

struct inode_s;
struct index_s;

/// @enum deng_notif_t
/// @brief Notification events for the file search process
enum deng_notif_t {
  DENG_NOTIF_DIR_DONE,   ///< Occurs when a directory has been fully processed
  DENG_NOTIF_STAGE_DONE, ///< Occurs when a stage has been fully processed
};

/// @struct deng_hooks_s
/// @brief Hook functions for file system search events
struct deng_hooks_s {
  void (*notify)(enum deng_notif_t notif); ///< Progress notification event
  void (*new)(struct inode_s* in);         ///< New file event
  void (*del)(struct inode_s* in);         ///< Deleted file event
  void (*mod)(struct inode_s* in);         ///< Modified file event
  void (*nop)(struct inode_s* in);         ///< Unmodified file event
};

/// @typedef deng_filter_t
/// @brief Filter function for ignoring files during the search process
/// @param fp The file path to filter
/// @return true if the file should be ignored, otherwise false
typedef bool (*deng_filter_t)(const char* fp);

/// @brief Recursively scans directory \p sd and compares the file system state
/// with a previously saved index. Any new, modified, deleted, or unmodified
/// files are reported to the caller via the provided hooks structure, \p hooks.
/// The index state \p new is then updated with the current file system state.
/// @param sd The directory to scan for conditionally ignoring files
/// @param filter The file filter function
/// @param hooks The file event hook functions
/// @param old The previous index state
/// @param new The current index state
/// @return 0 if successful, otherwise a non-zero error code.
int dengsearch(const char* sd, deng_filter_t filter,
               const struct deng_hooks_s* hooks, const struct index_s* old,
               struct index_s* new);

#endif//FSAUTOPROC_DENG_H
