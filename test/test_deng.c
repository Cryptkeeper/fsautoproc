#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "deng.h"
#include "index.h"
#include "log.h"

struct evcounts_s {
  int new;
  int del;
  int mod;
  int nop;
};

static struct evcounts_s evcounts; /* recycled global used for hook callbacks */

static void onnew(struct inode_s* in) {
  assert(in != NULL);
  evcounts.new ++;
}

static void ondel(struct inode_s* in) {
  assert(in != NULL);
  evcounts.del++;
}

static void onmod(struct inode_s* in) {
  assert(in != NULL);
  evcounts.mod++;
}

static void onnop(struct inode_s* in) {
  assert(in != NULL);
  evcounts.nop++;
}

struct scantest_s {
  const char* sd;                   /* initial search directory */
  _Bool hasindex;                   /* has `index.dat` file in directory */
  const struct evcounts_s expected; /* expected event counts to compare */
};

#define SCANTESTCOUNT 4

static const struct scantest_s scantests[SCANTESTCOUNT] = {
        /* scan of a directory with no previous index */
        {"../test/new-files-test", false, {3, 0, 0, 0}},
        /* scan of a directory with an outdated index */
        {"../test/modified-files-test", true, {1, 0, 3, 0}},
        /* scan of a directory with removed files */
        {"../test/deleted-files-test", true, {1, 3, 0, 0}},
        /* scan of a directory with new/modified files */
        {"../test/mixed-files-test", true, {4, 3, 0, 0}},
};

int main(void) {
  const struct deng_hooks_s hooks = {
          .new = onnew,
          .del = ondel,
          .mod = onmod,
          .nop = onnop,
  };

  for (int i = 0; i < SCANTESTCOUNT; i++) {
    const struct scantest_s* test = &scantests[i];
    log_verbose("running test %d against `%s`", i, test->sd);

    struct index_s old = {0};
    struct index_s new = {0};

    if (test->hasindex) {
      char fp[256];
      snprintf(fp, sizeof(fp), "%s/index.dat", test->sd);
      FILE* f = fopen(fp, "r");
      assert(f != NULL);
      assert(indexread(&old, f) == 0);
      fclose(f);
      log_verbose("using fixed index `%s`", fp);
    }

    assert(dengsearch(test->sd, &hooks, &old, &new) == 0);

    log_verbose("%d new files (expected %d)", evcounts.new, test->expected.new);
    log_verbose("%d del files (expected %d)", evcounts.del, test->expected.del);
    log_verbose("%d mod files (expected %d)", evcounts.mod, test->expected.mod);
    log_verbose("%d nop files (expected %d)", evcounts.nop, test->expected.nop);

    assert(test->expected.new == evcounts.new);
    assert(test->expected.del == evcounts.del);
    assert(test->expected.mod == evcounts.mod);
    assert(test->expected.nop == evcounts.nop);

    memset(&evcounts, 0, sizeof(evcounts));

    indexfree(&old);
    indexfree(&new);
  }

  return 0;
}
