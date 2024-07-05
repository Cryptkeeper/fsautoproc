#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dq.h"
#include "fs.h"
#include "index.h"
#include "lcmd.h"
#include "log.h"
#include "tp.h"

static struct {
  char* configfile;
  char* indexfile;
  char* searchdir;
  char* tracefile;
  _Bool includejunk;
  _Bool skipproc;
  int threads;
  _Bool verbose;
} initargs;

static void freeinitargs(void) {
  free(initargs.configfile);
  free(initargs.tracefile);
  free(initargs.indexfile);
  free(initargs.searchdir);
}

static struct lcmdset_s** cmdsets;

static void freecmdsets(void) { lcmdfree_r(cmdsets); }

static struct index_s lastmap; /* stored index from previous run (if any) */
static struct index_s thismap; /* live checked index from this run */

static void freeindexmaps(void) {
  indexfree(&lastmap);
  indexfree(&thismap);
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
  while ((c = getopt(argc, argv, ":hc:i:js:t:r:uv")) != -1) {
    switch (c) {
      case 'h':
        printf("Usage: %s -i <file>\n"
               "\n"
               "Options:\n"
               "  -c <file>   Configuration file (default: `fsautoproc.json`)\n"
               "  -i <file>   File index write path\n"
               "  -j          Include ignored files in index (default: false)\n"
               "  -s <dir>    Search directory root (default: `.`)\n"
               "  -t <#>      Number of worker threads (default: 4)\n"
               "  -r <file>   Trace which command sets match the file\n"
               "  -u          Skip processing files, only update file index\n"
               "  -v          Enable verbose output\n",
               argv[0]);
        exit(0);
      case 'c':
        strdupoptarg(initargs.configfile);
        break;
      case 'i':
        strdupoptarg(initargs.indexfile);
        break;
      case 'j':
        initargs.includejunk = true;
        break;
      case 's':
        strdupoptarg(initargs.searchdir);
        break;
      case 't':
        initargs.threads = (int) strtol(optarg, NULL, 10);
        break;
      case 'r':
        strdupoptarg(initargs.tracefile);
        break;
      case 'u':
        initargs.skipproc = true;
        break;
      case 'v':
        initargs.verbose = true;
        break;
      case ':':
        log_error("option is missing argument: %c", optopt);
        return 1;
      case '?':
      default:
        log_error("unknown option: %c", optopt);
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
  assert(lastmap.size == 0);
  FILE* s = fopen(fp, "r");
  if (s == NULL) return -1;
  const int err = indexread(&lastmap, s);
  fclose(s);
  return err;
}

static int savethismap(const char* fp) {
  FILE* s = fopen(fp, "w");
  if (s == NULL) return -1;
  const int err = indexwrite(&thismap, s);
  fclose(s);
  return err;
}

#define abssub(a, b) ((a) >= (b) ? (a) - (b) : (b) - (a))

static int fsprocfile_pre(const char* fp) {
  // only process files that match at least one command set
  if (!initargs.includejunk && !lcmdmatchany(cmdsets, fp)) {
    if (initargs.verbose) log_verbose("[j] %s", fp);
    return 0;
  }

  struct inode_s finfo = {0};
  if ((finfo.fp = strdup(fp)) == NULL) return -1;
  if (fsstat(fp, &finfo.st)) return -1;

  // attempt to match file in previous index
  struct inode_s* prev = indexfind(&lastmap, fp);

  // lookup from previous iteration or insert new record and lookup
  struct inode_s* curr = indexfind(&thismap, fp);
  if (curr == NULL) {
    if ((curr = indexput(&thismap, finfo)) == NULL) return -1;
  }

  if (prev != NULL) {
    const bool modified = !fsstateql(&prev->st, &curr->st);
    if (modified) {
      const uint64_t dt = abssub(prev->st.fsze, curr->st.fsze);
      log_info("[*] %s (Δ%" PRIu64 ")", curr->fp, dt);
    } else {
      if (initargs.verbose) log_verbose("[n] %s", curr->fp);
    }

    if (!initargs.skipproc) {
      int flags = modified ? LCTRIG_MOD : LCTRIG_NOP;
      if (initargs.verbose) flags |= LCTOPT_VERBOSE;
      const struct tpreq_s req = {cmdsets, curr, flags};
      int err;
      if ((err = tpqueue(&req))) return err;

      if (!modified) return 0;
    }
  } else {
    log_info("[+] %s", curr->fp);

    if (!initargs.skipproc) {
      const int flags = LCTRIG_NEW | (initargs.verbose ? LCTOPT_VERBOSE : 0);
      const struct tpreq_s req = {cmdsets, curr, flags};
      int err;
      if ((err = tpqueue(&req))) return err;
    }
  }

  return 0;
}

static int fsprocfile_post(const char* fp) {
  // only process files that match at least one command set
  if (!initargs.includejunk && !lcmdmatchany(cmdsets, fp)) {
    if (initargs.verbose) log_info("[j] %s", fp);
    return 0;
  }

  struct inode_s* curr = indexfind(&thismap, fp);
  if (curr != NULL) {
    // check if the file was modified during the command execution
    struct fsstat_s mod = {0};
    if (fsstat(fp, &mod)) return -1;
    if (fsstateql(&curr->st, &mod)) return 0;

    if (initargs.verbose) {
      const uint64_t dt = abssub(curr->st.fsze, mod.fsze);
      log_info("[*] %s (Δ%" PRIu64 ")", curr->fp, dt);
    }

    // update the file info in the current index
    curr->st = mod;

    return 0;
  }

  struct inode_s finfo = {0};
  if ((finfo.fp = strdup(fp)) == NULL) return -1;
  if (fsstat(fp, &finfo.st)) return -1;

  if ((curr = indexput(&thismap, finfo)) == NULL) return -1;

  log_info("[+] %s", curr->fp);

  if (!initargs.skipproc) {
    const int flags = LCTRIG_NEW | (initargs.verbose ? LCTOPT_VERBOSE : 0);
    const struct tpreq_s req = {cmdsets, curr, flags};
    int err;
    if ((err = tpqueue(&req))) return err;
  }

  return 0;
}

static int waitforwork(void) {
  // wait for all work to complete
  int err;
  if ((err = tpwait())) log_error("error waiting for threads: %d", err);
  return err;
}

static int checkremoved(void) {
  if (lastmap.size == 0) return 0;// no previous map entries to check

  struct inode_s** lastlist;
  if ((lastlist = indexlist(&lastmap)) == NULL) return -1;

  for (size_t i = 0; i < lastmap.size; i++) {
    struct inode_s* prev = lastlist[i];
    if (indexfind(&thismap, prev->fp) == NULL) { /* file no longer exists */
      log_info("[-] %s", prev->fp);

      if (initargs.skipproc) continue;
      const int flags = LCTRIG_DEL | (initargs.verbose ? LCTOPT_VERBOSE : 0);
      const struct tpreq_s req = {cmdsets, prev, flags};
      int err;
      if ((err = tpqueue(&req))) {
        log_error("error queuing deletion command for `%s`: %d", prev->fp, err);
      }
    }
  }

  free(lastlist);

  return waitforwork();
}

static int execstage(fswalkfn_t filefn) {
  dqreset();
  if (dqpush(initargs.searchdir)) {
    perror(NULL);
    return -1;
  }

  char* dir;
  while ((dir = dqnext()) != NULL) {
    if (initargs.verbose) log_verbose("[s] %s", dir);

    int err;
    if ((err = fswalk(dir, filefn, dqpush))) {
      log_error("file func for `%s` returned %d", dir, err);
      return -1;
    }
  }

  return waitforwork();
}

static int cmpchanges(void) {
  if (loadlastmap(initargs.indexfile)) {
    // attempt to load the file, but continue if it doesn't exist
    if (errno != ENOENT) {
      log_error("cannot read index `%s`: %s", initargs.indexfile,
                strerror(errno));
      return -1;
    }
  }

  // walk the scan directory in two passes:
  //  - fsprocfile_pre, which will compare the file stats with the previous index
  //    (if any) and fire all types of file events (NEW, MOD, DEL, NOP)
  //  - fsprocfile_post, which will only fire NEW events for files that were created
  //    as a result of the previous commands to ensure all files are indexed
  int err;
  if ((err = execstage(fsprocfile_pre)) || (err = checkremoved()) ||
      (err = execstage(fsprocfile_post)))
    return err;

  log_info("compared %zu files", thismap.size);

  if (savethismap(initargs.indexfile)) {
    log_error("cannot write index `%s`: %s", initargs.indexfile,
              strerror(errno));
    return -1;
  }

  return 0;
}

static int tracefile(const char* fp) {
  struct inode_s node = {.fp = (char*) fp};
  if (fsstat(fp, &node.st)) return -1;
  return lcmdexec(cmdsets, &node, LCTOPT_TRACE | LCTRIG_ALL);
}

int main(int argc, char** argv) {
  atexit(freeall);
  if (parseinitargs(argc, argv)) return 1;

  // init worker thread pool
  int err;
  if ((err = tpinit(initargs.threads))) {
    log_error("error initializing thread pool: %d", err);
    return 1;
  }

  // load configuration file
  if ((cmdsets = lcmdparse(initargs.configfile)) == NULL) {
    log_error("error loading configuration file `%s`", initargs.configfile);
    return 1;
  }

  if (initargs.tracefile != NULL) {
    // prints which command sets match the file and exits
    return tracefile(initargs.tracefile);
  } else if ((err = cmpchanges())) {
    return err;
  }

  return 0;
}
