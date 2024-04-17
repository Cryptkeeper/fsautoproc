#include "lcmd.h"

#include <assert.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "log.h"

#include "sl.h"
#include "sys.h"

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
/// @return NULL is returned and `errno`is set if the file could not be read.
/// Otherwise, a pointer to the file contents buffer is returned. The caller is
/// responsible for freeing the buffer.
static char* fsreadstr(const char* fp) {
  FILE* fh = NULL;   /* file handle */
  long fsze;         /* file size */
  char* fbuf = NULL; /* file contents buffer */

  if ((fh = fopen(fp, "rb")) == NULL) return NULL;

  // determine file size for buffer allocation
  if (fseek(fh, 0, SEEK_END) != 0 || (fsze = ftell(fh)) < 0) goto err;

  rewind(fh);

  // read file contents into buffer
  if ((fbuf = malloc(fsze + 1)) == NULL) goto err;
  if (fread(fbuf, 1, fsze, fh) != fsze) goto err;
  fbuf[fsze] = '\0'; /* null terminate string */

  fclose(fh);
  return fbuf;

err:
  free(fbuf);
  fclose(fh);
  return NULL;
}

/// @brief `lcmdjsontosl` duplicates a cJSON array of strings to a StringList.
/// cJSON array entries that fail `cJSON_IsString` will be silently ignored.
/// @param arr cJSON array of strings
/// @return NULL if an error occurred, otherwise a pointer to a dynamically
/// allocated StringList. The caller is responsible for freeing the list using
/// `slfree`.
static char** lcmdjsontosl(const cJSON* arr) {
  char** sl = NULL;
  cJSON* e;
  cJSON_ArrayForEach(e, arr) {
    if (!cJSON_IsString(e)) continue;
    char** new = NULL;
    if ((new = sladd(sl, e->valuestring)) == NULL) {
      slfree(sl);
      return NULL;
    }
    sl = new;
  }
  return sl;
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
      log_warn("unknown trigger `%s`", e->valuestring);
    }
  }
  return flags;
}

/// @brief `lcmdparseone` populates a single command struct by parsing the
/// provided cJSON object.
/// @param item cJSON object containing the command data
/// @param cmd Struct to populate with parsed command data
/// @return 0 if successful, otherwise non-zero to indicate an error.
static int lcmdparseone(const cJSON* item, struct lcmdset_s* cmd) {
  cJSON* onlist = cJSON_GetObjectItem(item, "on");
  cJSON* plist = cJSON_GetObjectItem(item, "patterns");
  cJSON* clist = cJSON_GetObjectItem(item, "commands");

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
    perrorf("error reading file `%s`", fp);
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

static bool lcmdmatch(char** fpatterns, const char* fp) {
  for (size_t i = 0; fpatterns[i] != NULL; i++) {
    int ret;
    if ((ret = fnmatch(fpatterns[i], fp, 0)) == 0) {
      return true;
    } else if (ret != FNM_NOMATCH) {
      perrorf("error matching pattern `%s`", fpatterns[i]);
    }
  }
  return false;
}

static int lcmdinvoke(const char* cmd, const struct inode_s* node) {
  log_trace("invoking command `%s` for `%s`", cmd, node->fp);
  // fork the process to run the command
  pid_t pid;
  if ((pid = fork()) < 0) {
    perrorf("error forking process for `%s`", cmd);
    return pid;
  } else if (pid == 0) {
    // child process, modify local environment variables for use in commands
    setenv("FILEPATH", node->fp, 1);

    // execute the command and instantly exit child process
    int err;
    if ((err = system(cmd))) perrorf("error executing command `%s`", cmd);
    exit(err);
  } else {
    // parent process, wait for child process to finish
    if (waitpid(pid, NULL, 0) < 0) {
      perrorf("error waiting for child process %d", pid);
      return -1;
    }
    return 0;
  }
}

int lcmdexec(struct lcmdset_s** cs, const struct inode_s* node, int flags) {
  log_debug("invoking cmd %02x for `%s`", flags, node->fp);
  for (size_t i = 0; cs != NULL && cs[i] != NULL; i++) {
    const struct lcmdset_s* cmdset = cs[i];
    if (!(cmdset->onflags & flags) || !lcmdmatch(cmdset->fpatterns, node->fp))
      continue;

    // invoke all system commands
    for (size_t j = 0; cmdset->syscmds[j] != NULL; j++) {
      const char* cmd = cmdset->syscmds[j];
      int ret;
      if ((ret = lcmdinvoke(cmd, node))) return ret;
    }
  }
  return 0;
}