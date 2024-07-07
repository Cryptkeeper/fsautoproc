#ifndef FSAUTOPROC_INDEX_H
#define FSAUTOPROC_INDEX_H

#include <stdio.h>

#include "fs.h"

struct inode_s {
  char* fp;             /* filepath string (duplicated) */
  struct fsstat_s st;   /* file stat info */
  struct inode_s* next; /* next node in linked list */
};

#define INDEXBUCKETS 16

struct index_s {
  struct inode_s* buckets[INDEXBUCKETS];
  size_t size;
};

/// @brief Searches the index for a node with a matching filepath.
/// @param idx The index to search
/// @param fp The search value (filepath) to compare
/// @return If a match is found, its pointer is returned, otherwise NULL.
struct inode_s* indexfind(struct index_s* idx, const char* fp);

/// @brief Flattens the index map into a sorted array of nodes (by filepath).
/// The list is then written to the file stream and freed.
/// @param idx The index to flatten
/// @param s The file stream to write to
/// @return If successful, 0 is returned. Otherwise, -1 is returned and `errno`
/// is set.
int indexwrite(struct index_s* idx, FILE* s);

/// @brief Reads a file stream and deserializes the contents into a map of
/// individual file nodes.
/// @param idx The index to populate
/// @param s The file stream to read from
/// @return If successful, 0 is returned. Otherwise, -1 is returned and `errno`
/// is set.
int indexread(struct index_s* idx, FILE* s);

/// @brief Copies the node and inserts it into the index mapping.
/// @param idx The index to insert into
/// @param tail The new node to copy and insert
/// @return The pointer to the new node in the index map, otherwise NULL.
struct inode_s* indexput(struct index_s* idx, struct inode_s node);

/// @brief Frees all nodes in the index map.
/// @param idx The index to free
void indexfree(struct index_s* idx);

/// @brief Flattens the index map into an unsorted array of nodes.
/// The list is dynamically allocated and must be freed by the caller. Array
/// size is determined by the `size` field in the index struct.
/// @param idx The index to flatten
/// @return If successful, a pointer to an array of size `idx->size` is
/// returned. Otherwise, NULL.
struct inode_s** indexlist(const struct index_s* idx);

#endif// FSAUTOPROC_INDEX_H
