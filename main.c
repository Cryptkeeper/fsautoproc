#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#include "dq.h"
#include "fs.h"
#include "index.h"
#include "lcmd.h"

struct args_s {
  char* configfile;
  char* indexfile;
  char* searchdir;
};

static struct args_s initargs;

static struct lcmdset_s** cmdsets;

static struct inode_s* lastmap; /* stored index from previous run (if any) */
static struct inode_s* thismap; /* live checked index from this run */

static void freeopts(void) {
  free(initargs.configfile);
  free(initargs.indexfile);
  free(initargs.searchdir);

  lcmdfree_r(cmdsets);

  indexfree_r(lastmap);
  indexfree_r(thismap);

  dqfree();
}

#define strdupoptarg(into)                                                     \
  if ((into = strdup(optarg)) == NULL) {                                       \
    perror(NULL);                                                              \
    return 1;                                                                  \
  }

static int parseinitargs(const int argc, char** const argv) {
  int c;
  while ((c = getopt(argc, argv, ":hc:i:s:")) != -1) {
    switch (c) {
      case 'h':
        printf("Usage: %s\n"
               "\n"
               "Options:\n"
               "  -c <file>   Configuration file (default: `fsautoproc.json`)\n"
               "  -i <file>   File index write path\n"
               "  -s <dir>    Search directory root (default: `.`)\n",
               argv[0]);
        exit(0);
      case 'c':
        strdupoptarg(initargs.configfile);
        break;
      case 'i':
        strdupoptarg(initargs.indexfile);
        break;
      case 's':
        strdupoptarg(initargs.searchdir);
        break;
      case ':':
        log_fatal("option is missing argument: %c", optopt);
        return 1;
      case '?':
      default:
        log_fatal("unknown option: %c", optopt);
        return 1;
    }
  }
  return 0;
}

static int loadlastmap(const char* fp) {
  assert(lastmap == NULL);
  FILE* s = fopen(fp, "r");
  if (s == NULL) return -1;
  const int err = indexread(&lastmap, s);
  fclose(s);
  return err;
}

static int savethismap(const char* fp) {
  FILE* s = fopen(fp, "w");
  if (s == NULL) return -1;
  int err = 0;
  if (thismap != NULL) err = indexwrite(thismap, s);
  fclose(s);
  return err;
}

static int fsprocfile_pre(const char* fp) {
  log_info("processing file `%s`", fp);

  struct inode_s finfo = {0};
  if ((finfo.fp = strdup(fp)) == NULL) return -1;
  if (fsstat(fp, &finfo.st)) return -1;

  // attempt to match file in previous index
  struct inode_s* prev = indexfind(lastmap, fp);

  // lookup from previous iteration or insert new record and lookup
  struct inode_s* curr = indexfind(thismap, fp);
  if (curr == NULL) {
    thismap = indexprepend(thismap, finfo);
    curr = indexfind(thismap, fp);
    assert(curr != NULL); /* should+must exist in the list */
  }

  if (prev != NULL) {
    const bool modified = fsstateql(&prev->st, &curr->st);
    if (modified) {
      log_trace("file `%s` has been modified (last modified: %zu -> %zu, file "
                "size: %zu -> %zu)",
                curr->fp, prev->st.lmod, curr->st.lmod, prev->st.fsze,
                curr->st.fsze);
    } else {
      log_trace("file `%s` has not been modified", curr->fp);
    }

    int err;
    if ((err = lcmdexec(cmdsets, curr, modified ? LCTRIG_MOD : LCTRIG_NOP)))
      return err;

    if (!modified) return 0;
  } else {
    log_debug("file does not exist in previous index `%s`", curr->fp);
    int err;
    if ((err = lcmdexec(cmdsets, curr, LCTRIG_NEW))) return err;
  }

  // update the file info in the current index
  // this is done after the command execution to capture any file modifications
  if (fsstat(fp, &finfo.st)) return -1;

  return 0;
}

static int fsprocfile_post(const char* fp) {
  struct inode_s* curr = indexfind(thismap, fp);
  if (curr != NULL) return 0;

  struct inode_s finfo = {0};
  if ((finfo.fp = strdup(fp)) == NULL) return -1;
  if (fsstat(fp, &finfo.st)) return -1;

  thismap = indexprepend(thismap, finfo);
  curr = indexfind(thismap, fp);
  assert(curr != NULL); /* should+must exist in the list */

  log_debug("file `%s` is new (post-command exec)", curr->fp);
  int err;
  if ((err = lcmdexec(cmdsets, curr, LCTRIG_NEW))) return err;

  // update the file info in the current index
  // this is done after the command execution to capture any file modifications
  if (fsstat(fp, &finfo.st)) return -1;

  return 0;
}

static void checkremoved(void) {
  if (lastmap == NULL) return;// no previous map entries to check

  struct inode_s* head = lastmap;
  while (head != NULL) {
    if (indexfind(thismap, head->fp) == NULL) { /* file no longer exists */
      int err;
      if ((err = lcmdexec(cmdsets, head, LCTRIG_DEL)))
        log_warn("error invoking DEL cmds for `%s` (%d)", head->fp, err);
    }
    head = head->next;
  }
}

static int submain(const struct args_s* args) {
  if (loadlastmap(args->indexfile)) {
    // attempt to load the file, but continue if it doesn't exist
    if (errno != ENOENT) {
      log_fatal("cannot read index `%s`: %s", args->indexfile, strerror(errno));
      return -1;
    }
  }

  // walk the scan directory in two passes:
  //  - fsprocfile_pre, which will compare the file stats with the previous index
  //    (if any) and fire all types of file events (NEW, MOD, DEL, NOP)
  //  - fsprocfile_post, which will only fire NEW events for files that were created
  //    as a result of the previous commands to ensure all files are indexed
  for (int pass = 0; pass < 1; pass++) {
    log_info("performing pass %d", pass);

    dqreset();
    dqpush(args->searchdir);

    char* dir;
    while ((dir = dqnext()) != NULL) {
      log_debug("scanning `%s`", dir);

      int err;
      if ((err = fswalk(dir, pass == 0 ? fsprocfile_pre : fsprocfile_post,
                        dqpush))) {
        log_fatal("file func for `%s` returned %d", err);
        return -1;
      }
    }

    if (pass == 0) checkremoved();// test for files removed in new index
  }

  if (savethismap(args->indexfile)) {
    log_fatal("cannot write index `%s`: %s", args->indexfile, strerror(errno));
    return -1;
  }

  return 0;
}

int main(int argc, char** argv) {
  atexit(freeopts);
  if (parseinitargs(argc, argv)) return 1;

  // copy initial arguments and default initialize any missing values
  struct args_s defargs = initargs;

  if (defargs.searchdir == NULL) defargs.searchdir = ".";
  if (defargs.configfile == NULL) defargs.configfile = "fsautoproc.json";

  if (defargs.indexfile == NULL) {
    // default to using index.dat in search directory
    char fp[256];
    snprintf(fp, sizeof(fp), "%s/index.dat", defargs.searchdir);
    if ((defargs.indexfile = strdup(fp)) == NULL) {
      perror(NULL);
      return 1;
    }
  }

  // load configuration file
  if ((cmdsets = lcmdparse(defargs.configfile)) == NULL) {
    log_fatal("error loading configuration file `%s`", defargs.configfile);
    return 1;
  }

  int err;
  if ((err = submain(&defargs))) return err;

  return 0;
}
