#ifndef FSAUTOPROC_LCMD_H
#define FSAUTOPROC_LCMD_H

#include "index.h"

#define LCTRIG_NEW (1 << 0)
#define LCTRIG_MOD (1 << 1)
#define LCTRIG_DEL (1 << 2)
#define LCTRIG_NOP (1 << 3)

struct lcmdset_s {
  int onflags;      /* command set trigger names */
  char** fpatterns; /* file patterns used for matching */
  char** syscmds;   /* commands to pass to `system(3) */
};

void lcmdfree_r(struct lcmdset_s** cs);

struct lcmdset_s** lcmdparse(const char* fp);

int lcmdexec(struct lcmdset_s** cs, const struct inode_s* node, int flags);

#endif//FSAUTOPROC_LCMD_H
