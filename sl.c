#include "sl.h"

#include <stdlib.h>
#include <string.h>

char** sladd(char** sl, const char* str) {
  size_t len = 0;
  for (; sl != NULL && sl[len] != NULL; len++)
    ;
  char** r = NULL;
  if ((r = realloc(sl, (len + 2) * sizeof(*sl))) == NULL) return NULL;
  sl = r;
  if ((sl[len] = strdup(str)) == NULL) {
    free(sl);
    return NULL;
  }
  sl[len + 1] = NULL;
  return r;
}

void slfree(char** sl) {
  for (size_t i = 0; sl != NULL && sl[i] != NULL; i++) free(sl[i]);
  free(sl);
}