#include "fastgit/cli.h"
#include "fastgit/fastgit.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

struct fastgit_cli {
    int argc;
    char** argv;
    fastgit_cmd_t cmd;
    int cmd_argc;
    char** cmd_argv;
};

// Forward declarations for builtin handlers (defined in cmd_*.c)
int fastgit_cmd_init(int argc, char **argv);
int fastgit_cmd_clone(int argc, char **argv);
int fastgit_cmd_version(int argc, char **argv);
int fastgit_cmd_add(int argc, char **argv);
int fastgit_cmd_commit(int argc, char **argv);
int fastgit_cmd_hash_object(int argc, char **argv);
int fastgit_cmd_cat_file(int argc, char **argv);
int fastgit_cmd_write_tree(int argc, char **argv);
int fastgit_cmd_read_tree(int argc, char **argv);
int fastgit_cmd_commit_tree(int argc, char **argv);
int fastgit_cmd_update_index(int argc, char **argv);
int fastgit_cmd_branch(int argc, char **argv);
int fastgit_cmd_tag(int argc, char **argv);
int fastgit_cmd_reset(int argc, char **argv);
int fastgit_cmd_rm(int argc, char **argv);
int fastgit_cmd_mv(int argc, char **argv);
int fastgit_cmd_checkout(int argc, char **argv);
int fastgit_cmd_fetch(int argc, char **argv);
int fastgit_cmd_push(int argc, char **argv);
int fastgit_cmd_pull(int argc, char **argv);
int fastgit_cmd_remote(int argc, char **argv);
int fastgit_cmd_merge(int argc, char **argv);
int fastgit_cmd_rebase(int argc, char **argv);
int fastgit_cmd_ls_files(int argc, char **argv);
int fastgit_cmd_ls_tree(int argc, char **argv);
int fastgit_cmd_show(int argc, char **argv);
int fastgit_cmd_diff(int argc, char **argv);
int fastgit_cmd_log(int argc, char **argv);
int fastgit_cmd_status(int argc, char **argv);
int fastgit_cmd_stash(int argc, char **argv);
int fastgit_cmd_blame(int argc, char **argv);
int fastgit_cmd_grep(int argc, char **argv);
int fastgit_cmd_doctor(int argc, char **argv);
int fastgit_cmd_migrate(int argc, char **argv);
int fastgit_cmd_verify(int argc, char **argv);
int fastgit_cmd_benchmark(int argc, char **argv);
int fastgit_cmd_stats(int argc, char **argv);
int fastgit_cmd_config(int argc, char **argv);

