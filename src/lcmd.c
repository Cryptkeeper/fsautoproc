/// @file lcmd.c
/// @brief File-specific system command execution mapping implementation.
#include "lcmd.h"

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cJSON/cJSON.h"

#include "fd.h"
#include "index.h"
#include "log.h"
#include "sl.h"
#include "tm.h"

/// @brief Frees the memory allocated for a single command set entry struct.
/// @param cmd Command set entry to free
static void lcmdfree(struct lcmdset_s* cmd) {
  for (size_t i = 0; cmd->fpatterns[i] != NULL; i++) {
    regex_t* reg = cmd->fpatterns[i];
    if (reg == NULL) continue;
    regfree(reg);
    free(reg);
  }
  free(cmd->fpatterns);
  free(cmd->name);
  slfree(cmd->syscmds);
  free(cmd);
}

void lcmdfree_r(struct lcmdset_s** cs) {
  for (size_t i = 0; cs != NULL && cs[i] != NULL; i++) lcmdfree(cs[i]);
  free(cs);
}

/// @brief Reads the contents of the file described by filepath \p fp into a
/// dynamically allocated buffer returned by the function.
/// @param fp The filepath to read
/// @return If successful, a pointer to a dynamically allocated buffer of the
/// file's null terminated contents. The caller is responsible for freeing the
/// buffer.
static char* fsreadstr(const char* fp) {
  long fsze;         /* file size */
  char* fbuf = NULL; /* file contents buffer */
  FILE* fh = NULL;   /* file handle */
  if ((fh = fopen(fp, "rb")) == NULL) return NULL;

  // determine file size for buffer allocation
  if (fseek(fh, 0, SEEK_END) != 0 || (fsze = ftell(fh)) < 0 ||
      fseek(fh, 0, SEEK_SET) != 0)
    goto err;

  // read file contents into buffer
  if ((fbuf = malloc(fsze + 1)) == NULL) goto err;
  if ((long) fread(fbuf, 1, fsze, fh) != fsze) goto err;
  fbuf[fsze] = '\0';
  fclose(fh);
  return fbuf;

err:
  free(fbuf);
  fclose(fh);
  return NULL;
}

/// @brief Duplicates a cJSON array of strings into a slist_t. cJSON array
/// entries that fail `cJSON_IsString` will be ignored and a warning printed.
/// @param arr cJSON array of strings
/// @return NULL if an error occurred, otherwise a pointer to a dynamically
/// allocated slist_t. The caller is responsible for freeing the list using
/// `slfree`.
static slist_t* lcmdjsontosl(const cJSON* arr) {
  slist_t* sl = NULL;
  cJSON* e;
  cJSON_ArrayForEach(e, arr) {
    if (!cJSON_IsString(e)) {
      log_error("error converting cmd, not a string: %s", e->valuestring);
    } else if (sladd(&sl, e->valuestring)) {
      goto err;
    }
  }
  return sl;
err:
  slfree(sl);
  return NULL;
}

/// @brief Parses a cJSON array of strings into a set of file event bit flags.
/// Non-string entries are ignored. Unrecognized string entries will log an
/// error message. Accepted strings are "new", "mod", "del", and "nop" which map
/// to \p LCTRIG_NEW, \p LCTRIG_MOD, \p LCTRIG_DEL, and \p LCTRIG_NOP respectively.
/// @param item cJSON array of strings
/// @return Bit flags representing the file event types, or 0 if no flags were
/// correctly parsed.
static int lcmdparseflags(const cJSON* item) {
  int flags = 0;
  cJSON* e;
  cJSON_ArrayForEach(e, item) {
    if (!cJSON_IsString(e)) continue;
    if (strcmp(e->valuestring, "new") == 0) {
      flags |= LCTRIG_NEW;
    } else if (strcmp(e->valuestring, "mod") == 0) {
      flags |= LCTRIG_MOD;
    } else if (strcmp(e->valuestring, "del") == 0) {
      flags |= LCTRIG_DEL;
    } else if (strcmp(e->valuestring, "nop") == 0) {
      flags |= LCTRIG_NOP;
    } else {
      log_error("unknown flag name `%s`", e->valuestring);
    }
  }
  return flags;
}

