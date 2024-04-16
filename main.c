#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>

#include "log.h"

#include "fs.h"
#include "index.h"
#include "lcmd.h"
#include "sys.h"

struct args_s {
  char* configfile;
  char* indexfile;
  char* searchdir;
};

static struct args_s initargs;

static void freeinitargs(void) {
  free(initargs.configfile);
  free(initargs.indexfile);
  free(initargs.searchdir);
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

static struct lcmdfile_s cmdfile; /* global loaded command list */

static void freecmdfile(void) { lcmdfilefree(&cmdfile); }

static struct inode_s* lastmap; /* stored index from previous run (if any) */
static struct inode_s* thismap; /* live checked index from this run */

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

static int fsnodestat(const char* fp, struct inode_s* node) {
  log_info("processing file `%s`", fp);

  if (node->fp == NULL)
    if ((node->fp = strdup(fp)) == NULL) return -1;
  if (fsstat(fp, &node->st)) return -1;

  // generate a unique hash sum for the file
  uint8_t sum[INODESUMBYTES];
  if (fssum(fp, sum)) return -1;

  // generate formatted string hash from sum bytes
  uint8_t hash[INODESTRBYTES] = {0};
  uint8_t* wp = &hash[0]; /* moving write pointer */
  for (int i = 0; i < INODESUMBYTES; i++)
    wp += sprintf((char*) wp, "%02x", sum[i]);
  *wp = '\0'; /* null terminate string */
  memcpy(node->sum_s, hash, sizeof(hash));

  return 0;
}

static int fsprocfile(const char* fp) {
  struct inode_s finfo = {0};
  if (fsnodestat(fp, &finfo)) return -1;

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
    bool dirty;
    if (!fsstateql(&prev->st, &curr->st)) {
      log_trace("file `%s` has been modified (last modified: %zu -> %zu, file "
                "size: %zu -> %zu)",
                curr->fp, prev->st.lmod, curr->st.lmod, prev->st.fsze,
                curr->st.fsze);
      dirty = true;
    } else if (strncmp((char*) prev->sum_s, (char*) curr->sum_s,
                       INODESTRBYTES) != 0) {
      log_trace("file `%s` has been modified (hash: %s -> %s)", curr->fp,
                prev->sum_s, curr->sum_s);
      dirty = true;
    } else {
      log_trace("file `%s` has not been modified", curr->fp);
      dirty = false;
    }

    int err;
    if ((err = lcmdfileexec(&cmdfile, curr, dirty ? LCTRIG_MOD : LCTRIG_NOP)))
      return err;

    if (!dirty) return 0;
  } else {
    log_debug("file does not exist in previous index `%s`", curr->fp);
    int err;
    if ((err = lcmdfileexec(&cmdfile, curr, LCTRIG_NEW))) return err;
  }

  // update the file info in the current index
  // this is done after the command execution to capture any file modifications
  if (fsnodestat(fp, curr)) return -1;

  return 0;
}

static int fsprocfileonlynew(const char* fp) {
  struct inode_s* curr = indexfind(thismap, fp);
  if (curr != NULL) return 0;

  struct inode_s finfo = {0};
  if (fsnodestat(fp, &finfo)) return -1;

  thismap = indexprepend(thismap, finfo);
  curr = indexfind(thismap, fp);
  assert(curr != NULL); /* should+must exist in the list */

  log_debug("file `%s` is new (post-command exec)", curr->fp);
  int err;
  if ((err = lcmdfileexec(&cmdfile, curr, LCTRIG_NEW))) return err;

  // update the file info in the current index
  // this is done after the command execution to capture any file modifications
  if (fsnodestat(fp, curr)) return -1;

  return 0;
}

static StringList* dirqueue;

static void fsqueuedirwalk(const char* dp) {
  assert(dirqueue != NULL); /* previously alloc'd with first argument value */
  log_debug("adding directory to queue: %s", dp);
  char* s;
  if ((s = strdup(dp)) == NULL) {
    perror(NULL);
    return;
  }

  // append the directory to the processing queue
  if (sl_add(dirqueue, s) != 0) perror(NULL);
}

static int fsinitwalkqueue(const char* searchdir) {
  if (dirqueue != NULL) sl_free(dirqueue, 1);

  // place the initial search dir starting value into the queue
  char* sd;
  if ((dirqueue = sl_init()) == NULL || (sd = strdup(searchdir)) == NULL ||
      sl_add(dirqueue, sd) != 0) {
    perror(NULL);
    return -1;
  }
  return 0;
}

static void checkremoved(void) {
  if (lastmap == NULL) return;// no previous map entries to check

  struct inode_s* head = lastmap;
  while (head != NULL) {
    if (indexfind(thismap, head->fp) == NULL) { /* file no longer exists */
      int err;
      if ((err = lcmdfileexec(&cmdfile, head, LCTRIG_DEL)))
        log_warn("error invoking DEL cmds for `%s` (%d)", head->fp, err);
    }
    head = head->next;
  }
}

static int submain(const struct args_s* args) {
  if (loadlastmap(args->indexfile)) {
    // attempt to load the file, but continue if it doesn't exist
    perrorf("error loading previously saved index `%s`", args->indexfile);
    if (errno != ENOENT) return -1;
  }

  // walk each directory in the queue (including as they are added)
  if (fsinitwalkqueue(args->searchdir)) return -1;
  for (size_t i = 0; i < dirqueue->sl_cur; i++) {
    const char* d = dirqueue->sl_str[i];
    log_info("walking dir `%s`", d);

    if (fswalk(d, fsprocfile, fsqueuedirwalk)) {
      perrorf("error walking directory `%s`", d);
      return -1;
    }
  }

  checkremoved();

  // reset walk queue to find new files/directories as a result of commands
  if (fsinitwalkqueue(args->searchdir)) return -1;
  for (size_t i = 0; i < dirqueue->sl_cur; i++) {
    const char* d = dirqueue->sl_str[i];
    log_debug("post-command scan `%s`", d);

    if (fswalk(d, fsprocfileonlynew, fsqueuedirwalk)) {
      perrorf("error walking directory `%s`", d);
      return -1;
    }
  }

  if (savethismap(args->indexfile)) {
    perror("error writing updated index");
    return -1;
  }

  return 0;
}

int main(int argc, char** argv) {
  atexit(freeinitargs);
  atexit(freecmdfile);

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
  if (lcmdfileparse(defargs.configfile, &cmdfile) < 0) {
    fprintf(stderr, "error loading configuration file `%s`\n",
            defargs.configfile);
    return 1;
  }

  int err;
  if ((err = submain(&defargs))) return err;

  indexfree_r(lastmap);
  indexfree_r(thismap);

  if (dirqueue != NULL) sl_free(dirqueue, 1);

  return 0;
}