static const fastgit_cmd_def_t builtin_commands[] = {
    { "init", "init", "Create an empty Git repository", FASTGIT_CMD_INIT, 0, 1, "[<directory>]", fastgit_cmd_init },
    { "clone", "clone", "Clone a repository into a new directory", FASTGIT_CMD_CLONE, 1, 2, "<repository> [<directory>]", fastgit_cmd_clone },
    { "add", "add", "Add file contents to the index", FASTGIT_CMD_ADD, 1, -1, "<pathspec>...", fastgit_cmd_add },
    { "commit", "commit", "Record changes to the repository", FASTGIT_CMD_COMMIT, 0, -1, "[-m <message>] [<pathspec>...]", fastgit_cmd_commit },
    { "status", "st", "Show the working tree status", FASTGIT_CMD_STATUS, 0, -1, "[<pathspec>...]", fastgit_cmd_status },
    { "diff", "diff", "Show changes between commits, commit and working tree, etc", FASTGIT_CMD_DIFF, 0, -1, "[<options>] [<commit>] [--] [<path>...]", fastgit_cmd_diff },
    { "log", "log", "Show commit logs", FASTGIT_CMD_LOG, 0, -1, "[<options>] [<revision range>] [[--] <path>...]", fastgit_cmd_log },
    { "checkout", "co", "Switch branches or restore working tree files", FASTGIT_CMD_CHECKOUT, 1, -1, "<branch> [<paths>...]", fastgit_cmd_checkout },
    { "branch", "branch", "List, create, or delete branches", FASTGIT_CMD_BRANCH, 0, -1, "[<branchname> [<start-point>]]", fastgit_cmd_branch },
    { "merge", "merge", "Join two or more development histories together", FASTGIT_CMD_MERGE, 1, -1, "<commit>...", fastgit_cmd_merge },
    { "rebase", "rebase", "Reapply commits on top of another base tip", FASTGIT_CMD_REBASE, 0, -1, "[<upstream> [<branch>]]", fastgit_cmd_rebase },
    { "fetch", "fetch", "Download objects and refs from another repository", FASTGIT_CMD_FETCH, 0, -1, "[<repository> [<refspec>...]]", fastgit_cmd_fetch },
    { "push", "push", "Update remote refs along with associated objects", FASTGIT_CMD_PUSH, 0, -1, "[<repository> [<refspec>...]]", fastgit_cmd_push },
    { "pull", "pull", "Fetch from and integrate with another repository", FASTGIT_CMD_PULL, 0, -1, "[<repository> [<refspec>...]]", fastgit_cmd_pull },
    { "remote", "remote", "Manage set of tracked repositories", FASTGIT_CMD_REMOTE, 0, -1, "<subcommand>...", fastgit_cmd_remote },
    { "tag", "tag", "Create, list, delete or verify a tag object", FASTGIT_CMD_TAG, 0, -1, "[-a|-s|-u <key-id>] [-m <msg>] [<tagname> [<commit>]]", fastgit_cmd_tag },
    { "reset", "reset", "Reset current HEAD to the specified state", FASTGIT_CMD_RESET, 0, -1, "[<mode>] [<commit>]", fastgit_cmd_reset },
    { "rm", "rm", "Remove files from the working tree and from the index", FASTGIT_CMD_RM, 1, -1, "[-f] [-r] [--] <pathspec>...", fastgit_cmd_rm },
    { "mv", "mv", "Move or rename a file, a directory, or a symlink", FASTGIT_CMD_MV, 2, -1, "<source> <destination>", fastgit_cmd_mv },
    { "stash", "stash", "Stash the changes in a dirty working directory", FASTGIT_CMD_STASH, 0, -1, "<subcommand>...", fastgit_cmd_stash },
    { "show", "show", "Show various types of objects", FASTGIT_CMD_SHOW, 0, -1, "[<object>...]", fastgit_cmd_show },
    { "blame", "blame", "Show what revision and author last modified each line of a file", FASTGIT_CMD_BLAME, 0, -1, "[<options>] <file>", fastgit_cmd_blame },
    { "grep", "grep", "Print lines matching a pattern", FASTGIT_CMD_GREP, 1, -1, "<pattern> [<path>...]", fastgit_cmd_grep },
    { "ls-files", "ls-files", "Show information about files in the index and the working tree", FASTGIT_CMD_LS_FILES, 0, -1, "[<options>] [<path>...]", fastgit_cmd_ls_files },
    { "ls-tree", "ls-tree", "List the contents of a tree object", FASTGIT_CMD_LS_TREE, 1, -1, "[<options>] <tree-ish> [<path>...]", fastgit_cmd_ls_tree },
    { "cat-file", "cat-file", "Provide content or type and size information for repository objects", FASTGIT_CMD_CAT_FILE, 1, -1, "<object>", fastgit_cmd_cat_file },
    { "hash-object", "hash-object", "Compute object ID and optionally creates a blob from a file", FASTGIT_CMD_HASH_OBJECT, 0, -1, "[-t <type>] [-w] [--stdin] [--] <file>...", fastgit_cmd_hash_object },
    { "write-tree", "write-tree", "Create a tree object from the current index", FASTGIT_CMD_WRITE_TREE, 0, 0, "", fastgit_cmd_write_tree },
    { "read-tree", "read-tree", "Read tree information into the index", FASTGIT_CMD_READ_TREE, 1, -1, "<tree-ish>", fastgit_cmd_read_tree },
    { "commit-tree", "commit-tree", "Create a new commit object", FASTGIT_CMD_COMMIT_TREE, 1, -1, "<tree> [-p <parent>] [-m <message>]", fastgit_cmd_commit_tree },
    { "update-index", "update-index", "Register file contents in the working tree to the index", FASTGIT_CMD_UPDATE_INDEX, 0, -1, "[<options>] [<path>...]", fastgit_cmd_update_index },
    { "config", "config", "Get and set repository or global options", FASTGIT_CMD_CONFIG, 0, -1, "<key> [<value>]", fastgit_cmd_config },
    { "benchmark", "benchmark", "Run performance benchmarks", FASTGIT_CMD_BENCHMARK, 0, -1, "[<benchmark>...]", fastgit_cmd_benchmark },
    { "stats", "stats", "Show repository statistics", FASTGIT_CMD_STATS, 0, 0, "", fastgit_cmd_stats },
    { "doctor", "doctor", "Check repository health", FASTGIT_CMD_DOCTOR, 0, 0, "", fastgit_cmd_doctor },
    { "migrate", "migrate", "Migrate repository to different hash algorithm", FASTGIT_CMD_MIGRATE, 0, -1, "<algorithm>", fastgit_cmd_migrate },
    { "verify", "verify", "Verify repository integrity", FASTGIT_CMD_VERIFY, 0, -1, "[<options>]", fastgit_cmd_verify },
    { "version", "version", "Show version information", FASTGIT_CMD_VERSION, 0, 0, "", fastgit_cmd_version },
};

