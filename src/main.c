#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <unistd.h>

#include "logc/src/log.h"

#include "dq.h"
#include "fs.h"
#include "index.h"
#include "lcmd.h"
#include "tp.h"

#include "loglock.h"

static struct {
  char* configfile;
  char* indexfile;
  char* searchdir;
  int threads;
} initargs;

static void freeinitargs(void) {
  free(initargs.configfile);
  free(initargs.indexfile);
  free(initargs.searchdir);
}

static struct lcmdset_s** cmdsets;

static void freecmdsets(void) { lcmdfree_r(cmdsets); }

static struct inode_s* lastmap; /* stored index from previous run (if any) */
static struct inode_s* thismap; /* live checked index from this run */

static void freeindexmaps(void) {
  indexfree_r(lastmap);
  indexfree_r(thismap);
}

static void freeglobals(void) {
  dqfree();
  tpfree();
}

static void freeall(void) {
  // wait for all work to complete
  int err;
  if ((err = tpwait())) log_error("error waiting for threads: %d", err);

  freeinitargs();
  freecmdsets();
  freeindexmaps();
  freeglobals();
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
               "  -s <dir>    Search directory root (default: `.`)\n"
               "  -t <#>      Number of worker threads (default: 4)\n",
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
      case 't':
        initargs.threads = (int) strtol(optarg, NULL, 10);
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

  // set default argument values
  if (initargs.configfile == NULL)
    if ((initargs.configfile = strdup("fsautoproc.json")) == NULL) return 1;

  if (initargs.searchdir == NULL)
    if ((initargs.searchdir = strdup(".")) == NULL) return 1;

  if (initargs.indexfile == NULL) {
    // default to using index.dat inside search directory
    char fp[256];
    snprintf(fp, sizeof(fp), "%s/index.dat", initargs.searchdir);
    if ((initargs.indexfile = strdup(fp)) == NULL) return 1;
  }

  if (initargs.threads == 0) initargs.threads = 4;

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
    struct inode_s* r;
    if ((r = indexprepend(thismap, finfo)) == NULL) return -1;
    thismap = r;
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

    const struct tpreq_s req = {cmdsets, curr,
                                modified ? LCTRIG_MOD : LCTRIG_NOP};
    int err;
    if ((err = tpqueue(&req))) return err;

    if (!modified) return 0;
  } else {
    log_debug("file does not exist in previous index `%s`", curr->fp);

    const struct tpreq_s req = {cmdsets, curr, LCTRIG_NEW};
    int err;
    if ((err = tpqueue(&req))) return err;
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

  struct inode_s* r;
  if ((r = indexprepend(thismap, finfo)) == NULL) return -1;
  thismap = r;
  curr = indexfind(thismap, fp);
  assert(curr != NULL); /* should+must exist in the list */

  log_debug("file `%s` is new (post-command exec)", curr->fp);

  const struct tpreq_s req = {cmdsets, curr, LCTRIG_NEW};
  int err;
  if ((err = tpqueue(&req))) return err;

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
      log_debug("file `%s` has been removed", head->fp);

      const struct tpreq_s req = {cmdsets, head, LCTRIG_DEL};
      int err;
      if ((err = tpqueue(&req)))
        log_error("error queuing deletion command for `%s`: %d", head->fp, err);
    }
    head = head->next;
  }
}

static int cmpchanges(void) {
  if (loadlastmap(initargs.indexfile)) {
    // attempt to load the file, but continue if it doesn't exist
    if (errno != ENOENT) {
      log_fatal("cannot read index `%s`: %s", initargs.indexfile,
                strerror(errno));
      return -1;
    }
  }

  // walk the scan directory in two passes:
  //  - fsprocfile_pre, which will compare the file stats with the previous index
  //    (if any) and fire all types of file events (NEW, MOD, DEL, NOP)
  //  - fsprocfile_post, which will only fire NEW events for files that were created
  //    as a result of the previous commands to ensure all files are indexed
  for (int pass = 0; pass <= 1; pass++) {
    log_info("performing pass %d", pass);

    dqreset();
    if (dqpush(initargs.searchdir)) {
      perror(NULL);
      return -1;
    }

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

    // wait for all work to complete
    int err;
    if ((err = tpwait())) {
      log_error("error waiting for threads: %d", err);
      return -1;
    }

    if (pass == 0) {
      checkremoved();// test for files removed in new index

      // wait for all work to complete
      if ((err = tpwait())) {
        log_error("error waiting for threads: %d", err);
        return -1;
      }
    }
  }

  if (savethismap(initargs.indexfile)) {
    log_fatal("cannot write index `%s`: %s", initargs.indexfile,
              strerror(errno));
    return -1;
  }

  return 0;
}

int main(int argc, char** argv) {
  atexit(freeall);
  if (parseinitargs(argc, argv)) return 1;

  log_set_lock(loglockfn, NULL);

  // init worker thread pool
  int err;
  if ((err = tpinit(initargs.threads))) {
    log_fatal("error initializing thread pool: %d", err);
    return 1;
  }

  // load configuration file
  if ((cmdsets = lcmdparse(initargs.configfile)) == NULL) {
    log_fatal("error loading configuration file `%s`", initargs.configfile);
    return 1;
  }

  if ((err = cmpchanges())) return err;

  return 0;
}
