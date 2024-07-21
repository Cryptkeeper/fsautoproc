#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "deng.h"
#include "fd.h"
#include "fs.h"
#include "index.h"
#include "lcmd.h"
#include "log.h"
#include "prog.h"
#include "tp.h"

static struct {
  char* configfile;
  char* indexfile;
  char* searchdir;
  char* tracefile;
  _Bool pipefiles;
  _Bool includejunk;
  _Bool listspent;
  _Bool skipproc;
  int threads;
  _Bool verbose;
} initargs;

/// @brief Frees all duplicated initialization arguments.
static void freeinitargs(void) {
  free(initargs.configfile);
  free(initargs.tracefile);
  free(initargs.indexfile);
  free(initargs.searchdir);
}

static struct lcmdset_s** cmdsets;

static struct index_s lastmap; /* stored index from previous run (if any) */
static struct index_s thismap; /* live checked index from this run */

/// @brief Frees all allocated resources.
static void freeall(void) {
  tpshutdown();
  freeinitargs();
  lcmdfree_r(cmdsets);
  indexfree(&lastmap);
  indexfree(&thismap);
  tpfree();
}

#define strdupoptarg(into)                                                     \
  do {                                                                         \
    if ((into = strdup(optarg)) == NULL) {                                     \
      perror(NULL);                                                            \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int parseinitargs(const int argc, char** const argv) {
  int c;
  while ((c = getopt(argc, argv, ":hc:i:jlps:t:r:uv")) != -1) {
    switch (c) {
      case 'h':
        printf("Usage: %s -i <file>\n"
               "\n"
               "Options:\n"
               "  -c <file>   Configuration file (default: `fsautoproc.json`)\n"
               "  -i <file>   File index write path\n"
               "  -j          Include ignored files in index (default: false)\n"
               "  -l          List time spent for each command set\n"
               "  -p          Pipe subprocess stdout/stderr to files "
               "(default: false)\n"
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
      case 'l':
        initargs.listspent = true;
        break;
      case 'p':
        initargs.pipefiles = true;
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

/// @brief Loads the index from the specified file path into the provided index.
/// @param idx The index to load into
/// @param fp The file path to load the index from
/// @return 0 if successful, otherwise a non-zero error code.
static int loadindex(struct index_s* idx, const char* fp) {
  assert(idx != NULL);
  FILE* s = fopen(fp, "r");
  if (s == NULL) return -1;
  const int err = indexread(idx, s);
  fclose(s);
  return err;
}

/// @brief Writes the index to the specified file path.
/// @param idx The index to write
/// @param fp The file path to save the index to
/// @return 0 if successful, otherwise a non-zero error code.
static int writeindex(struct index_s* idx, const char* fp) {
  FILE* s = fopen(fp, "w");
  if (s == NULL) return -1;
  const int err = indexwrite(idx, s);
  fclose(s);
  return err;
}

static bool filterjunk(const char* fp) {
  const bool junk = !initargs.includejunk && !lcmdmatchany(cmdsets, fp);
  if (junk && initargs.verbose) log_info("[j] %s", fp);
  return junk;
}

static void onnotifydone(void) { printprogbar(thismap.size, lastmap.size); }

/// @brief Queues command execution for a file event of the specified type,
/// using the provided inode for the file information. If the `skipproc` flag
/// is set, the command execution is skipped. If the `verbose` flag is set, the
/// command execution is done with verbose output.
/// @param in The inode for the file event
/// @param trig The file event type
static void trigfileevent(struct inode_s* in, const int trig) {
  if (initargs.skipproc) return;
  const int flags = trig | (initargs.verbose ? LCTOPT_VERBOSE : 0);
  const struct tpreq_s req = {cmdsets, in, flags};
  int err;
  if ((err = tpqueue(&req)))
    log_error("error executing command set for `%s`: %d", in->fp, err);
}

static void onnew(struct inode_s* in) {
  log_info("[+] %s", in->fp);
  trigfileevent(in, LCTRIG_NEW);
}

static void ondel(struct inode_s* in) {
  log_info("[-] %s", in->fp);
  trigfileevent(in, LCTRIG_DEL);
}

static void onmod(struct inode_s* in) {
  log_info("[*] %s", in->fp);
  trigfileevent(in, LCTRIG_MOD);
}

static void onnop(struct inode_s* in) {
  if (initargs.verbose) log_info("[n] %s", in->fp);
  trigfileevent(in, LCTRIG_NOP);
}

/// @brief Compares the current file system state with a previously saved index.
/// @return 0 if successful, otherwise a non-zero error code.
static int cmpchanges(void) {
  if (loadindex(&lastmap, initargs.indexfile)) {
    // continue if the index file does not exist
    if (errno != ENOENT) {
      log_error("error reading `%s`: %s", initargs.indexfile, strerror(errno));
      return -1;
    }
  }

  const struct deng_hooks_s hooks = {filterjunk, onnotifydone, onnew,
                                     ondel,      onmod,        onnop};

  int err;
  if ((err = dengsearch(initargs.searchdir, &hooks, &lastmap, &thismap))) {
    log_error("error processing directory `%s`: %d", initargs.searchdir, err);
    return -1;
  }

  log_info("compared %zu files", thismap.size);

  if (writeindex(&thismap, initargs.indexfile)) {
    log_error("error writing `%s`: %s", initargs.indexfile, strerror(errno));
    return -1;
  }

  return 0;
}

/// @brief Traces which command sets match the specified file by manually
/// invoking the command execution logic with a trace flag. `lcmdexec` will
/// print the command set names that match the file.
/// @param fp The file path to trace
/// @return 0 if successful, otherwise a non-zero error code.
static int tracefile(const char* fp) {
  struct inode_s node = {.fp = (char*) fp};
  if (fsstat(fp, &node.st)) return -1;
  const struct fdset_s fds = {.out = STDOUT_FILENO, .err = STDERR_FILENO};
  return lcmdexec(cmdsets, &node, &fds, LCTOPT_TRACE | LCTRIG_ALL);
}

static void printmsspent(void) {
  for (size_t i = 0; cmdsets != NULL && cmdsets[i] != NULL; i++) {
    const struct lcmdset_s* s = cmdsets[i];
    log_info("%s: %.3f%s", s->name, (float) s->msspent,
             s->msspent > 1000 ? "s" : "ms");
  }
}

int main(int argc, char** argv) {
  atexit(freeall);
  if (parseinitargs(argc, argv)) return 1;

  // init worker thread pool
  int err;
  const int tpflags = initargs.pipefiles ? TPOPT_LOGFILES : 0;
  if ((err = tpinit(initargs.threads, tpflags))) {
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
    if ((err = tracefile(initargs.tracefile))) {
      log_error("error tracing file `%s`: %d", initargs.tracefile, err);
      return 1;
    }
    return 0;
  } else if ((err = cmpchanges())) {
    log_error("error comparing changes: %d", err);
    return 1;
  }

  if (initargs.listspent) printmsspent();

  return 0;
}
