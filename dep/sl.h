// sl.h - single-header string list library
// https://github.com/Cryptkeeper/sl
// MIT licensed
// define SL_IMPL before including this file in a source file
#ifndef SL_SINGLEFILE_H
#define SL_SINGLEFILE_H
#define SL_IMPL_NO_INCLUDE
#line 1 "sl.h"
#ifndef SL_H
#define SL_H

/// @struct slist_s
/// @brief A resizable string list structure wrapping an array of strings.
struct slist_s {
  char **strings; //< Array of strings, array itself is NULL terminated
  long len;       //< Current number of strings in the list
  long cap;       //< Total capacity of the strings list
};

typedef struct slist_s slist_t;

/// @brief 'sladd' duplicates a string and appends it to the end of the string
/// list. The list may be resized if necessary to accommodate the new string.
/// 'sladd' will not automatically shrink the list capacity.
/// @param sl The string list to append to, must not be NULL. A zero initialized
/// list is treated as an empty list.
/// @param str The string to duplicate and append, must not be NULL.
/// @return 0 if successful and 'sl' is set to the new string list, otherwise -1
/// is returned, 'errno' is set, and the original list is left unchanged.
int sladd(slist_t *sl, const char *str);

/// @brief 'slfree' frees any/all strings in the string list. The list may be
/// reused safely after this call.
/// @param sl The string list to free, may be NULL.
void slfree(slist_t *sl);

/// @brief 'slpop' removes and returns the last element from the string list.
/// This does not resize the array. The "removed" element is instead set to NULL
/// and its memory can be reclaimed by the next call to 'sladd'. The caller is
/// responsible for freeing the returned string using 'free'.
/// @param sl The string list to pop from
/// @return The last element removed from the list. If the list is NULL or
/// empty, NULL is returned.
char *slpop(slist_t *sl);

/// @brief 'slcount' returns the number of elements in the string list.
/// @param sl The string list to count elements in.
/// @return The number of elements in the list. If the list is NULL or empty, 0
/// is returned. The return value is always non-negative.
long slcount(const slist_t *sl);

#endif // SL_H

#endif // SL_SINGLEFILE_H
#ifdef SL_IMPL
#ifndef SL_IMPL_ONCE
#define SL_IMPL_ONCE
#line 1 "sl.c"
#ifndef SL_IMPL_NO_INCLUDE
#include "sl.h"
#endif

#ifndef SL_OVERRIDE
#include <stdlib.h> /* for free, realloc, NULL */
#include <string.h> /* for strdup */

#define SLX_REALLOC realloc
#define SLX_FREE free
#define SLX_STRDUP strdup
#endif

int sladd(slist_t *sl, const char *str) {
  if (sl->len + 1 >= sl->cap) {
    const long newcap = sl->cap == 0 ? 4 : (long)((double)sl->cap * 1.5);
    char **r;
    if ((r = SLX_REALLOC(sl->strings, (newcap + 1) * sizeof(char *))) == NULL)
      return -1;
    sl->strings = r;
    sl->cap = newcap;
  }
  if ((sl->strings[sl->len] = SLX_STRDUP(str)) == NULL)
    return -1;
  sl->strings[++sl->len] = NULL;
  return 0;
}

void slfree(slist_t *sl) {
  if (sl == NULL)
    return;
  long i;
  for (i = 0; sl->strings != NULL && sl->strings[i] != NULL; i++)
    SLX_FREE(sl->strings[i]);
  SLX_FREE(sl->strings);
  sl->strings = NULL;
  sl->len = sl->cap = 0;
}

char *slpop(slist_t *sl) {
  if (sl == NULL || sl->len == 0)
    return NULL;
  sl->len--;
  char *const str = sl->strings[sl->len];
  sl->strings[sl->len] = NULL;
  return str;
}

long slcount(const slist_t *sl) {
  return sl != NULL ? sl->len : 0;
}

#endif // SL_IMPL_ONCE
#endif // SL_IMPL