static size_t builtin_count = sizeof(builtin_commands) / sizeof(builtin_commands[0]);

static fastgit_cmd_def_t* custom_commands = NULL;
static size_t custom_count = 0;
static size_t custom_capacity = 0;

fastgit_error_t fastgit_cli_new(int argc, char** argv, fastgit_cli_t** out) {
    if (!out) return FASTGIT_EINVAL;

    fastgit_cli_t* cli = calloc(1, sizeof(fastgit_cli_t));
    if (!cli) return FASTGIT_ENOMEM;

    cli->argc = argc;
    cli->argv = argv;

    *out = cli;
    return FASTGIT_OK;
}

void fastgit_cli_free(fastgit_cli_t* cli) {
    if (!cli) return;
    free(cli);
}

static int find_command(const char* name) {
    for (size_t i = 0; i < builtin_count; i++) {
        if (strcmp(builtin_commands[i].name, name) == 0 ||
            (builtin_commands[i].short_name && strcmp(builtin_commands[i].short_name, name) == 0)) {
            return (int)builtin_commands[i].cmd;
        }
    }
    for (size_t i = 0; i < custom_count; i++) {
        if (strcmp(custom_commands[i].name, name) == 0 ||
            (custom_commands[i].short_name && strcmp(custom_commands[i].short_name, name) == 0)) {
            return (int)custom_commands[i].cmd;
        }
    }
    return -1;
}

static void print_usage(const char* progname) {
    printf("usage: %s [<options>] <command> [<args>]\n\n", progname);
    printf("Available commands:\n");
    for (size_t i = 0; i < builtin_count; i++) {
        printf("  %-15s %s\n", builtin_commands[i].name, builtin_commands[i].description);
    }
    if (custom_count > 0) {
        printf("\nCustom commands:\n");
        for (size_t i = 0; i < custom_count; i++) {
            printf("  %-15s %s\n", custom_commands[i].name, custom_commands[i].description);
        }
    }
    printf("\nUse '%s <command> --help' for more information on a specific command.\n", progname);
}

