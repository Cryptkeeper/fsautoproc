/// @file je.c
/// @brief jemalloc-related utility function implementations.
#include "je.h"

#include "jemalloc/jemalloc.h"

#include <stddef.h>
#include <string.h>

char* je_strdup(const char* s) {
  if (s == NULL) return NULL;
  size_t len = strlen(s);
  char* dup = je_malloc(len + 1);
  if (dup == NULL) return NULL;
  memcpy(dup, s, len);
  dup[len] = '\0';
  return dup;
}