/// @brief Populates a single command struct by parsing the fields of the
/// provided cJSON object.
/// @param obj cJSON object containing the command data
/// @param cmd Struct to populate with parsed command data
/// @param id Command set index for naming purposes
/// @return 0 if successful, otherwise non-zero to indicate an error.
static int lcmdparseone(const cJSON* obj, struct lcmdset_s* cmd, const int id) {
  cJSON* onlist = cJSON_GetObjectItem(obj, "on");
  cJSON* plist = cJSON_GetObjectItem(obj, "patterns");
  cJSON* clist = cJSON_GetObjectItem(obj, "commands");

  if (!cJSON_IsArray(onlist) || !cJSON_IsArray(plist) || !cJSON_IsArray(clist))
    return -1;

  if ((cmd->onflags = lcmdparseflags(onlist)) == 0) return -1;
  if ((cmd->syscmds = lcmdjsontosl(clist)) == NULL) return -1;

  // copy description, otherwise use the index as the name
  cJSON* desc = cJSON_GetObjectItem(obj, "description");
  if (cJSON_IsString(desc)) {
    if ((cmd->name = strdup(desc->valuestring)) == NULL) return -1;
  } else {
    char b[32] = {0};
    snprintf(b, sizeof(b), "cmdset %d", id);
    if ((cmd->name = strdup(b)) == NULL) return -1;
  }

  const int regcount = cJSON_GetArraySize(plist);
  if ((cmd->fpatterns = calloc(regcount + 1, sizeof(regex_t*))) == NULL)
    return -1;

  // compile regex patterns
  for (int i = 0; i < regcount; i++) {
    cJSON* p = cJSON_GetArrayItem(plist, i);
    if (!cJSON_IsString(p)) return -1;

    regex_t* reg;
    if ((reg = cmd->fpatterns[i] = calloc(1, sizeof(*reg))) == NULL) return -1;

    int regmode = REG_EXTENDED | REG_NOSUB;
#ifdef __APPLE__
    regmode |= REG_ENHANCED;
#endif

    if (regcomp(reg, p->valuestring, regmode)) {
      char errmsg[512] = {0};
      regerror(errno, reg, errmsg, sizeof(errmsg));
      log_error("error compiling pattern `%s`: %s", p->valuestring, errmsg);

      return -1;
    }
  }

  return 0;
}

struct lcmdset_s** lcmdparse(const char* fp) {
  char* fbuf = NULL;            /* file contents buffer */
  cJSON* jt = NULL;             /* parsed JSON tree */
  struct lcmdset_s** cs = NULL; /* command set array */

  if ((fbuf = fsreadstr(fp)) == NULL) {
    log_error("error reading file `%s`: %s", fp, strerror(errno));
    return NULL;
  }
  if ((jt = cJSON_Parse(fbuf)) == NULL) {
    log_error("error parsing JSON file `%s`", fp);
    goto err;
  }

  const int len = cJSON_GetArraySize(jt);
  if ((cs = calloc(len + 1, sizeof(cs))) == NULL) goto err;

  // iterate over each command block
  cJSON* item;
  int i = 0;
  cJSON_ArrayForEach(item, jt) {
    assert(i < len);
    struct lcmdset_s* cmd;
    if ((cmd = cs[i] = malloc(sizeof(*cmd))) == NULL) goto err;
    if (lcmdparseone(item, cmd, i)) {
      log_error("error parsing command block %d", i);
      goto err;
    }
    i++;
  }

  goto ok;

err:
  lcmdfree_r(cs);
ok:
  free(fbuf);
  if (jt != NULL) cJSON_Delete(jt);
  return cs;
}

/// @brief Checks if the provided filepath matches any of the compiled regex
/// patterns in the provided array.
/// @param fpatterns Array of compiled regex patterns
/// @param fp Filepath to match
/// @return True if the filepath matches any of the patterns, otherwise false.
static bool lcmdmatch(regex_t** fpatterns, const char* fp) {
  for (size_t i = 0; fpatterns[i] != NULL; i++)
    if (!regexec(fpatterns[i], fp, 0, NULL, 0)) return true;
  return false;
}

