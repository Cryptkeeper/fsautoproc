/// @file main.c
/// @brief Main program entry point.
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jemalloc/jemalloc.h"

#include "deng.h"
#include "fd.h"
#include "fl.h"
#include "fs.h"
#include "index.h"
#include "je.h"
#include "lcmd.h"
#include "log.h"
#include "prog.h"
#include "tp.h"

/// @brief Managed initialization arguments for the program.
static struct {
  char* configfile;   ///< Configuration file path (-c)
  char* indexfile;    ///< Index file path (-i)
  char* lockfile;     ///< Exclusive lock file path
  char* searchdir;    ///< Search directory root (-s)
  char* tracefile;    ///< Trace file path
  int threads;        ///< Number of worker threads (-t)
  _Bool pipefiles : 1;///< Pipe subprocess stdout/stderr to files
  _Bool listspent : 1;///< List time spent for each command set
  _Bool skipproc : 1; ///< Skip processing files, only update file index
  _Bool verbose : 1;  ///< Enable verbose output
  _Bool preview : 1;  ///< Preview changes without modifying environment
} initargs;

/// @brief Frees all duplicated initialization arguments.
static void freeinitargs(void) {
  je_free(initargs.configfile);
  je_free(initargs.tracefile);
  je_free(initargs.lockfile);
  je_free(initargs.indexfile);
  je_free(initargs.searchdir);
}

static struct lcmdset_s** cmdsets;///< Command sets loaded from configuration

static struct index_s lastmap;///< Stored index from previous run (if any)
static struct index_s thismap;///< Live checked index from this run
static struct index_s goodmap;///< File paths known to match at least one regex

static struct flock_s worklock;///< Exclusive work lock for local directory

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
  indexfree(&goodmap);
  tpfree();
}

/// @brief Allocates and formats a string for the index file path based on the
/// provided configuration file path. The resulting string will have the same
/// name as the configuration file but with an added `.idx` suffix.
/// @param configfp The configuration file path to base the index file path on
/// @return A dynamically allocated string containing the index file path.
/// The caller is responsible for freeing the returned string.
static char* mkindexpath(const char* configfp) {
  const size_t l = strlen(configfp) + 5;// +5 for ".idx\0" suffix
  char* str = je_malloc(l);
  if (str) snprintf(str, l, "%s.idx", configfp);
  return str;
}

/// @def muststrdup
/// @brief Duplicates the source `src` string value into the specified variable.
/// If the duplication fails, an error message is printed and the function
/// returns 1.
/// @param src The source string to duplicate
/// @param dest The destination string variable to store the duplicated value
#define muststrdup(src, dest)                                                  \
  do {                                                                         \
    if ((dest = je_strdup(src)) == NULL) {                                     \
      perror(NULL);                                                            \
      return 1;                                                                \
    }                                                                          \
  } while (0)

#define opt_listTime    1001
#define opt_pipeStd     1002
#define opt_trace       1003
#define opt_updateIndex 1004
#define opt_verbose     1005
#define opt_lockPath    1006
#define opt_preview     1007

