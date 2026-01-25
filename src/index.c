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

static char indexfpbuf[1024];///< Filepath buffer for index I/O operations

/// @def indexbucket
/// @brief Maps and casts the hash value to a bucket index via the last N bits
/// (where N is the number of buckets, `INDEXBUCKETS`).
#define indexbucket(hash) ((int) (hash & INDEXBUCKETSMASK))

uint64_t indexhash(const char* fp) {
  uint64_t hash = 14695981039346656037u;// FNV offset basis
  while (*fp) {
    hash ^= (uint8_t) *fp;// XOR byte into hash
    hash *= 1099511628211;// FNV prime
    fp++;
  }
  return hash;
}

struct inode_s* indexfind(const struct index_s* idx, const char* fp,
                          const uint64_t fphash) {
  struct inode_s* head = idx->buckets[indexbucket(fphash)].head;
  while (head != NULL) {
    if (head->fphash == fphash) {
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

/// @def INDEXWRITEFMT
/// @brief Format string used for writing index entries to a file stream.
#define INDEXWRITEFMT "%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n"

/// @brief Function pointer type for writing a buffer to a stream.
/// @param ctx The stream context (FILE* or gzFile)
/// @param buf The buffer to write
/// @param len The length of the buffer
/// @return 0 on success, -1 on error.
typedef int (*indexwriter_fn)(void* ctx, const char* buf, int len);

static int indexwriter_file(void* ctx, const char* buf, const int len) {
  return fwrite(buf, len, 1, (FILE*) ctx) == 1 ? 0 : -1;
}

static int indexwriter_gz(void* ctx, const char* buf, const int len) {
  return gzwrite((gzFile) ctx, buf, len) > 0 ? 0 : -1;
}

/// @brief Flattens the index map into a sorted array of nodes (by filepath).
/// The list is then written to the stream context and freed.
/// @param idx The index to flatten
/// @param ctx The stream context (FILE* or gzFile)
/// @param wfn The writer function to use for output
/// @return If successful, 0 is returned. Otherwise, -1 is returned.
static int indexwrite_impl(struct index_s* idx, void* ctx, indexwriter_fn wfn) {
  if (idx->size == 0) return 0;
  struct inode_s** fl;
  if ((fl = indexlist(idx)) == NULL) return -1;
  qsort(fl, idx->size, sizeof(struct inode_s*), indexnodecmp);

  int err = 0;
  for (long i = 0; i < idx->size; i++) {
    struct inode_s* node = fl[i];
    const int n = snprintf(indexfpbuf, sizeof(indexfpbuf), INDEXWRITEFMT,
                           node->fp, node->st.lmod, node->st.fsze, node->xx);
    if (wfn(ctx, indexfpbuf, n)) {
      err = -1;
      break;
    }
  }
  je_free(fl);
  return err;
}

int indexwrite(struct index_s* idx, FILE* s) {
  return indexwrite_impl(idx, s, indexwriter_file);
}

int indexwrite_gz(struct index_s* idx, gzFile gz) {
  return indexwrite_impl(idx, gz, indexwriter_gz);
}

int indexread(struct index_s* idx, gzFile gz) {
  struct fsstat_s st = {0};
  uint64_t xx = 0;
  while (gzgets(gz, indexfpbuf, sizeof(indexfpbuf)) != NULL) {
    // find the first comma to separate filepath from the rest
    char* comma = strchr(indexfpbuf, ',');
    if (comma == NULL) continue;
    *comma = '\0';// null-terminate the filepath
    if (sscanf(comma + 1, "%" PRIu64 ",%" PRIu64 ",%" PRIu64, &st.lmod,
               &st.fsze, &xx) != 3)
      continue;
    const uint64_t fphash = indexhash(indexfpbuf);
    if (indexput(idx, indexfpbuf, fphash, &st, xx) == NULL) return -1;
  }
  return 0;
}

/// @brief Appends the node to the end of the bucket (linked list), potentially
/// assigning a new head if the bucket is empty.
/// @param head The head of the linked list
/// @param node The node pointer to insert
static void indexappend(struct ibucket_s* bucket, struct inode_s* node) {
  node->next = NULL;// ensure value is initialized
  if (bucket->tail != NULL) {
    bucket->tail->next = node;
    bucket->tail = node;
  } else {
    bucket->head = bucket->tail = node;
  }
}

struct inode_s* indexput(struct index_s* idx, const char* fp,
                         const uint64_t fphash, const struct fsstat_s* st,
                         const uint64_t xx) {
  struct inode_s* node = je_malloc(sizeof(struct inode_s));
  if (node == NULL) return NULL;
  if ((fp = je_strdup(fp)) == NULL) {// duplicate filepath string
    je_free(node);
    return NULL;
  }
  *node = (struct inode_s) {(char*) fp, fphash, *st, xx, NULL};
  struct ibucket_s* bucket = &idx->buckets[indexbucket(node->fphash)];
  indexappend(bucket, node);
  idx->size++;
  return node;
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
  for (int i = 0; i < INDEXBUCKETS; i++) indexfree_r(idx->buckets[i].head);
}

struct inode_s** indexlist(const struct index_s* idx) {
  if (idx->size == 0) return NULL;
  struct inode_s** fl;
  if ((fl = je_calloc(idx->size, sizeof(*fl))) == NULL) return NULL;
  long ni = 0;
  for (int i = 0; i < INDEXBUCKETS; i++) {
    for (struct inode_s* head = idx->buckets[i].head; head != NULL;
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
