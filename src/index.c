#include "index.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#define INDEXMAXFP 512

static int indexhash(const char* fp) {
  int h = 0;
  for (const char* p = fp; *p != '\0'; p++) h = (h << 5) - h + *p;
  if (h < 0) h = -h;
  return h % INDEXBUCKETS;
}

struct inode_s* indexfind(struct index_s* idx, const char* fp) {
  struct inode_s* head = idx->buckets[indexhash(fp)];
  while (head != NULL) {
    if (strcmp(head->fp, fp) == 0) return head;
    head = head->next;
  }
  return NULL;
}

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
  for (size_t i = 0; i < idx->size; i++) {
    struct inode_s* node = fl[i];
    const int n = snprintf(lbuf, sizeof(lbuf), "%s,%" PRIu64 ",%" PRIu64 "\n",
                           node->fp, node->st.lmod, node->st.fsze);
    if (fwrite(lbuf, n, 1, s) != 1) {
      err = -1;
      break;
    }
  }
  free(fl);
  return err;
}

int indexread(struct index_s* idx, FILE* s) {
  char fp[INDEXMAXFP] = {0};          /* fscanf filepath string buffer */
  struct inode_s b = {fp, {0}, NULL}; /* fscanf node buffer */

  while (fscanf(s, "%[^,],%" PRIu64 ",%" PRIu64 "\n", b.fp, &b.st.lmod,
                &b.st.fsze) == 3) {
    // duplicate the string onto the heap
    if ((b.fp = strdup(b.fp)) == NULL) return -1;
    if (indexput(idx, b) == NULL) return -1;
    b.fp = fp;
  }

  return 0;
}

static struct inode_s* indexprepend(struct inode_s* idx,
                                    const struct inode_s tail) {
  struct inode_s* node;
  if ((node = malloc(sizeof(tail))) == NULL) return NULL;
  memcpy(node, &tail, sizeof(tail));
  node->next = idx;
  return node;
}

struct inode_s* indexput(struct index_s* idx, const struct inode_s node) {
  struct inode_s* bucket = idx->buckets[indexhash(node.fp)];
  struct inode_s* head = indexprepend(bucket, node);
  if (head == NULL) return NULL;
  idx->buckets[indexhash(node.fp)] = head;
  idx->size++;
  return head;
}

static void indexfree_r(struct inode_s* idx) {
  struct inode_s *head, *prev;
  for (head = idx; head != NULL;) {
    free(head->fp);
    prev = head, head = head->next;
    free(prev); /* free previous node */
  }
}

void indexfree(struct index_s* idx) {
  for (int i = 0; i < INDEXBUCKETS; i++) indexfree_r(idx->buckets[i]);
}

struct inode_s** indexlist(const struct index_s* idx) {
  struct inode_s** fl;
  if ((fl = calloc(idx->size, sizeof(struct inode_s*))) == NULL) return NULL;
  size_t ni = 0;
  for (int i = 0; i < INDEXBUCKETS; i++) {
    for (struct inode_s* head = idx->buckets[i]; head != NULL;
         head = head->next) {
      // prevent linked-list data from exceeding the expected/alloc'd index size
      if (ni < idx->size) {
        fl[ni++] = head;
      } else {
        log_error("indexlist: size error (limit %zu, at %zu)", idx->size, ni);
        free(fl);
        return NULL;
      }
    }
  }
  return fl;
}
