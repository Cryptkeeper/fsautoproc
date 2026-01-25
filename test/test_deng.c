#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "zlib.h"

#include "deng.h"
#include "index.h"
#include "log.h"

struct evcounts_s {
  int new;
  int del;
  int mod;
};

static struct evcounts_s evcounts; /* recycled global used for hook callbacks */

static void onevent(const enum deng_fevent_t type, struct inode_s* in) {
  assert(in != NULL);
  switch (type) {
    case DENG_FEVENT_NEW:
      evcounts.new ++;
      break;
    case DENG_FEVENT_DEL:
      evcounts.del++;
      break;
    case DENG_FEVENT_MOD:
      evcounts.mod++;
      break;
    default:
      assert("unknown event type %d");
  }
}

struct scantest_s {
  const char* sd;                   /* initial search directory */
  _Bool hasindex;                   /* has `index.dat` file in directory */
  const struct evcounts_s expected; /* expected event counts to compare */
};

#define SCANTESTCOUNT 4

static const struct scantest_s scantests[SCANTESTCOUNT] = {
        /* scan of a directory with no previous index */
        {"../test/new-files-test", false, {3, 0, 0}},
        /* scan of a directory with an outdated index */
        {"../test/modified-files-test", true, {1, 0, 3}},
        /* scan of a directory with removed files */
        {"../test/deleted-files-test", true, {1, 3, 0}},
        /* scan of a directory with new/modified files */
        {"../test/mixed-files-test", true, {4, 3, 0}},
};

int main(void) {
  const struct deng_hooks_s hooks = {NULL, onevent};

  for (int i = 0; i < SCANTESTCOUNT; i++) {
    const struct scantest_s* test = &scantests[i];
    log_verbose("running test %d against `%s`", i, test->sd);

    struct index_s old = {0};
    struct index_s new = {0};

    if (test->hasindex) {
      char fp[256];
      snprintf(fp, sizeof(fp), "%s/index.dat", test->sd);
      gzFile gz = gzopen(fp, "rb");
      assert(gz != NULL);
      assert(indexread(&old, gz) == 0);
      gzclose(gz);
      log_verbose("using fixed index `%s`", fp);
    }

    const struct deng_params_s p = {test->sd, NULL, &hooks, &old, &new};
    assert(dengsearch(&p, NULL) == 0);

    log_verbose("%d new files (expected %d)", evcounts.new, test->expected.new);
    log_verbose("%d del files (expected %d)", evcounts.del, test->expected.del);
    log_verbose("%d mod files (expected %d)", evcounts.mod, test->expected.mod);

    assert(test->expected.new == evcounts.new);
    assert(test->expected.del == evcounts.del);
    assert(test->expected.mod == evcounts.mod);

    memset(&evcounts, 0, sizeof(evcounts));

    indexfree(&old);
    indexfree(&new);
  }

  return 0;
}
