#ifndef EASYLIB_INDEX_H
#define EASYLIB_INDEX_H

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

/// @brief Iterates and writes the linked node list to a file stream for future
/// deserializing using `indexread`.
/// @param idx The head node of the linked list
/// @param s The file stream to write to
/// @return If successful, 0 is returned. Otherwise, -1 is returned and `errno`
/// is set.
int indexwrite(struct inode_s* idx, FILE* s);

/// @brief Reads a file stream and deserializes the contents into a linked node list.
/// Invalid lines are skipped without warning.
/// @param idx Return value of the the head node of the linked list
/// @param s The file stream to read from
/// @return If successful, 0 is returned. Otherwise, -1 is returned and `errno`
/// is set.
int indexread(struct inode_s** idx, FILE* s);

/// @brief Dynamically allocates a new node and prepends it to the linked list.
/// @param idx The previous head node of the linked list
/// @param tail The new node to copy and prepend
/// @return The new head node of the linked list, or NULL if an error occurred.
struct inode_s* indexprepend(struct inode_s* idx, struct inode_s tail);

/// @brief Iterates and frees the memory allocated by the linked node list.
/// @param idx The head node of the linked list
void indexfree_r(struct inode_s* idx);

#endif