int fastgit_cli_run(fastgit_cli_t* cli) {
    if (!cli || cli->argc < 2) {
        print_usage(cli->argv[0]);
        return 1;
    }

    // handle global options like -C <path> before command (git-compatible)
    int arg = 1;
    while (arg < cli->argc && cli->argv[arg][0] == '-') {
        if (strcmp(cli->argv[arg], "-C") == 0 && arg + 1 < cli->argc) {
            if (chdir(cli->argv[arg+1]) != 0) {
                fprintf(stderr, "fatal: cannot chdir to '%s': %s\n", cli->argv[arg+1], strerror(errno));
                return 1;
            }
            arg += 2;
        } else if (strncmp(cli->argv[arg], "-C", 2) == 0 && cli->argv[arg][2] != '\0') {
            const char* p = cli->argv[arg] + 2;
            if (chdir(p) != 0) {
                fprintf(stderr, "fatal: cannot chdir to '%s': %s\n", p, strerror(errno));
                return 1;
            }
            arg += 1;
        } else if (strcmp(cli->argv[arg], "--help") == 0 || strcmp(cli->argv[arg], "-h") == 0) {
            print_usage(cli->argv[0]);
            return 0;
        } else {
            break;
        }
        if (arg >= cli->argc) { print_usage(cli->argv[0]); return 1; }
    }
    if (arg >= cli->argc) { print_usage(cli->argv[0]); return 1; }

    int cmd = find_command(cli->argv[arg]);
    if (cmd < 0) {
        fprintf(stderr, "fastgit: '%s' is not a fastgit command.\n\n", cli->argv[arg]);
        print_usage(cli->argv[0]);
        return 1;
    }

    cli->cmd = (fastgit_cmd_t)cmd;
    cli->cmd_argc = cli->argc - arg - 1;
    cli->cmd_argv = cli->argv + arg + 1;

    // table dispatch via builtin_commands handler
    for (size_t i = 0; i < builtin_count; i++) {
        if (builtin_commands[i].cmd == cli->cmd) {
            if (builtin_commands[i].handler) {
                return builtin_commands[i].handler(cli->cmd_argc, cli->cmd_argv);
            }
            break;
        }
    }
    for (size_t i = 0; i < custom_count; i++) {
        if (custom_commands[i].cmd == cli->cmd) {
            if (custom_commands[i].handler) {
                return custom_commands[i].handler(cli->cmd_argc, cli->cmd_argv);
            }
            break;
        }
    }

    fprintf(stderr, "fastgit: command '%s' not yet implemented\n", cli->argv[arg]);
    return 1;
}

const fastgit_cmd_def_t* fastgit_cli_commands(size_t* count) {
    if (count) *count = builtin_count + custom_count;
    static fastgit_cmd_def_t* all = NULL;
    static size_t all_count = 0;

    if (all_count != builtin_count + custom_count) {
        free(all);
        all = malloc((builtin_count + custom_count) * sizeof(fastgit_cmd_def_t));
        if (all) {
            memcpy(all, builtin_commands, builtin_count * sizeof(fastgit_cmd_def_t));
            if (custom_count > 0) {
                memcpy(all + builtin_count, custom_commands, custom_count * sizeof(fastgit_cmd_def_t));
            }
            all_count = builtin_count + custom_count;
        }
    }
    return all;
}

fastgit_error_t fastgit_cli_register_command(const fastgit_cmd_def_t* cmd) {
    if (!cmd) return FASTGIT_EINVAL;

    if (custom_count >= custom_capacity) {
        custom_capacity = custom_capacity ? custom_capacity * 2 : 8;
        fastgit_cmd_def_t* new_cmds = realloc(custom_commands, custom_capacity * sizeof(fastgit_cmd_def_t));
        if (!new_cmds) return FASTGIT_ENOMEM;
        custom_commands = new_cmds;
    }

    custom_commands[custom_count++] = *cmd;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_cli_unregister_command(fastgit_cmd_t cmd) {
    for (size_t i = 0; i < custom_count; i++) {
        if (custom_commands[i].cmd == cmd) {
            for (size_t j = i; j < custom_count - 1; j++) {
                custom_commands[j] = custom_commands[j + 1];
            }
            custom_count--;
            return FASTGIT_OK;
        }
    }
    return FASTGIT_ENOENT;
}

fastgit_error_t fastgit_config_get(const char* key, char** value) {
    if (!key || !value) return FASTGIT_EINVAL;
    *value = NULL;
    return FASTGIT_ENOENT;
}

fastgit_error_t fastgit_config_set(const char* key, const char* value, const char* scope) {
    (void)key; (void)value; (void)scope;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_config_list(fastgit_config_entry_t** entries, size_t* count) {
    if (!entries || !count) return FASTGIT_EINVAL;
    *entries = NULL;
    *count = 0;
    return FASTGIT_OK;
}

void fastgit_config_free(fastgit_config_entry_t* entries, size_t count) {
    if (!entries) return;
    for (size_t i = 0; i < count; i++) {
        free((void*)entries[i].key);
        free((void*)entries[i].value);
        free((void*)entries[i].source);
    }
    free(entries);
}
