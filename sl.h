#ifndef FSAUTOPROC_SL_H
#define FSAUTOPROC_SL_H

/// @brief `sladd()` duplicates a string and appends it to a string list.
/// The caller is responsible for freeing the list using `slfree`.
/// @param sl The string list to append to, or NULL to create a new list.
/// @param str The string to duplicate and append.
/// @return NULL if an error occurred, otherwise a pointer to the enlarged string list.
char** sladd(char** sl, const char* str);

/// @brief `slfree()` frees a string list and all its elements.
void slfree(char** sl);

#endif//FSAUTOPROC_SL_H
