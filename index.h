#ifndef EASYLIB_INDEX_H
#define EASYLIB_INDEX_H

#include <stdint.h>
#include <stdio.h>

#include "fs.h"

struct inode_s {
  char* fp;             /* filepath string (duplicated) */
  struct fsstat_s st;   /* file stat info */
  struct inode_s* next; /* next node in linked list */
};

/// @brief Searches the linked node list for an entry with a matching `fp` value.
/// @param idx The head node of the linked list
/// @param fp The search value (filepath)
/// @return If a match is found, its allocation pointer is returned, otherwise NULL.
struct inode_s* indexfind(struct inode_s* idx, const char* fp);

int indexwrite(struct inode_s* idx, FILE* s);

int indexread(struct inode_s** idx, FILE* s);

struct inode_s* indexprepend(struct inode_s* idx, struct inode_s tail);

void indexfree_r(struct inode_s* idx);

#endif
