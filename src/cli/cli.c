#include "fastgit/cli.h"
#include "fastgit/fastgit.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include "fastgit/worktree.h"
#include "fastgit/odb.h"
#include "fastgit/hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>

struct fastgit_cli {
    int argc;
    char** argv;
    fastgit_cmd_t cmd;
    int cmd_argc;
    char** cmd_argv;
};

static const fastgit_cmd_def_t builtin_commands[] = {
    { "init", "init", "Create an empty Git repository", FASTGIT_CMD_INIT, 0, 1, "[<directory>]", NULL },
    { "clone", "clone", "Clone a repository into a new directory", FASTGIT_CMD_CLONE, 1, 2, "<repository> [<directory>]", NULL },
    { "add", "add", "Add file contents to the index", FASTGIT_CMD_ADD, 1, -1, "<pathspec>...", NULL },
    { "commit", "commit", "Record changes to the repository", FASTGIT_CMD_COMMIT, 0, -1, "[-m <message>] [<pathspec>...]", NULL },
    { "status", "st", "Show the working tree status", FASTGIT_CMD_STATUS, 0, -1, "[<pathspec>...]", NULL },
    { "diff", "diff", "Show changes between commits, commit and working tree, etc", FASTGIT_CMD_DIFF, 0, -1, "[<options>] [<commit>] [--] [<path>...]", NULL },
    { "log", "log", "Show commit logs", FASTGIT_CMD_LOG, 0, -1, "[<options>] [<revision range>] [[--] <path>...]", NULL },
    { "checkout", "co", "Switch branches or restore working tree files", FASTGIT_CMD_CHECKOUT, 1, -1, "<branch> [<paths>...]", NULL },
    { "branch", "branch", "List, create, or delete branches", FASTGIT_CMD_BRANCH, 0, -1, "[<branchname> [<start-point>]]", NULL },
    { "merge", "merge", "Join two or more development histories together", FASTGIT_CMD_MERGE, 1, -1, "<commit>...", NULL },
    { "rebase", "rebase", "Reapply commits on top of another base tip", FASTGIT_CMD_REBASE, 0, -1, "[<upstream> [<branch>]]", NULL },
    { "fetch", "fetch", "Download objects and refs from another repository", FASTGIT_CMD_FETCH, 0, -1, "[<repository> [<refspec>...]]", NULL },
    { "push", "push", "Update remote refs along with associated objects", FASTGIT_CMD_PUSH, 0, -1, "[<repository> [<refspec>...]]", NULL },
    { "pull", "pull", "Fetch from and integrate with another repository", FASTGIT_CMD_PULL, 0, -1, "[<repository> [<refspec>...]]", NULL },
    { "remote", "remote", "Manage set of tracked repositories", FASTGIT_CMD_REMOTE, 0, -1, "<subcommand>...", NULL },
    { "tag", "tag", "Create, list, delete or verify a tag object", FASTGIT_CMD_TAG, 0, -1, "[-a|-s|-u <key-id>] [-m <msg>] [<tagname> [<commit>]]", NULL },
    { "reset", "reset", "Reset current HEAD to the specified state", FASTGIT_CMD_RESET, 0, -1, "[<mode>] [<commit>]", NULL },
    { "rm", "rm", "Remove files from the working tree and from the index", FASTGIT_CMD_RM, 1, -1, "[-f] [-r] [--] <pathspec>...", NULL },
    { "mv", "mv", "Move or rename a file, a directory, or a symlink", FASTGIT_CMD_MV, 2, -1, "<source> <destination>", NULL },
    { "stash", "stash", "Stash the changes in a dirty working directory", FASTGIT_CMD_STASH, 0, -1, "<subcommand>...", NULL },
    { "show", "show", "Show various types of objects", FASTGIT_CMD_SHOW, 0, -1, "[<object>...]", NULL },
    { "blame", "blame", "Show what revision and author last modified each line of a file", FASTGIT_CMD_BLAME, 0, -1, "[<options>] <file>", NULL },
    { "grep", "grep", "Print lines matching a pattern", FASTGIT_CMD_GREP, 1, -1, "<pattern> [<path>...]", NULL },
    { "ls-files", "ls-files", "Show information about files in the index and the working tree", FASTGIT_CMD_LS_FILES, 0, -1, "[<options>] [<path>...]", NULL },
    { "ls-tree", "ls-tree", "List the contents of a tree object", FASTGIT_CMD_LS_TREE, 1, -1, "[<options>] <tree-ish> [<path>...]", NULL },
    { "cat-file", "cat-file", "Provide content or type and size information for repository objects", FASTGIT_CMD_CAT_FILE, 1, -1, "<object>", NULL },
    { "hash-object", "hash-object", "Compute object ID and optionally creates a blob from a file", FASTGIT_CMD_HASH_OBJECT, 0, -1, "[-t <type>] [-w] [--stdin] [--] <file>...", NULL },
    { "write-tree", "write-tree", "Create a tree object from the current index", FASTGIT_CMD_WRITE_TREE, 0, 0, "", NULL },
    { "read-tree", "read-tree", "Read tree information into the index", FASTGIT_CMD_READ_TREE, 1, -1, "<tree-ish>", NULL },
    { "commit-tree", "commit-tree", "Create a new commit object", FASTGIT_CMD_COMMIT_TREE, 1, -1, "<tree> [-p <parent>] [-m <message>]", NULL },
    { "update-index", "update-index", "Register file contents in the working tree to the index", FASTGIT_CMD_UPDATE_INDEX, 0, -1, "[<options>] [<path>...]", NULL },
    { "config", "config", "Get and set repository or global options", FASTGIT_CMD_CONFIG, 0, -1, "<key> [<value>]", NULL },
    { "benchmark", "benchmark", "Run performance benchmarks", FASTGIT_CMD_BENCHMARK, 0, -1, "[<benchmark>...]", NULL },
    { "stats", "stats", "Show repository statistics", FASTGIT_CMD_STATS, 0, 0, "", NULL },
    { "doctor", "doctor", "Check repository health", FASTGIT_CMD_DOCTOR, 0, 0, "", NULL },
    { "migrate", "migrate", "Migrate repository to different hash algorithm", FASTGIT_CMD_MIGRATE, 0, -1, "<algorithm>", NULL },
    { "verify", "verify", "Verify repository integrity", FASTGIT_CMD_VERIFY, 0, -1, "[<options>]", NULL },
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

    int cmd = find_command(cli->argv[1]);
    if (cmd < 0) {
        fprintf(stderr, "fastgit: '%s' is not a fastgit command.\n\n", cli->argv[1]);
        print_usage(cli->argv[0]);
        return 1;
    }

    cli->cmd = (fastgit_cmd_t)cmd;
    cli->cmd_argc = cli->argc - 2;
    cli->cmd_argv = cli->argv + 2;

    switch (cli->cmd) {
        case FASTGIT_CMD_INIT: {
            const char* path = cli->cmd_argc > 0 ? cli->cmd_argv[0] : ".";
            fastgit_repository_t* repo;
            fastgit_error_t err = fastgit_repository_init(path, false, &repo);
            if (err != FASTGIT_OK) {
                fprintf(stderr, "fatal: could not create repository: %s\n", fastgit_error_string(err));
                return 1;
            }
            fastgit_repository_free(repo);
            printf("Initialized empty fastgit repository in %s/.git/\n", path);
            return 0;
        }
        case FASTGIT_CMD_HASH_OBJECT: {
            // parse -t <type> -w --stdin
            const char* type_name = "blob";
            bool write = false;
            bool use_stdin = false;
            int file_start = 0;
            for (int i = 0; i < cli->cmd_argc; i++) {
                if (strcmp(cli->cmd_argv[i], "-w") == 0) write = true;
                else if (strcmp(cli->cmd_argv[i], "--stdin") == 0) use_stdin = true;
                else if (strcmp(cli->cmd_argv[i], "-t") == 0 && i+1 < cli->cmd_argc) { type_name = cli->cmd_argv[i+1]; i++; }
                else if (cli->cmd_argv[i][0]=='-') { /* ignore unknown */ }
                else { file_start = i; break; }
            }
            // collect files (or stdin)
            int nfiles = use_stdin ? 1 : (cli->cmd_argc - file_start);
            if (nfiles <= 0 && !use_stdin) {
                fprintf(stderr, "usage: fastgit hash-object [-t <type>] [-w] [--stdin] [--] <file>...\n");
                return 1;
            }
            fastgit_obj_type_t type = fastgit_obj_type_from_name(type_name);
            fastgit_repository_t* repo = NULL;
            fastgit_odb_t* odb = NULL;
            if (write) {
                if (fastgit_repository_open(".", &repo) != FASTGIT_OK) {
                    // try init location fallback: use .git directly if not found but -w requires repo
                    char gitdir[4096];
                    if (fastgit_repository_init(".", false, &repo) != FASTGIT_OK) {
                        fprintf(stderr, "fatal: not a fastgit repository (and -w requires one)\n");
                        return 1;
                    }
                }
                odb = fastgit_repository_odb(repo);
            }
            int ret = 0;
            for (int fi = 0; fi < nfiles; fi++) {
                const char* path = use_stdin ? "-" : cli->cmd_argv[file_start + fi];
                void* data = NULL; size_t len = 0;
                if (use_stdin) {
                    size_t cap = 8192; data = malloc(cap); len = 0;
                    size_t n; while ((n = fread((char*)data+len,1,cap-len,stdin))>0) { len+=n; if(len==cap){ cap*=2; data=realloc(data,cap); } }
                    if (!data) { ret=1; break; }
                } else {
                    FILE* f = fopen(path, "rb");
                    if (!f) { fprintf(stderr, "fatal: could not open '%s'\n", path); ret=1; continue; }
                    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
                    if (sz<0) sz=0; len=(size_t)sz; data=malloc(len?len:1);
                    if (len) fread(data,1,len,f); fclose(f);
                }
                fastgit_object_t* obj=NULL;
                fastgit_error_t err = fastgit_object_parse(type, data, len, &obj);
                free(data);
                if (err!=FASTGIT_OK) { fprintf(stderr,"error: parse failed\n"); ret=1; continue; }
                fastgit_oid_t oid;
                err = fastgit_object_hash(obj, FASTGIT_HASH_SHA256, &oid);
                if (err!=FASTGIT_OK) { fastgit_object_free(obj); ret=1; continue; }
                if (write && odb) {
                    fastgit_oid_t woid;
                    fastgit_odb_write(odb, type, obj->data, obj->size, &woid);
                    // woid should equal oid
                }
                char hex[129]; fastgit_oid_to_hex(&oid, hex, sizeof(hex));
                printf("%s\n", hex);
                fastgit_object_free(obj);
            }
            if (repo) fastgit_repository_free(repo);
            return ret;
        }
        case FASTGIT_CMD_CAT_FILE: {
            if (cli->cmd_argc < 1) {
                fprintf(stderr, "usage: fastgit cat-file [-p|-t|-s] <object>\n");
                return 1;
            }
            const char* opt = NULL; const char* objname = NULL;
            for (int i=0;i<cli->cmd_argc;i++) {
                if (cli->cmd_argv[i][0]=='-') opt=cli->cmd_argv[i];
                else objname=cli->cmd_argv[i];
            }
            if (!objname) { fprintf(stderr,"fatal: need object\n"); return 1; }
            fastgit_repository_t* repo=NULL;
            if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a fastgit repository\n"); return 1; }
            fastgit_oid_t oid;
            if (fastgit_oid_from_hex(objname,&oid)!=FASTGIT_OK) { fprintf(stderr,"fatal: invalid object name %s\n",objname); fastgit_repository_free(repo); return 1; }
            fastgit_odb_object_t o;
            fastgit_error_t err = fastgit_odb_read(fastgit_repository_odb(repo), &oid, &o);
            if (err!=FASTGIT_OK) { fprintf(stderr,"fatal: object %s not found\n",objname); fastgit_repository_free(repo); return 1; }
            if (opt && strcmp(opt,"-t")==0) printf("%s\n", fastgit_obj_type_name(o.type));
            else if (opt && strcmp(opt,"-s")==0) printf("%zu\n", o.size);
            else fwrite(o.data,1,o.size,stdout);
            free(o.data); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_STATUS: {
            fastgit_repository_t* repo;
            fastgit_error_t err = fastgit_repository_open(".", &repo);
            if (err != FASTGIT_OK) {
                fprintf(stderr, "fatal: not a fastgit repository: %s\n", fastgit_error_string(err));
                return 1;
            }
            fastgit_worktree_t* wt = fastgit_repository_worktree(repo);
            fastgit_status_entry_t* entries;
            size_t count;
            err = fastgit_status(wt, &entries, &count);
            if (err != FASTGIT_OK) {
                fprintf(stderr, "error: status failed: %s\n", fastgit_error_string(err));
                fastgit_repository_free(repo);
                return 1;
            }
            for (size_t i = 0; i < count; i++) {
                const char* status_str = "";
                switch (entries[i].worktree_status) {
                    case FASTGIT_STATUS_WT_NEW: status_str = "??"; break;
                    case FASTGIT_STATUS_WT_MODIFIED: status_str = " M"; break;
                    case FASTGIT_STATUS_WT_DELETED: status_str = " D"; break;
                    default: status_str = "  "; break;
                }
                printf("%s %s\n", status_str, entries[i].path);
            }
            fastgit_status_free(entries, count);
            fastgit_repository_free(repo);
            return 0;
        }
        case FASTGIT_CMD_BENCHMARK: {
            printf("Running benchmarks...\n");
            fastgit_stats_t stats = fastgit_stats_get();
            printf("Objects read: %llu\n", stats.objects_read);
            printf("Objects written: %llu\n", stats.objects_written);
            printf("Bytes read: %llu\n", stats.bytes_read);
            printf("Bytes written: %llu\n", stats.bytes_written);
            printf("CPU time: %.2f ms\n", stats.cpu_time_ms);
            printf("Wall time: %.2f ms\n", stats.wall_time_ms);
            return 0;
        }
        case FASTGIT_CMD_STATS: {
            fastgit_stats_t stats = fastgit_stats_get();
            printf("fastgit statistics:\n");
            printf("  Objects read:    %llu\n", stats.objects_read);
            printf("  Objects written: %llu\n", stats.objects_written);
            printf("  Bytes read:      %llu\n", stats.bytes_read);
            printf("  Bytes written:   %llu\n", stats.bytes_written);
            printf("  CPU time:        %.2f ms\n", stats.cpu_time_ms);
            printf("  Wall time:       %.2f ms\n", stats.wall_time_ms);
            return 0;
        }
        case FASTGIT_CMD_VERSION: {
            printf("fastgit version %s\n", fastgit_version());
            return 0;
        }
        default: {
            fprintf(stderr, "fastgit: command '%s' not yet implemented\n", cli->argv[1]);
            return 1;
        }
    }
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
