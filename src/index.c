#include "index.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define INDEXMAXFP 512

struct inode_s* indexfind(struct inode_s* idx, const char* fp) {
  struct inode_s* head = idx;
  while (head != NULL) {
    if (strcmp(head->fp, fp) == 0) return head;
    head = head->next;
  }
  return NULL;
}

int indexwrite(struct inode_s* idx, FILE* s) {
  char lbuf[INDEXMAXFP]; /* line output format buffer */

  struct inode_s* head;
  for (head = idx; head != NULL; head = head->next) {
    const int n = snprintf(lbuf, sizeof(lbuf), "%s,%" PRIu64 ",%" PRIu64 "\n",
                           head->fp, head->st.lmod, head->st.fsze);
    if (fwrite(lbuf, n, 1, s) != 1) return -1;
  }

  return 0;
}

int indexread(struct inode_s** idx, FILE* s) {
  char fp[INDEXMAXFP] = {0};          /* fscanf filepath string buffer  */
  struct inode_s b = {fp, {0}, NULL}; /* fscanf node buffer */

  while (fscanf(s, "%[^,],%" PRIu64 ",%" PRIu64 "\n", b.fp, &b.st.lmod,
                &b.st.fsze) == 3) {
    // duplicate the string onto the heap
    if ((b.fp = strdup(b.fp)) == NULL) return -1;
    struct inode_s* r;
    if ((r = indexprepend(*idx, b)) == NULL) {
      free(*idx), *idx = NULL;
      return -1;
    }
    *idx = r;
    b.fp = fp;
  }

  return 0;
}

struct inode_s* indexprepend(struct inode_s* idx, const struct inode_s tail) {
  struct inode_s* node;
  if ((node = malloc(sizeof(tail))) == NULL) return NULL;
  memcpy(node, &tail, sizeof(tail));
  node->next = idx;
  return node;
}

void indexfree_r(struct inode_s* idx) {
  struct inode_s *head, *prev;
  for (head = idx; head != NULL;) {
    free(head->fp);
    prev = head, head = head->next;
    free(prev); /* free previous node */
  }
}
