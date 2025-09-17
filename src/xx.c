/// @file xx.c
/// @brief xxHash64 file hashing function implementation.
#include "xx.h"

#include <assert.h>
#include <stdio.h>

#include "xxHash/xxhash.h"

#include "fs.h"

/// @brief Computes the xxHash64 hash of the data read from the given file
/// using a buffer for reading. The file is read in chunks until EOF.
/// @param b Buffer for reading file data. Must not be NULL.
/// @param f FILE object to read data from. Must not be NULL. The file should be
/// opened in binary mode and positioned at the start of the data.
/// @return Hash value of the file data, or 0 if an error occurred.
static uint64_t xxhash(uint8_t* b, FILE* f) {
  assert(b != NULL);
  assert(f != NULL);
  XXH3_state_t* state = XXH3_createState();
  if (!state) return 0;
  if (XXH3_64bits_reset(state)) goto doerr;
  size_t n;
  while ((n = fread(b, 1, XXBUFSZE, f)) > 0)
    if (XXH3_64bits_update(state, b, n)) goto doerr;
  uint64_t h = XXH3_64bits_digest(state);
  XXH3_freeState(state);
  return h;
doerr:
  XXH3_freeState(state);
  return 0;
}

/// @brief Computes the xxHash64 hash of the file located at the given filepath.
/// The file is opened, read in chunks until EOF, and then closed.
/// @param b Buffer for reading file data. Must not be NULL.
/// @param fp The filepath of the file to hash. Must not be NULL.
/// @return The hash value of the file, or 0 if an error occurred (e.g. file
/// could not be opened).
static uint64_t xxhashfp(uint8_t* b, const char* fp) {
  assert(b != NULL);
  assert(fp != NULL);
  FILE* f = fopen(fp, "rb");
  if (!f) return 0;
  const uint64_t h = xxhash(b, f);
  fclose(f);
  return h;
}

uint64_t xxupdate(const struct xxreq_s* req) {
  assert(req != NULL);
  const struct fsstat_s* pst = req->pst;
  if (pst != NULL && pst->lmod == req->cst->lmod && pst->fsze == req->cst->fsze)
    return req->pxx;// no change
  return xxhashfp(req->b, req->fp);
}
