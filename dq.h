#ifndef FSAUTOPROC_DQ_H
#define FSAUTOPROC_DQ_H

#include <stdbool.h>

char* dqnext(void);

void dqreset(void);

#define dqfree dqreset

void dqpush(const char* dir);

#endif//FSAUTOPROC_DQ_H
