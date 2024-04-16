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

  struct inode_s* head = idx;
  while (head != NULL) {
    const int n = snprintf(lbuf, sizeof(lbuf), "%s,%" PRIu64 ",%" PRIu64 "\n",
                           head->fp, head->st.lmod, head->st.fsze);
    if (fwrite(lbuf, n, 1, s) != 1) return -1;
    head = head->next;
  }

  return 0;
}

int indexread(struct inode_s** idx, FILE* s) {
  char fp0[INDEXMAXFP] = {0}; /* fscanf filepath string buffer  */

  // temp buffer used for decoding, backed by stack arrays, copied when prepended
  struct inode_s tail = {fp0, 0};

  while (fscanf(s, "%[^,],%" PRIu64 ",%" PRIu64 "\n", tail.fp, &tail.st.lmod,
                &tail.st.fsze) == 3) {
    // duplicate the string onto the heap
    if ((tail.fp = strdup(tail.fp)) == NULL) return -1;
    *idx = indexprepend(*idx, tail);
    // reset the buffer pointers
    tail.fp = fp0;
  }

  return 0;
}

struct inode_s* indexprepend(struct inode_s* idx, const struct inode_s tail) {
  // duplicate tail into a heap allocation
  struct inode_s* node = malloc(sizeof(tail));
  if (node == NULL) return idx; /* return last valid pointer */
  memcpy(node, &tail, sizeof(tail));

  // prepend to the start of the list
  node->next = idx;
  return node;
}

void indexfree_r(struct inode_s* idx) {
  struct inode_s *head, *prev;
  for (head = idx; head != NULL;) {
    free(head->fp);
    prev = head;
    head = head->next; /* advance to next node */
    free(prev);        /* free previous node */
  }
}
