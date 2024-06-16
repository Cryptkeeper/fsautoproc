#include "lcmd.h"

#include <assert.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cJSON/cJSON.h"

#include "index.h"
#include "log.h"
#include "sl.h"

static void lcmdfree(struct lcmdset_s* cmd) {
  slfree(cmd->fpatterns);
  slfree(cmd->syscmds);
  free(cmd);
}

void lcmdfree_r(struct lcmdset_s** cs) {
  for (size_t i = 0; cs != NULL && cs[i] != NULL; i++) lcmdfree(cs[i]);
  free(cs);
}

/// @brief `fsreadstr` reads the contents of the file described by filepath `fp`
/// into a dynamically allocated buffer returned by the function.
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

/// @brief `lcmdjsontosl` duplicates a cJSON array of strings to a slist_t.
/// cJSON array entries that fail `cJSON_IsString` will be ignored and a warning printed.
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

/// @brief `lcmdparseone` populates a single command struct by parsing the
/// provided cJSON object.
/// @param obj cJSON object containing the command data
/// @param cmd Struct to populate with parsed command data
/// @return 0 if successful, otherwise non-zero to indicate an error.
static int lcmdparseone(const cJSON* obj, struct lcmdset_s* cmd) {
  cJSON* onlist = cJSON_GetObjectItem(obj, "on");
  cJSON* plist = cJSON_GetObjectItem(obj, "patterns");
  cJSON* clist = cJSON_GetObjectItem(obj, "commands");

  if (!cJSON_IsArray(onlist) || !cJSON_IsArray(plist) || !cJSON_IsArray(clist))
    return -1;

  if ((cmd->onflags = lcmdparseflags(onlist)) == 0) return -1;
  if ((cmd->fpatterns = lcmdjsontosl(plist)) == NULL) return -1;
  if ((cmd->syscmds = lcmdjsontosl(clist)) == NULL) return -1;

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

  const size_t len = cJSON_GetArraySize(jt);
  if ((cs = calloc(len + 1, sizeof(cs))) == NULL) goto err;

  // iterate over each command block
  cJSON* item;
  size_t i = 0;
  cJSON_ArrayForEach(item, jt) {
    assert(i < len);
    struct lcmdset_s* cmd;
    if ((cmd = cs[i] = malloc(sizeof(*cmd))) == NULL) goto err;
    if (lcmdparseone(item, cmd)) {
      log_error("error parsing command block %zu", i);
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

static bool lcmdmatch(const slist_t* fpatterns, const char* fp) {
  for (size_t i = 0; fpatterns[i] != NULL; i++) {
    int ret;
    if ((ret = fnmatch(fpatterns[i], fp, 0)) == 0) {
      return true;
    } else if (ret != FNM_NOMATCH) {
      log_error("fnmatch error `%s`: %s", fpatterns[i], strerror(errno));
    }
  }
  return false;
}

static int lcmdinvoke(const char* cmd, const struct inode_s* node,
                      const int flags) {
  if (flags & LCTOPT_VERBOSE)
    log_verbose("invoking command `%s` for `%s`", cmd, node->fp);

  // fork the process to run the command
  pid_t pid;
  if ((pid = fork()) < 0) {
    log_error("process forking error `%s`: %s", cmd, strerror(errno));
    return pid;
  } else if (pid == 0) {
    // child process, modify local environment variables for use in commands
    setenv("FILEPATH", node->fp, 1);

    // execute the command and instantly exit child process
    int err;
    if ((err = system(cmd))) log_error("command `%s` returned %d", cmd, err);
    _exit(err); /* avoid firing parent atexit handlers */
  } else {
    // parent process, wait for child process to finish
    if (waitpid(pid, NULL, 0) < 0) {
      log_error("cannot wait for child process %d: %s", pid, strerror(errno));
      return -1;
    }
    return 0;
  }
}

int lcmdexec(struct lcmdset_s** cs, const struct inode_s* node, int flags) {
  if (flags & LCTOPT_VERBOSE)
    log_verbose("exec file: %s (flags: %02x)", node->fp, flags);

  int ret = 0;
  for (size_t i = 0; cs != NULL && cs[i] != NULL; i++) {
    const struct lcmdset_s* s = cs[i];
    if (!(s->onflags & flags) || !lcmdmatch(s->fpatterns, node->fp)) continue;

    // invoke all system commands
    for (size_t j = 0; s->syscmds[j] != NULL; j++)
      if ((ret = lcmdinvoke(s->syscmds[j], node, flags))) break;
  }
  return ret;
}
