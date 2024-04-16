#include "sl.h"

#include <stdlib.h>
#include <string.h>

char** sl_add(char** sl, const char* str) {
  size_t len = 0;
  if (sl != NULL) {
    for (len = 0; sl[len] != NULL; len++)
      ;
  }
  char** new = NULL;
  if ((new = realloc(sl, (len + 2) * sizeof(*sl))) == NULL) return NULL;
  sl = new;
  if ((sl[len] = strdup(str)) == NULL) {
    free(sl);
    return NULL;
  }
  sl[len + 1] = NULL;
  return new;
}

void sl_free(char** sl) {
  if (sl == NULL) return;
  for (size_t i = 0; sl[i] != NULL; i++) free(sl[i]);
  free(sl);
}