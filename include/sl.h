#ifndef FSAUTOPROC_SL_H
#define FSAUTOPROC_SL_H

typedef char* slist_t;

/// @brief `sladd()` duplicates a string and appends it to a string list. The
/// string list is dynamically resized as needed and returned via the `sl` param.
/// Caller is responsible for freeing the list using `slfree`.
/// @param sl The string list to append to, and the return value of the resized list.
/// @param str The string to duplicate and append.
/// @return 0 if successful and `sl` is set to the new string list, otherwise -1
/// is returned and `errno` is set.
int sladd(slist_t** sl, const char* str);

/// @brief `slfree()` frees a string list and all its elements.
/// @param sl The string list to free.
void slfree(slist_t* sl);

/// @brief `slpop()` removes and returns the last element from the string list.
/// The caller is responsible for freeing the returned string.
/// @param sl The string list to pop from.
/// @return The last element removed from the list or NULL if the list is empty.
char* slpop(slist_t* sl);

#endif//FSAUTOPROC_SL_H
