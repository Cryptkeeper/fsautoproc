#ifndef FSAUTOPROC_TP_H
#define FSAUTOPROC_TP_H

#include "index.h"
#include "lcmd.h"

struct tpreq_s {
  struct lcmdset_s** cs;
  const struct inode_s* node;
  int flags;
};

int tpinit(int size);

int tpqueue(const struct tpreq_s* req);

int tpwait(void);

#endif//FSAUTOPROC_TP_H
