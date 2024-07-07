#ifndef FSAUTOPROC_FS_H
#define FSAUTOPROC_FS_H

#include <stdbool.h>
#include <stdint.h>

typedef int (*fswalkfn_t)(const char* fp);

/// @brief `fswalk` walks the single directory described by `dir` and calls
/// `filefn` for each file found and `dirfn` for each directory found.
/// @param dir The directory to walk
/// @param filefn The function to call for each file found. The filepath is
/// passed as the first argument. If the function returns a non-zero value, the
/// walk is terminated and the same value is returned by `fswalk`.
/// @param dirfn The function to call for each directory found. The directory
/// path is passed as the first argument. If the function returns a non-zero
/// value, the walk is terminated and the same value is returned by `fswalk`.
/// @return If successful, 0 is returned. An internal `fswalk` error will return
/// a value of -1 and `errno` is set. Otherwise the return value of the first
/// non-zero `filefn` or `dirfn` call is returned.
int fswalk(const char* dir, fswalkfn_t filefn, fswalkfn_t dirfn);

struct fsstat_s {
  uint64_t lmod;
  uint64_t fsze;
};

/// @brief `fsstateql()` compares the fields of two `struct fsstat_s` values for
/// equality. Two structs with the same values are considered equal.
/// @param a The first `struct fsstat_s` to compare
/// @param b The second `struct fsstat_s` to compare
/// @return Returns true if the two `struct fsstat_s` values are equal.
bool fsstateql(const struct fsstat_s* a, const struct fsstat_s* b);

/// @brief `fsstat()` populates all fields of a given `struct fsstat_s` for the
/// file described by the filepath `fp`.
/// @param fp The filepath to fstat
/// @param s The `struct fsstat_s` to populate
/// @return If successful, `s` is populated and 0 is returned. Otherwise -1 is
/// returned and `errno` is set.
int fsstat(const char* fp, struct fsstat_s* s);

#endif// FSAUTOPROC_FS_H
