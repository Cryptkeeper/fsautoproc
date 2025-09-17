/// @file xx.h
/// @brief xxHash64 file hashing functions.
#ifndef FSAUTOPROC_XX_H
#define FSAUTOPROC_XX_H

#include <stddef.h>
#include <stdint.h>

/// @def XXBUFSZE
/// @brief Size of the buffer used for reading files to hash.
#define XXBUFSZE 65536

/// @struct xxreq_s
/// @brief File hash request structure for processing file hash changes.
struct xxreq_s {
  uint8_t* b;                ///< Buffer for reading file data
  const char* fp;            ///< Filepath to hash
  const struct fsstat_s* pst;///< Previous file stat info, may be NULL
  const struct fsstat_s* cst;///< Current file stat info
  uint64_t pxx;              ///< Previous xxHash64 value, or 0 if unknown
  const char* cause;         ///< Optional cause string for logging, may be NULL
};

/// @brief Processes a file hash request. If the file's last modified time or
/// size differ from the previous stat info \p pst, the file is re-hashed
/// and the current hash value is returned. Otherwise the previous \p pxx hash
/// value is returned indicating no change.
/// @param req The file hash request structure containing the filepath, previous
/// and current stat info, and previous hash value (if known).
/// @return 0 if an error occurred, otherwise the current hash value.
uint64_t xxupdate(const struct xxreq_s* req);

#endif//FSAUTOPROC_XX_H
