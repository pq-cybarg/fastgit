#ifndef FASTGIT_CLI_H
#define FASTGIT_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "fastgit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fastgit_cli fastgit_cli_t;

typedef enum {
    FASTGIT_CMD_INIT = 1,
    FASTGIT_CMD_CLONE = 2,
    FASTGIT_CMD_ADD = 3,
    FASTGIT_CMD_COMMIT = 4,
    FASTGIT_CMD_STATUS = 5,
    FASTGIT_CMD_DIFF = 6,
    FASTGIT_CMD_LOG = 7,
    FASTGIT_CMD_CHECKOUT = 8,
    FASTGIT_CMD_BRANCH = 9,
    FASTGIT_CMD_MERGE = 10,
    FASTGIT_CMD_REBASE = 11,
    FASTGIT_CMD_FETCH = 12,
    FASTGIT_CMD_PUSH = 13,
    FASTGIT_CMD_PULL = 14,
    FASTGIT_CMD_REMOTE = 15,
    FASTGIT_CMD_TAG = 16,
    FASTGIT_CMD_RESET = 17,
    FASTGIT_CMD_RM = 18,
    FASTGIT_CMD_MV = 19,
    FASTGIT_CMD_STASH = 20,
    FASTGIT_CMD_SHOW = 21,
    FASTGIT_CMD_BLAME = 22,
    FASTGIT_CMD_GREP = 23,
    FASTGIT_CMD_LS_FILES = 24,
    FASTGIT_CMD_LS_TREE = 25,
    FASTGIT_CMD_CAT_FILE = 26,
    FASTGIT_CMD_HASH_OBJECT = 27,
    FASTGIT_CMD_WRITE_TREE = 28,
    FASTGIT_CMD_READ_TREE = 29,
    FASTGIT_CMD_COMMIT_TREE = 30,
    FASTGIT_CMD_UPDATE_INDEX = 31,
    FASTGIT_CMD_CONFIG = 32,
    FASTGIT_CMD_BENCHMARK = 33,
    FASTGIT_CMD_STATS = 34,
    FASTGIT_CMD_DOCTOR = 35,
    FASTGIT_CMD_MIGRATE = 36,
    FASTGIT_CMD_VERIFY = 37,
    FASTGIT_CMD_VERSION = 38,
} fastgit_cmd_t;

typedef struct {
    const char* name;
    const char* short_name;
    const char* description;
    fastgit_cmd_t cmd;
    int min_args;
    int max_args;
    const char* usage;
    int (*handler)(int argc, char** argv);
} fastgit_cmd_def_t;

fastgit_error_t fastgit_cli_new(int argc, char** argv, fastgit_cli_t** out);
void fastgit_cli_free(fastgit_cli_t* cli);

int fastgit_cli_run(fastgit_cli_t* cli);

const fastgit_cmd_def_t* fastgit_cli_commands(size_t* count);

fastgit_error_t fastgit_cli_register_command(const fastgit_cmd_def_t* cmd);
fastgit_error_t fastgit_cli_unregister_command(fastgit_cmd_t cmd);

typedef struct {
    const char* key;
    const char* value;
    const char* source;
} fastgit_config_entry_t;

fastgit_error_t fastgit_config_get(const char* key, char** value);
fastgit_error_t fastgit_config_set(const char* key, const char* value, const char* scope);
fastgit_error_t fastgit_config_list(fastgit_config_entry_t** entries, size_t* count);
void fastgit_config_free(fastgit_config_entry_t* entries, size_t count);

typedef enum {
    FASTGIT_CONFIG_SCOPE_SYSTEM = 1,
    FASTGIT_CONFIG_SCOPE_GLOBAL = 2,
    FASTGIT_CONFIG_SCOPE_LOCAL = 3,
    FASTGIT_CONFIG_SCOPE_WORKTREE = 4,
} fastgit_config_scope_t;

#ifdef __cplusplus
}
#endif

#endif
