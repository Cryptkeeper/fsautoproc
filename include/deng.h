/// @file deng.h
/// @brief Differential file search comparison function and hook structure.
#ifndef FSAUTOPROC_DENG_H
#define FSAUTOPROC_DENG_H

#include <signal.h>
#include <stdbool.h>

struct inode_s;
struct index_s;

/// @enum deng_notif_t
/// @brief Notification events for the file search process
enum deng_notif_t {
  DENG_NOTIF_FILE_FOUND,///< Occurs when a file will be processed
  DENG_NOTIF_STAGE_DONE,///< Occurs when a stage has been fully processed
};

/// @enum deng_fevent_t
/// @brief File change event types for the file comparison process
enum deng_fevent_t {
  DENG_FEVENT_NEW,///< New file event
  DENG_FEVENT_DEL,///< Deleted file event
  DENG_FEVENT_MOD,///< Modified file event
};

/// @struct deng_hooks_s
/// @brief Hook functions for file system search events
struct deng_hooks_s {
  void (*notify)(enum deng_notif_t notif);///< Progress notification event
  void (*event)(enum deng_fevent_t event, struct inode_s* in);///< File event
};

/// @typedef deng_filter_t
/// @brief Filter function for ignoring files during the search process
/// @param fp The file path to filter
/// @return true if the file should be ignored, otherwise false
typedef bool (*deng_filter_t)(const char* fp);

/// @struct deng_params_s
/// @brief Parameters for the differential file search process
struct deng_params_s {
  const char* sd;                  ///< Search directory root
  deng_filter_t filter;            ///< File filter function
  const struct deng_hooks_s* hooks;///< File event hook functions
  const struct index_s* old;       ///< Previous index state
  struct index_s* new;             ///< Current index state
};

/// @brief Recursively scans directory \p sd and compares the file system state
/// with a previously saved index. Any new, modified, deleted, or unmodified
/// files are reported to the caller via the provided hooks structure, \p hooks.
/// The index state \p new is then updated with the current file system state.
/// @param p The differential search parameters
/// @param stop An atomic integer which can be set to a non-zero value to
/// interrupt the search process (can be NULL to ignore)
/// @return 0 if successful, otherwise a non-zero error code.
int dengsearch(const struct deng_params_s* p,
               const volatile sig_atomic_t* stop);

#endif//FSAUTOPROC_DENG_H
