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

struct lcmdfile_s {
  struct lcmdset_s* head;
  size_t len;
};

void lcmdfilefree(struct lcmdfile_s* cmdfile);

int lcmdfileparse(const char* fp, struct lcmdfile_s* cmdfile);

int lcmdfileexec(const struct lcmdfile_s* cmdfile, const struct inode_s* node,
                 int onflags);

#endif//FSAUTOPROC_LCMD_H
