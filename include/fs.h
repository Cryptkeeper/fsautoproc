/// @file fs.h
/// @brief Filesystem walk and stat functions.
#ifndef FSAUTOPROC_FS_H
#define FSAUTOPROC_FS_H

#include <stdint.h>

/// @struct fsstat_s
/// @brief Stat structure for storing the last modified time and file size.
struct fsstat_s {
  uint64_t lmod;///< Last modified time in milliseconds since epoch
  uint64_t fsze;///< File size in bytes
};

/// @typedef fswalkfn_t
/// @brief Callback function used by `fswalk` to process files and directories.
/// @param fp The file or directory path to process
/// @param st File stat information
/// @param udata User data passed to the callback function
/// @return 0 if the walk should continue, otherwise a non-zero value to stop
typedef int (*fswalkfn_t)(const char* fp, const struct fsstat_s* st,
                          void* udata);

/// @brief Recursively walks the single directory described by \p dir and calls
/// \p filefn for each file found.
/// @param dir The directory to walk
/// @param filefn The function to call for each file found. The filepath is
/// passed as the first argument. If the function returns a non-zero value, the
/// walk is terminated and the same value is returned by `fswalk`.
/// @param udata User data to pass to \p filefn and \p dirfn
/// @return If successful, 0 is returned. An internal `fswalk` error will return
/// a value of -1 and `errno` is set. Otherwise the return value of the first
/// non-zero \p filefn or \p dirfn call is returned.
int fswalk(const char* dir, fswalkfn_t filefn, void* udata);

/// @brief Populates all fields of a given \p fsstat_s structure for the file
/// described by the filepath \p fp.
/// @param fp The filepath to fstat
/// @param s The `struct fsstat_s` to populate
/// @return If successful, \p s is populated and 0 is returned. Otherwise -1 is
/// returned and `errno` is set.
int fsstat(const char* fp, struct fsstat_s* s);

/// @brief Concatenates the two strings into a single path string, joined by a
/// `/` (path separator) character. Neither input string is modified or freed.
/// @param dir The directory path
/// @param file The file path
/// @return A dynamically allocated string containing the joined path (i.e.
/// `dir/file`). The caller is responsible for freeing the string. NULL is
/// returned if an error occurred.
char* fsjoin(const char* dir, const char* file);

#endif// FSAUTOPROC_FS_H
