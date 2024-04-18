#include "sl.h"

#include <stdlib.h>
#include <string.h>

int sladd(slist_t* p, const char* str) {
  slist_t sl = *p;
  size_t len = 0;
  for (; sl != NULL && sl[len] != NULL; len++)
    ;
  slist_t r;
  if ((r = realloc(sl, (len + 2) * sizeof(sl))) == NULL ||
      (r[len] = strdup(str)) == NULL)
    return -1;
  r[len + 1] = NULL, *p = r;
  return 0;
}

void slfree(slist_t sl) {
  for (size_t i = 0; sl != NULL && sl[i] != NULL; i++) free(sl[i]);
  free(sl);
}
