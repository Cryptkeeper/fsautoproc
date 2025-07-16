/// @file index.c
/// @brief File index mapping and serialization implementation.
#include "index.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "jemalloc/jemalloc.h"

#include "je.h"
#include "log.h"

/// @def INDEXMAXFP
/// @brief The maximum filepath length of a file in the index.
#define INDEXMAXFP 512

/// @brief Hashes the filepath string.
/// @note 64-bit FNV-1a implementation is used for hashing.
/// @param fp The filepath string to hash
/// @return The hashed value of the filepath.
static uint64_t indexhash(const char* fp) {
  uint64_t hash = 14695981039346656037u;// FNV offset basis
  while (*fp) {
    hash ^= (uint8_t) *fp;// XOR byte into hash
    hash *= 1099511628211;// FNV prime
    fp++;
  }
  return hash;
}

/// @def indexbucket
/// @brief Maps and casts the hash value to a bucket index via the last N bits
/// (where N is the number of buckets, `INDEXBUCKETS`).
#define indexbucket(hash) ((int) (hash & INDEXBUCKETSMASK))

struct inode_s* indexfind(const struct index_s* idx, const char* fp) {
  const uint64_t hash = indexhash(fp);
  struct inode_s* head = idx->buckets[indexbucket(hash)];
  while (head != NULL) {
    if (head->fphash == hash) {
      if (strcmp(head->fp, fp) == 0) return head;
    }
    head = head->next;
  }
  return NULL;
}

/// @brief Compares two file nodes for sorting in ascending order by filepath.
/// @param a The first file node to compare
/// @param b The second file node to compare
/// @return The result of the comparison.
/// @note This function is equivalent to `strcmp(a->fp, b->fp)`.
static int indexnodecmp(const void* a, const void* b) {
  const struct inode_s* na = *(const struct inode_s**) a;
  const struct inode_s* nb = *(const struct inode_s**) b;
  return strcmp(na->fp, nb->fp);
}

int indexwrite(struct index_s* idx, FILE* s) {
  struct inode_s** fl;
  if ((fl = indexlist(idx)) == NULL) return -1;
  qsort(fl, idx->size, sizeof(struct inode_s*), indexnodecmp);

  char lbuf[INDEXMAXFP]; /* line output format buffer */

  int err = 0;
  for (long i = 0; i < idx->size; i++) {
    struct inode_s* node = fl[i];
    const int n = snprintf(lbuf, sizeof(lbuf), "%s,%" PRIu64 ",%" PRIu64 "\n",
                           node->fp, node->st.lmod, node->st.fsze);
    if (fwrite(lbuf, n, 1, s) != 1) {
      err = -1;
      break;
    }
  }
  je_free(fl);
  return err;
}

int indexread(struct index_s* idx, FILE* s) {
  char fp[INDEXMAXFP] = {0}; /* fscanf filepath string buffer */
  struct fsstat_s st = {0};  /* fscanf file stat structure */
  while (fscanf(s, "%[^,],%" PRIu64 ",%" PRIu64 "\n", fp, &st.lmod, &st.fsze) ==
         3) {
    if (indexput(idx, fp, st) == NULL) return -1;
  }
  return 0;
}

/// @brief Prepends a new node to the linked list by overwriting the head.
/// @param idx The head of the linked list
/// @param tail The new node to prepend
/// @return The new head of the linked list or NULL if memory allocation fails.
static struct inode_s* indexprepend(struct inode_s* idx,
                                    const struct inode_s tail) {
  struct inode_s* node;
  if ((node = je_malloc(sizeof(tail))) == NULL) return NULL;
  memcpy(node, &tail, sizeof(tail));
  node->next = idx;
  return node;
}

struct inode_s* indexput(struct index_s* idx, const char* fp,
                         const struct fsstat_s st) {
  const uint64_t fphash = indexhash(fp);
  struct inode_s node = {NULL, fphash, st, NULL};
  if ((node.fp = je_strdup(fp)) == NULL) return NULL;
  struct inode_s* bucket = idx->buckets[indexbucket(fphash)];
  struct inode_s* head = indexprepend(bucket, node);
  if (head == NULL) {
    je_free(node.fp);
    return NULL;
  }
  idx->buckets[indexbucket(fphash)] = head;
  idx->size++;
  return head;
}

/// @brief Recursively frees a linked list of nodes starting from a given head.
/// @param idx The head of the linked list
static void indexfree_r(struct inode_s* idx) {
  struct inode_s *head, *prev;
  for (head = idx; head != NULL;) {
    je_free(head->fp);
    prev = head, head = head->next;
    je_free(prev); /* free previous node */
  }
}

void indexfree(struct index_s* idx) {
  for (int i = 0; i < INDEXBUCKETS; i++) indexfree_r(idx->buckets[i]);
}

struct inode_s** indexlist(const struct index_s* idx) {
  errno = 0;
  struct inode_s** fl;
  if ((fl = je_calloc(idx->size, sizeof(*fl))) == NULL) return NULL;
  long ni = 0;
  for (int i = 0; i < INDEXBUCKETS; i++) {
    for (struct inode_s* head = idx->buckets[i]; head != NULL;
         head = head->next) {
      // prevent linked-list data from exceeding the expected/alloc'd index size
      if (ni >= idx->size) {
        errno = ERANGE;
        log_error("indexlist: size error (limit %ld, at %ld)", idx->size, ni);
        je_free(fl);
        return NULL;
      }
      fl[ni++] = head;
    }
  }
  return fl;
}
