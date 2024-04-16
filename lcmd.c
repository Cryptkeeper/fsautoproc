#include "lcmd.h"

#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "log.h"

#include "sys.h"

static void lcmdsetfree(struct lcmdset_s* cmd) {
  sl_free(cmd->fpatterns, 1);
  sl_free(cmd->syscmds, 1);
}

void lcmdfilefree(struct lcmdfile_s* cmdfile) {
  for (size_t i = 0; i < cmdfile->len; i++) lcmdsetfree(&cmdfile->head[i]);
  free(cmdfile->head);
  cmdfile->head = NULL, cmdfile->len = 0;
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
/// `sl_free`.
static StringList* lcmdjsontosl(const cJSON* arr) {
  StringList* sl;
  if ((sl = sl_init()) == NULL) return NULL;
  cJSON* e;
  cJSON_ArrayForEach(e, arr) {
    if (!cJSON_IsString(e)) continue;
    char* str; /* duplicate string for StringList ownership */
    if ((str = strdup(e->valuestring)) == NULL || sl_add(sl, str)) {
      sl_free(sl, 1);
      return NULL;
    }
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
      fprintf(stderr, "unknown trigger `%s`", e->valuestring);
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

int lcmdfileparse(const char* fp, struct lcmdfile_s* cmdfile) {
  char* fbuf = NULL; /* file contents buffer */
  cJSON* jt = NULL;  /* parsed JSON tree */
  int ret;           /* error return value */

  if ((fbuf = fsreadstr(fp)) == NULL) {
    ret = -1;
    goto doret;
  }
  if ((jt = cJSON_Parse(fbuf)) == NULL) {
    ret = -1;
    goto doret;
  }

  // iterate over each command block
  cJSON* item;
  cJSON_ArrayForEach(item, jt) {
    struct lcmdset_s tmpcmd = {0};
    if (lcmdparseone(item, &tmpcmd)) {
      ret = -1;
      goto doret;
    }

    // append the parsed command to the `cmds` array
    struct lcmdset_s* newalloc = realloc(
            cmdfile->head, (cmdfile->len + 1) * sizeof(struct lcmdset_s));
    if (newalloc == NULL) {
      ret = -1;
      goto doret;
    }
    newalloc[cmdfile->len] = tmpcmd, cmdfile->head = newalloc, cmdfile->len++;
  }

  ret = 0; /* success */

doret:
  free(fbuf);
  if (jt != NULL) cJSON_Delete(jt);
  return ret;
}

static bool lcmdmatch(const StringList* fpatterns, const char* fp) {
  for (size_t i = 0; i < fpatterns->sl_cur; i++) {
    int ret;
    if ((ret = fnmatch(fpatterns->sl_str[i], fp, 0)) == 0) {
      return true;
    } else if (ret != FNM_NOMATCH) {
      perrorf("error matching pattern `%s`", fpatterns->sl_str[i]);
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
    setenv("FILEHASH", node->sum_s, 1);

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

int lcmdfileexec(const struct lcmdfile_s* cmdfile, const struct inode_s* node,
                 const int onflags) {
  log_debug("invoking cmd %02x for `%s`", onflags, node->fp);
  for (size_t i = 0; i < cmdfile->len; i++) {
    const struct lcmdset_s* cmdset = &cmdfile->head[i];
    if (!(cmdset->onflags & onflags) || !lcmdmatch(cmdset->fpatterns, node->fp))
      continue;

    // invoke all system commands
    for (size_t j = 0; j < cmdset->syscmds->sl_cur; j++) {
      const char* cmd = cmdset->syscmds->sl_str[j];
      int ret;
      if ((ret = lcmdinvoke(cmd, node))) return ret;
    }
  }
  return 0;
}