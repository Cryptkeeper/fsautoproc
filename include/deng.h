#ifndef FSAUTOPROC_DENG_H
#define FSAUTOPROC_DENG_H

#include <stdbool.h>

struct inode_s;
struct index_s;

enum deng_notif_t {
  DENG_NOTIF_DIR_DONE,   /* a directory has been processed */
  DENG_NOTIF_STAGE_DONE, /* a stage has been completed */
};

struct deng_hooks_s {
  bool (*filter_junk)(const char* fp);     /* filter junk files       */
  void (*notify)(enum deng_notif_t notif); /* processing flow events  */
  void (*new)(struct inode_s* in);         /* new file event          */
  void (*del)(struct inode_s* in);         /* deleted file event      */
  void (*mod)(struct inode_s* in);         /* modified file event     */
  void (*nop)(struct inode_s* in);         /* unmodified file event   */
};

/// @brief Recursively scans a directory tree and compares the file system state
/// with a previously saved index. Any new, modified, deleted, or unmodified
/// files are reported to the caller via the provided hooks. The index state is
/// updated with the current file system state.
/// @param sd The directory to scan
/// @param hooks The file event hook functions
/// @param old The previous index state
/// @param new The current index state
/// @return 0 if successful, otherwise a non-zero error code.
int dengsearch(const char* sd, const struct deng_hooks_s* hooks,
               const struct index_s* old, struct index_s* new);

#endif//FSAUTOPROC_DENG_H
