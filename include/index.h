/// @file index.h
/// @brief File index mapping and serialization functions.
#ifndef FSAUTOPROC_INDEX_H
#define FSAUTOPROC_INDEX_H

#include <stdint.h>
#include <stdio.h>

#include "fs.h"

/// @struct inode_s
/// @brief Individual file node in the index map.
struct inode_s {
  char* fp;            ///< File path (string duplicated)
  uint64_t fphash;     ///< File path hash value
  struct fsstat_s st;  ///< File stat info structure
  uint64_t xx;         ///< xxHash64 hash value
  struct inode_s* next;///< Next node in the index map
};

/// @struct ibucket_s
/// @brief Index bucket structure for storing linked list head/tail pointers.
struct ibucket_s {
  struct inode_s* head;///< Head of the linked list of nodes in the bucket
  struct inode_s* tail;///< Tail of the linked list of nodes in the bucket
};

/// @def INDEXBUCKETS
/// @brief The fixed number of buckets in the index map.
/// @note This is a magic number, but via testing, performance is healthy around
/// ~40 nodes per bucket, so this should roughly accommodate ~150k nodes.
#define INDEXBUCKETS 4096

/// @def INDEXBUCKETSMASK
/// @brief The bitmask used for deriving the bucket index from the hash value.
/// @note This should correspond to the number of buckets defined in
/// `INDEXBUCKETS` (0xFFF, 0b1111_1111_1111, 2^12-1 = 4095).
#define INDEXBUCKETSMASK 0xFFF///< Maximum bucket index mask

/// @struct index_s
/// @brief Index map structure for storing file nodes.
struct index_s {
  struct ibucket_s buckets[INDEXBUCKETS];///< Array of index buckets
  long size;                             ///< Number of sum nodes in the index
};

/// @brief Hashes the filepath string for use in the index map.
/// @note 64-bit FNV-1a implementation is used for hashing.
/// @param fp The filepath string to hash
/// @return The hashed value of the filepath.
uint64_t indexhash(const char* fp);

/// @brief Searches the index for a node with a matching filepath.
/// @param idx The index to search
/// @param fp The search value (filepath) to compare
/// @param fphash The hash value of the filepath to compare
/// @return If a match is found, its pointer is returned, otherwise NULL.
struct inode_s* indexfind(const struct index_s* idx, const char* fp,
                          uint64_t fphash);

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
/// @param fp The file path to use for the new node, duplicated internally
/// @param fphash The file path hash value to use for the new node
/// @param st The file stat info to use for the new node
/// @param xx The xxHash64 hash value to use for the new node
/// @return The pointer to the new node in the index map, otherwise NULL is
/// returned and `errno` is set.
struct inode_s* indexput(struct index_s* idx, const char* fp, uint64_t fphash,
                         const struct fsstat_s* st, uint64_t xx);

/// @brief Frees all nodes in the index map.
/// @param idx The index to free
void indexfree(struct index_s* idx);

/// @brief Flattens the index map into an unsorted array of nodes.
/// The list is dynamically allocated and must be freed by the caller. Array
/// size is determined by the `size` field in the index struct.
/// @param idx The index to flatten
/// @return If successful, a pointer to an array of size `idx->size` is
/// returned. Otherwise, NULL is returned and `errno` is set.
struct inode_s** indexlist(const struct index_s* idx);

#endif// FSAUTOPROC_INDEX_H