bool lcmdmatchany(struct lcmdset_s** cs, const char* fp) {
  for (size_t i = 0; cs != NULL && cs[i] != NULL; i++)
    if (lcmdmatch(cs[i]->fpatterns, fp)) return true;
  return false;
}

/// @brief Invokes a string \p cmd as a system command using `system(3)` in a
/// forked/child process. The file path of \p node is set as an environment
/// variable for use in the command. File descriptor set \p fds is used to
/// optionally redirect stdout and stderr of the child command processes.
/// @param cmd The command string to execute
/// @param node The file node to use for the FILEPATH environment variable
/// @param fds The file descriptor set to use for stdout/stderr redirection
/// @param flags Bit flags for controlling command execution. If the
/// `LCTOPT_VERBOSE` flag is set, the command will be printed to stdout before
/// execution. If the `LCTOPT_TRACE` flag is set, debug information regarding
/// the eligibility of any encountered file path will be printed to stdout.
/// @param msspent Optional pointer to a uint64_t value to which the time spent
/// executing the command (in milliseconds) will be added.
/// @return 0 if successful, otherwise -1 to indicate an error.
static int lcmdinvoke(const char* cmd, const struct inode_s* node,
                      const struct fdset_s* fds, const int flags,
                      uint64_t* msspent) {
  if (flags & LCTOPT_VERBOSE) log_verbose("[x] %s", cmd);

  const uint64_t start = tmnow();

  // fork the process to run the command
  pid_t pid;
  if ((pid = fork()) < 0) {
    log_error("process forking error `%s`: %s", cmd, strerror(errno));
    return -1;
  } else if (pid == 0) {
    if (dup2(fds->out, STDOUT_FILENO) < 0) {
      log_error("cannot redirect stdout to %d: %s", fds->out, strerror(errno));
      _exit(1); /* avoid firing parent atexit handlers */
    }
    if (dup2(fds->err, STDERR_FILENO) < 0) {
      log_error("cannot redirect stderr to %d: %s", fds->err, strerror(errno));
      _exit(1); /* avoid firing parent atexit handlers */
    }

    // child process, modify local environment variables for use in commands
    setenv("FILEPATH", node->fp, 1);

    // execute the command and instantly exit child process
    int err;
    if ((err = system(cmd))) log_error("command `%s` returned %d", cmd, err);
    fdclose((struct fdset_s*) fds); /* close child process references */
    _exit(err);                     /* avoid firing parent atexit handlers */
  } else {
    // parent process, wait for child process to finish
    if (waitpid(pid, NULL, 0) < 0) {
      log_error("cannot wait for child process %d: %s", pid, strerror(errno));
      return -1;
    }

    // return the time spent executing the command
    if (msspent != NULL) *msspent += tmnow() - start;
    return 0;
  }
}

int lcmdexec(struct lcmdset_s** cs, const struct inode_s* node,
             const struct fdset_s* fds, int flags) {
  int ret = 0;
  for (size_t i = 0; cs != NULL && cs[i] != NULL; i++) {
    struct lcmdset_s* s = cs[i];
    if (!(s->onflags & flags)) {
      if (flags & LCTOPT_TRACE)
        log_info("cmdset %zu ignored flags: 0x%02X", i, flags);
      continue;
    }
    if (!lcmdmatch(s->fpatterns, node->fp)) {
      if (flags & LCTOPT_TRACE)
        log_info("cmdset %zu ignored filepath: %s", i, node->fp);
      continue;
    }

    if (flags & LCTOPT_TRACE) {
      log_info("cmdset %zu (0x%02X) matched: %s", i, s->onflags, node->fp);
      continue;// skip executing commands
    }

    // invoke all system commands
    for (size_t j = 0; s->syscmds[j] != NULL; j++)
      if ((ret = lcmdinvoke(s->syscmds[j], node, fds, flags, &s->msspent)))
        break;
  }
  return ret;
}