/// @brief Parses the program initialization arguments into \p initargs.
/// @param argc The number of arguments
/// @param argv The argument array
/// @return 0 if successful, otherwise a non-zero error code which indicates
/// the caller should exit.
/// @note This function will print the program usage and exit(0) if the `-h`
/// option is provided.
static int parseinitargs(const int argc, char** const argv) {
  static const struct option opts[] = {
          {"help", no_argument, NULL, 'h'},
          {"config", required_argument, NULL, 'c'},
          {"index", required_argument, NULL, 'i'},
          {"list-time", no_argument, NULL, opt_listTime},
          {"pipe-std", no_argument, NULL, opt_pipeStd},
          {"search-dir", required_argument, NULL, 's'},
          {"threads", required_argument, NULL, 't'},
          {"trace", required_argument, NULL, opt_trace},
          {"update-index", no_argument, NULL, opt_updateIndex},
          {"verbose", no_argument, NULL, opt_verbose},
          {"lock-path", required_argument, NULL, opt_lockPath},
          {"preview", no_argument, NULL, opt_preview},
  };

  int c;
  while ((c = getopt_long(argc, argv, ":hc:i:s:t:", opts, NULL)) != -1) {
    switch (c) {
      case 'h':
        printf("Usage: %s -i <file> [options...]\n"
               "\n"
               "Options:\n"
               "  -c --config <file>      Configuration file path (default: `fsautoproc.json`)\n"
               "  -i --index <file>       Index file path\n"
               "  -s --search-dir <dir>   Search directory root (default: `.`)\n"
               "  -t --threads <#>        Number of worker threads (default: 4)\n"
               "     --update-index       Skip processing files, only update file index\n"
               "     --preview            Test changes without modifying file index or running commands\n"
               "     --list-time          List time spent for each command set\n"
               "     --pipe-std           Pipe subprocess stdout/stderr to files\n"
               "     --trace <file>       Trace which command sets match the file\n"
               "     --verbose            Enable verbose output\n"
               "     --lock-path <file>   Exclusive lock file path\n",
               argv[0]);
        exit(0);
      case 'c':
        muststrdup(optarg, initargs.configfile);
        break;
      case 'i':
        muststrdup(optarg, initargs.indexfile);
        break;
      case opt_listTime:
        initargs.listspent = true;
        break;
      case opt_pipeStd:
        initargs.pipefiles = true;
        break;
      case 's':
        muststrdup(optarg, initargs.searchdir);
        break;
      case 't':
        initargs.threads = (int) strtol(optarg, NULL, 10);
        break;
      case opt_trace:
        muststrdup(optarg, initargs.tracefile);
        break;
      case opt_updateIndex:
        initargs.skipproc = true;
        break;
      case opt_verbose:
        initargs.verbose = true;
        break;
      case opt_lockPath:
        muststrdup(optarg, initargs.lockfile);
        break;
      case opt_preview:
        initargs.preview = true;
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
    muststrdup("fsautoproc.json", initargs.configfile);

  if (initargs.searchdir == NULL) muststrdup(".", initargs.searchdir);

  // default to using config file name with .dat suffix
  if (initargs.indexfile == NULL &&
      !(initargs.indexfile = mkindexpath(initargs.configfile))) {
    perror(NULL);
    return 1;
  }

  // default to using fsap.lock inside search directory
  if (initargs.lockfile == NULL &&
      !(initargs.lockfile = fsjoin(initargs.searchdir, "fsap.lock"))) {
    perror(NULL);
    return 1;
  }

  if (initargs.threads == 0) initargs.threads = 4;

  return 0;
}

/// @brief Interrupt signal handler for cleanly exiting on SIGINT.
/// @param signo The signal number (should be SIGINT)
static void interruptsig(const int signo) {
  log_info("received signal: %d\nwaiting for threads to exit...", signo);
  tpwait();// wait for active work to finish
  exit(0);
}

/// @brief Attaches the interrupt signal handler to handle SIGINT signals.
/// @return 0 if successful, otherwise a non-zero error code.
static int siglisten(void) {
  static struct sigaction sa = {0};
  sa.sa_handler = interruptsig;
  sa.sa_flags = SA_NODEFER /* resend repeat signals */ |
                SA_RESETHAND /* restore default handler after first signal */;
  int err;
  if ((err = sigaction(SIGINT, &sa, NULL)))
    log_error("error attaching interrupt signal handler: %s", strerror(errno));
  return err;
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
  const uint64_t fphash = indexhash(fp);
  if (indexfind(&goodmap, fp, fphash)) return false;// previously matched
  const bool junk = !lcmdmatchany(cmdsets, fp);
  if (junk) {
    if (initargs.verbose) log_info("[j] %s", fp);
  } else {
    const struct fsstat_s st = {0};
    indexput(&goodmap, fp, fphash, &st, 0);// mark as known good
  }
  return junk;
}

/// @brief Callback function passed to the diff engine to handle progress
/// notifications. This function will print a progress bar to the console when
/// a directory is completed, and block between stage completions to ensure all
/// thread work requests are complete before the next stage.
/// @param notif The notification type
static void onnotify(const enum deng_notif_t notif) {
  switch (notif) {
    case DENG_NOTIF_FILE_FOUND:
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
  if (initargs.skipproc || initargs.preview) return;
  const int flags = trig | (initargs.verbose ? LCTOPT_VERBOSE : 0);
  const struct tpreq_s req = {cmdsets, in, flags};
  int err;
  if ((err = tpqueue(&req)))
    log_error("error executing command set for `%s`: %d", in->fp, err);
}

/// @brief Callback function for the diff engine to handle file events.
/// This will log a work request in the thread pool for any command sets which
/// match the file event and type.
/// @param event The file event type
/// @param in The inode for the file
static void onevent(const enum deng_fevent_t event, struct inode_s* in) {
  switch (event) {
    case DENG_FEVENT_NEW:
      log_info("[+] %s", in->fp);
      trigfileevent(in, LCTRIG_NEW);
      break;
    case DENG_FEVENT_DEL:
      log_info("[-] %s", in->fp);
      trigfileevent(in, LCTRIG_DEL);
      break;
    case DENG_FEVENT_MOD:
      log_info("[*] %s", in->fp);
      trigfileevent(in, LCTRIG_MOD);
      break;
  }
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

  // attach interrupt signal handler once worker threads will be activated
  int err;
  if ((err = siglisten())) {
    log_error("error attaching interrupt signal handler: %d", err);
    return 1;
  }

  const struct deng_hooks_s hooks = {onnotify, onevent};

  if ((err = dengsearch(initargs.searchdir, filterjunk, &hooks, &lastmap,
                        &thismap))) {
    log_error("error processing directory `%s`: %d", initargs.searchdir, err);
    return -1;
  }

  log_info("compared %zu files", thismap.size);

  if (initargs.preview) return 0; // Don't modifying index in preview mode
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
  const struct fdset_s fds = {.out = STDOUT_FILENO, .err = STDERR_FILENO};
  return lcmdexec(cmdsets, fp, fds, LCTOPT_TRACE | LCTRIG_ALL);
}

/// @brief Prints the time spent for each command set to the console.
static void printmsspent(void) {
  for (size_t i = 0; cmdsets != NULL && cmdsets[i] != NULL; i++) {
    const struct lcmdset_s* s = cmdsets[i];
    const float ts = (float) s->msspent;
    log_info("%s: %.3f%s", s->name, ts > 1000 ? ts / 1000 : ts,
             ts > 1000 ? "s" : "ms");
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
