#ifndef FSAUTOPROC_LCMD_H
#define FSAUTOPROC_LCMD_H

#include "sl.h"

struct inode_s;

#define LCTRIG_NEW (1 << 0)
#define LCTRIG_MOD (1 << 1)
#define LCTRIG_DEL (1 << 2)
#define LCTRIG_NOP (1 << 3)

struct lcmdset_s {
  int onflags;       /* command set trigger names */
  slist_t fpatterns; /* file patterns used for matching */
  slist_t syscmds;   /* commands to pass to `system(3) */
};

/// @brief `lcmdfree()` iterates and frees all memory allocated by `lcmdparse()`.
/// @param cs The command set array to free
void lcmdfree_r(struct lcmdset_s** cs);

/// @brief `lcmdparse()` parses the provided file path and populates an array of
/// command sets. The file must be a valid JSON file containing an array of
/// objects. Each object must contain the following keys:
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

/// @brief `lcmdexec()` sequentially iterates the command set and executes the
/// system commands on the provided file node if the trigger flags and file
/// patterns match.
/// @param cs The command set array to filter and execute
/// @param node The file node to execute on
/// @param flags The trigger flags to match
/// @return 0 if successful, otherwise the first non-zero return value from
/// `system(3)` is returned.
int lcmdexec(struct lcmdset_s** cs, const struct inode_s* node, int flags);

#endif//FSAUTOPROC_LCMD_H
