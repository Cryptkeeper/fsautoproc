/// @file lcmd.h
/// @brief File-specific system command execution mapping functions.
#ifndef FSAUTOPROC_LCMD_H
#define FSAUTOPROC_LCMD_H

#include <regex.h>
#include <stdbool.h>
#include <stdint.h>

#include "sl.h"

struct inode_s;
struct fdset_s;

/// @def LCTRIG_NEW
/// @brief Trigger bit flag for new file events
#define LCTRIG_NEW (1 << 0)

/// @def LCTRIG_MOD
/// @brief Trigger bit flag for modified file events
#define LCTRIG_MOD (1 << 1)

/// @def LCTRIG_DEL
/// @brief Trigger bit flag for deleted file events
#define LCTRIG_DEL (1 << 2)

/// @def LCTRIG_NOP
/// @brief Trigger bit flag for no operation/unmodified file events
#define LCTRIG_NOP (1 << 3)

/// @def LCTRIG_ALL
/// @brief Trigger bit flag for all file events
#define LCTRIG_ALL (LCTRIG_NEW | LCTRIG_MOD | LCTRIG_DEL | LCTRIG_NOP)

/// @def LCTOPT_TRACE
/// @brief Option bit flag for tracing command set matches by printing to stdout
#define LCTOPT_TRACE (1 << 7)

/// @def LCTOPT_VERBOSE
/// @brief Option bit flag for printing commands to stdout before execution
#define LCTOPT_VERBOSE (1 << 8)

/// @struct lcmdset_s
/// @brief A set of system commands to execute when a file event of a specific
/// type and file path is triggered.
struct lcmdset_s {
  int onflags;         ///< Command set trigger bit flags
  regex_t** fpatterns; ///< Compiled regex patterns used for file path matching
  slist_t* syscmds;    ///< Commands to pass to `system(3)`
  char* name;          ///< Command set name or description for logging
  uint64_t msspent;    ///< Sum milliseconds spent executing commands
};

/// @brief Iterates and frees all memory allocated by the command set array.
/// @param cs The command set array to free
void lcmdfree_r(struct lcmdset_s** cs);

/// @brief Prses the provided file path and populates an array of command sets.
/// The file must be a valid JSON file containing an array of objects. Each
/// object must contain the following keys:
/// - `on`: An array of trigger flags to match
/// - `patterns`: An array of file patterns to match
/// - `commands`: An array of system commands to execute
/// The `on` array must contain one or more of the following strings:
/// - `new`: Trigger on new files
/// - `mod`: Trigger on modified files
/// - `del`: Trigger on deleted files
/// - `nop`: Trigger on no operation
/// The `patterns` array must contain one or more strings that are used to match
/// the file path. The `commands` array must contain one or more strings that are
/// passed to `system(3)` for execution.
/// @param fp The file path to parse
/// @return An array of command sets if successful, otherwise NULL.
struct lcmdset_s** lcmdparse(const char* fp);

/// @brief Checks if the provided file path matches any of the file patterns in
/// the command set.
/// @param cs The command set array to filter
/// @param fp The file path to match
/// @return true if the file path matches any file pattern, otherwise false
bool lcmdmatchany(struct lcmdset_s** cs, const char* fp);

/// @brief Sequentially iterates the command set and executes the configured
/// system commands on the provided file node if the trigger flags and file
/// patterns match.
/// @param cs The command set array to filter and execute
/// @param node The file node to execute on
/// @param fds The file descriptor set to use for stdout/stderr redirection
/// @param flags The trigger flags to match, see `LCTRIG_*`. If `LCTOPT_VERBOSE`
/// is set, the commands will be printed to stdout before execution. If
/// `LCTOPT_TRACE` is set, the true/false match result for each command set will
/// be printed to stdout.
/// @return 0 if successful, otherwise the first non-zero return value from
/// `system(3)` is returned.
int lcmdexec(struct lcmdset_s** cs, const struct inode_s* node,
             const struct fdset_s* fds, int flags);

#endif//FSAUTOPROC_LCMD_H
