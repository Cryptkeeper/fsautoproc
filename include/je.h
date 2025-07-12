/// @file je.h
/// @brief jemalloc-related utility functions.
#ifndef FSAUTOPROC_JE_H
#define FSAUTOPROC_JE_H

/**
 * Duplicates the given string \p s into a new dynamically allocated string
 * using jemalloc's `malloc` function.
 * @param s The string to duplicate
 * @return A pointer to the newly allocated string, or NULL if the allocation
 * fails.
 */
char* je_strdup(const char* s);

#endif//FSAUTOPROC_JE_H
