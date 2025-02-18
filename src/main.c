/// @file main.c
/// @brief Main program entry point.
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "deng.h"
#include "fd.h"
#include "fl.h"
#include "fs.h"
#include "index.h"
#include "lcmd.h"
#include "log.h"
#include "prog.h"
#include "tp.h"

/// @brief Managed initialization arguments for the program.
static struct {
  char* configfile; ///< Configuration file path (-c)
  char* indexfile;  ///< Index file path (-i)
  char* lockfile;   ///< Exclusive lock file path (-x)
  char* searchdir;  ///< Search directory root (-s)
  char* tracefile;  ///< Trace file path (-r)
  _Bool pipefiles;  ///< Pipe subprocess stdout/stderr to files (-p)
  _Bool includejunk;///< Include ignored files in index (-j)
  _Bool listspent;  ///< List time spent for each command set (-l)
  _Bool skipproc;   ///< Skip processing files, only update file index (-u)
  int threads;      ///< Number of worker threads (-t)
  _Bool verbose;    ///< Enable verbose output (-v)
} initargs;

/// @brief Frees all duplicated initialization arguments.
static void freeinitargs(void) {
  free(initargs.configfile);
  free(initargs.tracefile);
  free(initargs.lockfile);
  free(initargs.indexfile);
  free(initargs.searchdir);
}

static struct lcmdset_s** cmdsets; ///< Command sets loaded from configuration

static struct index_s lastmap; ///< Stored index from previous run (if any)
static struct index_s thismap; ///< Live checked index from this run

static struct flock_s worklock; ///< Exclusive work lock for local directory

/// @brief Frees all allocated resources.
static void freeall(void) {
  // release work lock, if successfully opened
  if (worklock.open && flunlock(&worklock))
    log_error("error releasing lock file for local directory: %s (you may need "
              "to delete it manually)",
              worklock.path);

  tpshutdown();
  freeinitargs();
  lcmdfree_r(cmdsets);
  indexfree(&lastmap);
  indexfree(&thismap);
  tpfree();
}

/// @def strdupoptarg
/// @brief Duplicates the current `optarg` value into the specified variable.
/// If the duplication fails, an error message is printed and the function
/// returns 1.
/// @param into The variable to duplicate into
#define strdupoptarg(into)                                                     \
  do {                                                                         \
    if ((into = strdup(optarg)) == NULL) {                                     \
      perror(NULL);                                                            \
      return 1;                                                                \
    }                                                                          \
  } while (0)

/// @brief Parses the program initialization arguments into \p initargs.
/// @param argc The number of arguments
/// @param argv The argument array
/// @return 0 if successful, otherwise a non-zero error code which indicates
/// the caller should exit.
/// @note This function will print the program usage and exit(0) if the `-h`
/// option is provided.
static int parseinitargs(const int argc, char** const argv) {
  int c;
  while ((c = getopt(argc, argv, ":hc:i:jlps:t:r:uvx:")) != -1) {
    switch (c) {
      case 'h':
        printf("Usage: %s -i <file>\n"
               "\n"
               "Options:\n"
               "  -c <file>   Configuration file (default: `fsautoproc.json`)\n"
               "  -i <file>   File index write path\n"
               "  -j          Enable including ignored files in index\n"
               "  -l          List time spent for each command set\n"
               "  -p          Pipe subprocess stdout/stderr to files\n"
               "  -s <dir>    Search directory root (default: `.`)\n"
               "  -t <#>      Number of worker threads (default: 4)\n"
               "  -r <file>   Trace which command sets match the file\n"
               "  -u          Skip processing files, only update file index\n"
               "  -v          Enable verbose output\n"
               "  -x <file>   Exclusive lock file path\n",
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
      case 'x':
        strdupoptarg(initargs.lockfile);
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

  if (initargs.lockfile == NULL) {
    // default to using fsautoproc.lock inside search directory
    char fp[256];
    snprintf(fp, sizeof(fp), "%s/fsautoproc.lock", initargs.searchdir);
    if ((initargs.lockfile = strdup(fp)) == NULL) return 1;
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

/// @brief Filters out junk files from the index based on loaded command sets
/// \p cmdsets and the \p initargs.includejunk flag/program option.
/// @param fp The file path to filter
/// @return True if the file is considered junk, otherwise false.
static bool filterjunk(const char* fp) {
  const bool junk = !initargs.includejunk && !lcmdmatchany(cmdsets, fp);
  if (junk && initargs.verbose) log_info("[j] %s", fp);
  return junk;
}

/// @brief Callback function passed to the diff engine to handle progress
/// notifications. This function will print a progress bar to the console when
/// a directory is completed, and block between stage completions to ensure all
/// thread work requests are complete before the next stage.
/// @param notif The notification type
static void onnotify(const enum deng_notif_t notif) {
  switch (notif) {
    case DENG_NOTIF_DIR_DONE:
      printprogbar(thismap.size, lastmap.size);
      break;
    case DENG_NOTIF_STAGE_DONE:
      tpwait(); /* wait for all queued commands to finish */
      break;
  }
}

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

/// @brief Callback function for the diff engine to handle new file events.
/// This will log a work request in the thread pool for any command sets which
/// match the new file event.
/// @param in The inode for the new file
static void onnew(struct inode_s* in) {
  log_info("[+] %s", in->fp);
  trigfileevent(in, LCTRIG_NEW);
}

/// @brief Callback function for the diff engine to handle deleted file events.
/// This will log a work request in the thread pool for any command sets which
/// match the deleted file event.
/// @param in The inode for the deleted file
static void ondel(struct inode_s* in) {
  log_info("[-] %s", in->fp);
  trigfileevent(in, LCTRIG_DEL);
}

/// @brief Callback function for the diff engine to handle modified file events.
/// This will log a work request in the thread pool for any command sets which
/// match the modified file event.
/// @param in The inode for the modified file
static void onmod(struct inode_s* in) {
  log_info("[*] %s", in->fp);
  trigfileevent(in, LCTRIG_MOD);
}

/// @brief Callback function for the diff engine to handle no-op file events.
/// This will log a work request in the thread pool for any command sets which
/// match the unmodified file event.
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

  const struct deng_hooks_s hooks = {onnotify, onnew, ondel, onmod, onnop};

  int err;
  if ((err = dengsearch(initargs.searchdir, filterjunk, &hooks, &lastmap,
                        &thismap))) {
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

/// @brief Prints the time spent for each command set to the console.
static void printmsspent(void) {
  for (size_t i = 0; cmdsets != NULL && cmdsets[i] != NULL; i++) {
    const struct lcmdset_s* s = cmdsets[i];
    log_info("%s: %.3f%s", s->name, (float) s->msspent,
             s->msspent > 1000 ? "s" : "ms");
  }
}

/// @brief Main program entry point.
/// @param argc The number of arguments
/// @param argv The argument array
/// @return 0 if successful, otherwise a non-zero error code.
int main(int argc, char** argv) {
  atexit(freeall);
  if (parseinitargs(argc, argv)) return 1;

  int err;

  // establish work lock
  worklock = flinit(initargs.lockfile);
  if ((err = fllock(&worklock))) {
    log_error("error establishing exclusive lock file for local directory "
              "`%s`: %d (is another instance already running? did a previous "
              "instance crash?)",
              worklock.path, err);
    return 1;
  }

  // init worker thread pool
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
