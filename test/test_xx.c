#undef NDEBUG
#include <assert.h>

#include "fs.h"
#include "xx.h"

#define FAKEHASH 1

static uint8_t xxbuf[XXBUFSZE];

static struct xxreq_s xx = {
        .fp = "../test/xx-test/a.txt",
        .b = xxbuf,
        .pxx = FAKEHASH,
};

static struct fsstat_s est = {0};

static void test_unchanged_file_stats(void) {
  xx.pst = &est;
  xx.cst = &est;
  assert(xxupdate(&xx) == FAKEHASH);// no changes, ensure hash re-used
}

static void test_new_file_stats(void) {
  xx.pst = NULL;// no previous stat
  xx.cst = &est;
  assert(xxupdate(&xx) != FAKEHASH);// new file, ensure new hash
}

static void test_changed_file_size(void) {
  xx.pst = &est;
  struct fsstat_s cst = {0, 1};
  xx.cst = &cst;
  assert(xxupdate(&xx) != FAKEHASH);// changed, ensure new hash
}

static void test_changed_file_lmod(void) {
  xx.pst = &est;
  struct fsstat_s cst = {1, 0};
  xx.cst = &cst;
  assert(xxupdate(&xx) != FAKEHASH);// changed, ensure new hash
}

int main(void) {
  test_unchanged_file_stats();
  test_new_file_stats();
  test_changed_file_size();
  test_changed_file_lmod();
  return 0;
}
