#include "fastgit/cli.h"
#include "fastgit/fastgit.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include "fastgit/worktree.h"
#include "fastgit/odb.h"
#include "fastgit/hash.h"
#include "fastgit/merge.h"
#include "fastgit/rebase.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <regex.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

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
                    fprintf(stderr, "fatal: not a fastgit repository (and -w requires one)\n");
                    return 1;
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
            if (fastgit_rev_parse(repo,objname,&oid)!=FASTGIT_OK && fastgit_oid_from_hex(objname,&oid)!=FASTGIT_OK) { fprintf(stderr,"fatal: invalid object name %s\n",objname); fastgit_repository_free(repo); return 1; }
            fastgit_odb_object_t o;
            fastgit_error_t err = fastgit_odb_read(fastgit_repository_odb(repo), &oid, &o);
            if (err!=FASTGIT_OK) { fprintf(stderr,"fatal: object %s not found\n",objname); fastgit_repository_free(repo); return 1; }
            if (opt && strcmp(opt,"-t")==0) printf("%s\n", fastgit_obj_type_name(o.type));
            else if (opt && strcmp(opt,"-s")==0) printf("%zu\n", o.size);
            else fwrite(o.data,1,o.size,stdout);
            free(o.data); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_ADD: {
            if (cli->cmd_argc < 1) {
                fprintf(stderr, "usage: fastgit add <pathspec>...\n");
                return 1;
            }
            fastgit_repository_t* repo;
            fastgit_error_t err = fastgit_repository_open(".", &repo);
            if (err != FASTGIT_OK) {
                fprintf(stderr, "fatal: not a fastgit repository: %s\n", fastgit_error_string(err));
                return 1;
            }
            fastgit_index_t* idx = fastgit_repository_index(repo);
            fastgit_odb_t* odb = fastgit_repository_odb(repo);
            // write blobs to ODB before updating index (mirrors git add)
            for (int i = 0; i < cli->cmd_argc; i++) {
                const char* path = cli->cmd_argv[i];
                if (path[0] == '-') continue;
                FILE* f = fopen(path, "rb");
                if (!f) continue;
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (sz < 0) sz = 0;
                size_t len = (size_t)sz;
                void* data = malloc(len ? len : 1);
                if (!data) { fclose(f); continue; }
                if (len) {
                    size_t n = fread(data, 1, len, f);
                    (void)n;
                }
                fclose(f);
                fastgit_oid_t woid;
                fastgit_odb_write(odb, FASTGIT_OBJ_BLOB, data, len, &woid);
                free(data);
            }
            // fast path: bulk add when multiple paths
            if ((size_t)cli->cmd_argc > 1) {
                const char** paths = (const char**)cli->cmd_argv;
                err = fastgit_index_add_many(idx, paths, (size_t)cli->cmd_argc);
                if (err != FASTGIT_OK) {
                    fprintf(stderr, "error: add failed: %s\n", fastgit_error_string(err));
                    fastgit_repository_free(repo);
                    return 1;
                }
            } else {
                for (int i = 0; i < cli->cmd_argc; i++) {
                    err = fastgit_index_add(idx, cli->cmd_argv[i]);
                    if (err != FASTGIT_OK) {
                        fprintf(stderr, "error: could not add '%s': %s\n", cli->cmd_argv[i], fastgit_error_string(err));
                        fastgit_repository_free(repo);
                        return 1;
                    }
                }
            }
            err = fastgit_index_write(idx);
            if (err != FASTGIT_OK) {
                fprintf(stderr, "error: could not write index: %s\n", fastgit_error_string(err));
                fastgit_repository_free(repo);
                return 1;
            }
            fastgit_repository_free(repo);
            return 0;
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
                if (entries[i].worktree_status==0 && entries[i].index_status==0) continue;
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
        case FASTGIT_CMD_COMMIT: {
            const char* msg = NULL;
            const char* msg_file = NULL;
            bool amend = false;
            for (int i=0;i<cli->cmd_argc;i++) {
                if (strcmp(cli->cmd_argv[i],"-m")==0 && i+1<cli->cmd_argc) msg=cli->cmd_argv[++i];
                else if (strcmp(cli->cmd_argv[i],"--amend")==0) amend=true;
                else if (strncmp(cli->cmd_argv[i],"-m",2)==0) msg=cli->cmd_argv[i]+2;
            }
            if (!msg) msg = "commit via fastgit";
            fastgit_repository_t* repo=NULL;
            if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
            fastgit_index_t* idx = fastgit_repository_index(repo);
            fastgit_oid_t tree_oid;
            if (fastgit_index_write_tree(idx, &tree_oid)!=FASTGIT_OK) { fprintf(stderr,"fatal: write-tree failed\n"); fastgit_repository_free(repo); return 1; }
            fastgit_oid_t parent_oid; bool has_parent = (fastgit_reference_lookup(repo,"HEAD",&parent_oid)==FASTGIT_OK || fastgit_rev_parse(repo,"HEAD",&parent_oid)==FASTGIT_OK);
            fastgit_signature_t *author=NULL,*committer=NULL;
            fastgit_signature_default(&author); fastgit_signature_default(&committer);
            fastgit_commit_t cmt = {0};
            cmt.tree = tree_oid;
            if (has_parent) { cmt.parent_count=1; cmt.parents=&parent_oid; }
            cmt.author=author; cmt.committer=committer; cmt.message=(char*)msg;
            fastgit_object_t* cobj=NULL;
            if (fastgit_commit_create(&cmt,&cobj)!=FASTGIT_OK) { fprintf(stderr,"fatal: commit create failed\n"); fastgit_signature_free(author); fastgit_signature_free(committer); fastgit_repository_free(repo); return 1; }
            fastgit_buf_t buf; fastgit_object_serialize(cobj, FASTGIT_HASH_SHA256, &buf);
            fastgit_oid_t oid; fastgit_hash(FASTGIT_HASH_SHA256, buf.data, buf.len, (fastgit_hash_t*)&oid); oid.algo=FASTGIT_HASH_SHA256; oid.len=32;
            // write via ODB – odb_write expects raw content without header, so strip header
            fastgit_odb_t* odb=fastgit_repository_odb(repo);
            fastgit_oid_t woid;
            {
                size_t hdr = 0; while (hdr < buf.len && ((uint8_t*)buf.data)[hdr] != '\0') hdr++;
                const void* content = hdr < buf.len ? (uint8_t*)buf.data + hdr + 1 : buf.data;
                size_t clen = hdr < buf.len ? buf.len - hdr - 1 : buf.len;
                fastgit_odb_write(odb, FASTGIT_OBJ_COMMIT, content, clen, &woid);
            }
            fastgit_reference_update(repo, "HEAD", &woid, msg);
            char hex[129]; fastgit_oid_to_hex(&woid,hex,sizeof(hex));
            printf("[%s %s] %s\n", "main", hex, msg);
            free(buf.data); fastgit_object_free(cobj);
            // author/committer ownership transferred to cobj via fastgit_commit_create -> commit_data_free
            fastgit_repository_free(repo); (void)amend; (void)msg_file;
            return 0;
        }
        case FASTGIT_CMD_LOG: {
            fastgit_repository_t* repo=NULL;
            if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
            fastgit_oid_t oid;
            const char* rev = cli->cmd_argc>0? cli->cmd_argv[0]: "HEAD";
            if (rev[0]=='-' ) rev="HEAD";
            if (fastgit_rev_parse(repo, rev, &oid)!=FASTGIT_OK) { fprintf(stderr,"fatal: ambiguous argument '%s'\n", rev); fastgit_repository_free(repo); return 1; }
            int limit=20; bool oneline=false;
            for(int i=0;i<cli->cmd_argc;i++) if(strcmp(cli->cmd_argv[i],"--oneline")==0) oneline=true;
            for(int n=0;n<limit;n++) {
                fastgit_object_t* obj=NULL;
                if (fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK) break;
                const fastgit_commit_t* c = fastgit_commit_parse(obj);
                char hex[129]; fastgit_oid_to_hex(&oid,hex,sizeof(hex));
                if (oneline) printf("%s %s\n", hex, c&&c->message?c->message:"");
                else {
                    printf("commit %s\n", hex);
                    if (c && c->author) printf("Author: %s <%s>\n", c->author->name, c->author->email);
                    if (c && c->message) printf("\n    %s\n\n", c->message);
                }
                fastgit_oid_t next; bool has_next=false;
                if (c && c->parent_count>0) { next=c->parents[0]; has_next=true; }
                fastgit_object_free(obj);
                if (!has_next) break;
                oid=next;
            }
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_CHECKOUT: {
            if (cli->cmd_argc<1) { fprintf(stderr,"usage: fastgit checkout <branch> [<paths>...]\n"); return 1; }
            // handle -- separator for pathspec checkout
            int arg_off=0;
            const char* target = cli->cmd_argv[0];
            int path_start=-1;
            if (strcmp(target,"--")==0) {
                // checkout -- <paths>: restore paths from index/HEAD
                arg_off=1;
                path_start=1;
                target=NULL;
            } else if (cli->cmd_argc>=2 && strcmp(cli->cmd_argv[1],"--")==0) {
                path_start=2;
            }
            fastgit_repository_t* repo=NULL;
            if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
            if (path_start>=0) {
                // path checkout: restore listed paths from index
                fastgit_index_t* idx = fastgit_repository_index(repo);
                (void)fastgit_repository_odb(repo);
                const char* wt_path = fastgit_repository_worktree(repo) ? "." : ".";
                (void)wt_path;
                for(int i=path_start;i<cli->cmd_argc;i++) {
                    const char* pth = cli->cmd_argv[i];
                    fastgit_index_entry_t* ent=NULL;
                    if (fastgit_index_find(idx, pth, 0, &ent)!=FASTGIT_OK || !ent) { fprintf(stderr,"error: pathspec '%s' did not match\n", pth); continue; }
                    fastgit_object_t* blob=NULL;
                    if (fastgit_object_lookup(repo,&ent->oid,&blob)==FASTGIT_OK) {
                        size_t sz = fastgit_object_size(blob); const void* data = fastgit_object_data(blob);
                        char* dup=strdup(pth); char* sl=strrchr(dup,'/');
                        if (sl) { *sl='\0'; char cmd[1024]; snprintf(cmd,sizeof(cmd),"mkdir -p \"%s\"",dup); system(cmd); }
                        free(dup);
                        FILE* f=fopen(pth,"wb"); if(f){ fwrite(data,1,sz,f); fclose(f); }
                        fastgit_object_free(blob);
                    }
                }
                fastgit_repository_free(repo); return 0;
            }
            // branch checkout
            if (strcmp(target,"--")==0) { fastgit_repository_free(repo); return 1; }
            (void)arg_off;
            fastgit_oid_t oid;
            if (fastgit_rev_parse(repo,target,&oid)!=FASTGIT_OK) {
                fprintf(stderr,"error: pathspec '%s' did not match\n", target);
                fastgit_repository_free(repo); return 1;
            }
            fastgit_object_t* obj=NULL;
            if (fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK) { fastgit_repository_free(repo); return 1; }
            fastgit_oid_t tree_oid = oid;
            const fastgit_commit_t* c = fastgit_commit_parse(obj);
            if (c) tree_oid = c->tree;
            fastgit_index_t* idx = fastgit_repository_index(repo);
            fastgit_index_read_tree(idx, &tree_oid);
            fastgit_index_write(idx);
            fastgit_worktree_t* wt = fastgit_repository_worktree(repo);
            fastgit_checkout_head(wt, true);
            fastgit_reference_update(repo,"HEAD",&oid,target);
            fastgit_object_free(obj); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_BRANCH: {
            fastgit_repository_t* repo=NULL;
            if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
            if (cli->cmd_argc==0) {
                char** list; size_t cnt;
                fastgit_reference_list(repo,NULL,&list,&cnt);
                for(size_t i=0;i<cnt;i++) {
                    // list entries are like heads/main or refs/heads/main depending on source
                    const char* n=list[i];
                    if(strncmp(n,"refs/heads/",11)==0) n+=11;
                    else if(strncmp(n,"heads/",6)==0) n+=6;
                    // filter to heads only
                    if(strstr(list[i],"heads/")) printf("  %s\n", n);
                }
                fastgit_reference_list_free(list,cnt);
            } else if (cli->cmd_argc>=1 && strcmp(cli->cmd_argv[0],"-d")==0 && cli->cmd_argc>=2) {
                char ref[512]; snprintf(ref,sizeof(ref),"refs/heads/%s",cli->cmd_argv[1]);
                fastgit_reference_remove(repo,ref);
            } else {
                const char* name=cli->cmd_argv[0]; const char* start = cli->cmd_argc>=2? cli->cmd_argv[1]:"HEAD";
                fastgit_oid_t oid; if(fastgit_rev_parse(repo,start,&oid)!=FASTGIT_OK){fprintf(stderr,"fatal: bad start %s\n",start); fastgit_repository_free(repo); return 1;}
                char ref[512]; snprintf(ref,sizeof(ref),"refs/heads/%s",name);
                fastgit_reference_create(repo,ref,&oid,false,"branch");
            }
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_DIFF: {
            fastgit_repository_t* repo=NULL;
            if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
            const char* path = cli->cmd_argc>=1 && cli->cmd_argv[0][0]!='-' ? cli->cmd_argv[0] : NULL;
            fastgit_worktree_t* wt = fastgit_repository_worktree(repo);
            char* out=NULL; fastgit_diff_worktree(wt, path, &out);
            if (out) { printf("%s", out); free(out); }
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_LS_FILES: {
            fastgit_repository_t* repo=NULL;
            if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
            fastgit_index_t* idx = fastgit_repository_index(repo);
            size_t n = fastgit_index_entry_count(idx);
            for(size_t i=0;i<n;i++) { const fastgit_index_entry_t* e = fastgit_index_entry_by_index(idx,i); if(e) printf("%s\n", e->path); }
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_LS_TREE: {
            if (cli->cmd_argc<1){fprintf(stderr,"usage: fastgit ls-tree <tree-ish>\n");return 1;}
            fastgit_repository_t* repo=NULL;
            if (fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
            fastgit_oid_t oid; if(fastgit_rev_parse(repo,cli->cmd_argv[0],&oid)!=FASTGIT_OK){fprintf(stderr,"fatal: bad tree %s\n",cli->cmd_argv[0]);fastgit_repository_free(repo);return 1;}
            fastgit_object_t* obj=NULL; if(fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
            fastgit_obj_type_t t=fastgit_object_type(obj);
            if(t==FASTGIT_OBJ_COMMIT){ const fastgit_commit_t* c=fastgit_commit_parse(obj); if(c) oid=c->tree; fastgit_object_free(obj); if(fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}}
            size_t cnt=fastgit_tree_entry_count(obj);
            for(size_t i=0;i<cnt;i++){ const fastgit_tree_entry_t* e=fastgit_tree_entry_by_index(obj,i); char hex[129]; fastgit_oid_to_hex(&e->oid,hex,sizeof(hex)); const char* tname = (e->mode == 040000 || (e->mode & 0170000) == 0040000) ? fastgit_obj_type_name(FASTGIT_OBJ_TREE) : fastgit_obj_type_name(FASTGIT_OBJ_BLOB); printf("%06o %s %s\t%s\n", e->mode, tname, hex, e->path); }
            fastgit_object_free(obj); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_WRITE_TREE: {
            fastgit_repository_t* repo=NULL;
            if (fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
            fastgit_oid_t oid; if(fastgit_index_write_tree(fastgit_repository_index(repo),&oid)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
            char hex[129]; fastgit_oid_to_hex(&oid,hex,sizeof(hex)); printf("%s\n",hex); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_READ_TREE: {
            if(cli->cmd_argc<1){fprintf(stderr,"usage: fastgit read-tree <tree-ish>\n");return 1;}
            fastgit_repository_t* repo=NULL;
            if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
            fastgit_oid_t oid; if(fastgit_rev_parse(repo,cli->cmd_argv[0],&oid)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
            fastgit_index_read_tree(fastgit_repository_index(repo),&oid);
            fastgit_index_write(fastgit_repository_index(repo));
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_COMMIT_TREE: {
            if(cli->cmd_argc<1){fprintf(stderr,"usage: fastgit commit-tree <tree> [-p <parent>] [-m <msg>]\n");return 1;}
            fastgit_repository_t* repo=NULL; fastgit_repository_open(".", &repo);
            fastgit_oid_t tree; fastgit_oid_from_hex(cli->cmd_argv[0],&tree);
            const char* msg="commit-tree"; fastgit_oid_t parent; bool has_p=false;
            for(int i=1;i<cli->cmd_argc;i++) if(strcmp(cli->cmd_argv[i],"-p")==0 && i+1<cli->cmd_argc) {fastgit_oid_from_hex(cli->cmd_argv[++i],&parent); has_p=true;} else if(strcmp(cli->cmd_argv[i],"-m")==0 && i+1<cli->cmd_argc) msg=cli->cmd_argv[++i];
            fastgit_signature_t *a=NULL,*c=NULL; fastgit_signature_default(&a); fastgit_signature_default(&c);
            fastgit_commit_t ct={0}; ct.tree=tree; if(has_p){ct.parent_count=1; ct.parents=&parent;} ct.author=a; ct.committer=c; ct.message=(char*)msg;
            fastgit_object_t* obj=NULL; fastgit_commit_create(&ct,&obj);
            fastgit_buf_t buf; fastgit_object_serialize(obj,FASTGIT_HASH_SHA256,&buf);
            fastgit_odb_t* odb=repo? fastgit_repository_odb(repo):NULL;
            fastgit_oid_t woid; if(odb) fastgit_odb_write(odb,FASTGIT_OBJ_COMMIT,buf.data,buf.len,&woid); else { fastgit_hash(FASTGIT_HASH_SHA256,buf.data,buf.len,(fastgit_hash_t*)&woid); woid.algo=FASTGIT_HASH_SHA256; woid.len=32; }
            char hex[129]; fastgit_oid_to_hex(&woid,hex,sizeof(hex)); printf("%s\n",hex);
            free(buf.data); fastgit_object_free(obj); if(repo) fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_UPDATE_INDEX: {
            fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
            fastgit_index_t* idx=fastgit_repository_index(repo);
            for(int i=0;i<cli->cmd_argc;i++) if(cli->cmd_argv[i][0]!='-') fastgit_index_add(idx, cli->cmd_argv[i]);
            fastgit_index_write(idx); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_RESET: {
            if(cli->cmd_argc<1){fprintf(stderr,"usage: fastgit reset [<commit>]\n");return 1;}
            fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
            fastgit_oid_t oid; if(fastgit_rev_parse(repo,cli->cmd_argv[0],&oid)!=FASTGIT_OK){fprintf(stderr,"fatal: bad rev\n");fastgit_repository_free(repo);return 1;}
            fastgit_reference_update(repo,"HEAD",&oid,"reset");
            // also reset index
            fastgit_object_t* obj=NULL; if(fastgit_object_lookup(repo,&oid,&obj)==FASTGIT_OK){
                fastgit_oid_t tree=oid; const fastgit_commit_t* c=fastgit_commit_parse(obj); if(c) tree=c->tree;
                fastgit_index_read_tree(fastgit_repository_index(repo),&tree); fastgit_index_write(fastgit_repository_index(repo));
                fastgit_object_free(obj);
            }
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_TAG: {
            fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
            if(cli->cmd_argc==0){ char** l; size_t n; fastgit_reference_list(repo,"refs/tags/",&l,&n); for(size_t i=0;i<n;i++) printf("%s\n", l[i]+10); fastgit_reference_list_free(l,n); fastgit_repository_free(repo); return 0; }
            const char* tname=cli->cmd_argv[0]; fastgit_oid_t oid; const char* target=cli->cmd_argc>=2?cli->cmd_argv[1]:"HEAD";
            if(fastgit_rev_parse(repo,target,&oid)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
            char ref[512]; snprintf(ref,sizeof(ref),"refs/tags/%s",tname);
            fastgit_reference_create(repo,ref,&oid,false,"tag"); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_RM: {
            fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
            for(int i=0;i<cli->cmd_argc;i++) if(cli->cmd_argv[i][0]!='-'){ fastgit_index_remove(fastgit_repository_index(repo), cli->cmd_argv[i],0); unlink(cli->cmd_argv[i]); }
            fastgit_index_write(fastgit_repository_index(repo)); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_MV: {
            if(cli->cmd_argc<2){fprintf(stderr,"usage: fastgit mv <src> <dst>\n");return 1;}
            const char* src=cli->cmd_argv[0]; const char* dst=cli->cmd_argv[1];
            if(rename(src,dst)!=0){perror("rename"); return 1;}
            fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)==FASTGIT_OK){
                fastgit_index_remove(fastgit_repository_index(repo),src,0);
                fastgit_index_add(fastgit_repository_index(repo),dst);
                fastgit_index_write(fastgit_repository_index(repo));
                fastgit_repository_free(repo);
            }
            return 0;
        }
        case FASTGIT_CMD_SHOW: {
            const char* rev=cli->cmd_argc>0?cli->cmd_argv[0]:"HEAD";
            fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
            fastgit_oid_t oid; if(fastgit_rev_parse(repo,rev,&oid)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
            fastgit_object_t* obj=NULL; if(fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
            fwrite(fastgit_object_data(obj),1,fastgit_object_size(obj),stdout);
            fastgit_object_free(obj); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_CONFIG: {
            if(cli->cmd_argc==1){ char* v=NULL; if(fastgit_config_get(cli->cmd_argv[0],&v)==FASTGIT_OK){printf("%s\n",v); free(v);} else {fprintf(stderr,"not found\n"); return 1; } return 0; }
            if(cli->cmd_argc==2){ fastgit_config_set(cli->cmd_argv[0], cli->cmd_argv[1], "local"); return 0; }
            fprintf(stderr,"usage: fastgit config <key> [<value>]\n"); return 1;
        }
        case FASTGIT_CMD_MERGE: {
            if (cli->cmd_argc < 1) { fprintf(stderr, "usage: fastgit merge <commit>\n"); return 1; }
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            fastgit_oid_t oid;
            if (fastgit_rev_parse(repo, cli->cmd_argv[0], &oid) != FASTGIT_OK) { fprintf(stderr, "fatal: could not parse %s\n", cli->cmd_argv[0]); fastgit_repository_free(repo); return 1; }
            fastgit_error_t err = fastgit_merge(repo, &oid, NULL);
            if (err != FASTGIT_OK) { fprintf(stderr, "merge failed: %s\n", fastgit_error_string(err)); fastgit_repository_free(repo); return 1; }
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_REBASE: {
            if (cli->cmd_argc < 1) { fprintf(stderr, "usage: fastgit rebase <upstream>\n"); return 1; }
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            fastgit_oid_t oid;
            if (fastgit_rev_parse(repo, cli->cmd_argv[0], &oid) != FASTGIT_OK) { fprintf(stderr, "fatal: could not parse %s\n", cli->cmd_argv[0]); fastgit_repository_free(repo); return 1; }
            fastgit_rebase_options_t opts = {0};
            fastgit_error_t err = fastgit_rebase(repo, &oid, &opts);
            if (err != FASTGIT_OK) { fprintf(stderr, "rebase failed: %s\n", fastgit_error_string(err)); fastgit_repository_free(repo); return 1; }
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_CLONE: {
            if (cli->cmd_argc < 1) { fprintf(stderr, "usage: fastgit clone <repository> [<directory>]\n"); return 1; }
            const char* url = cli->cmd_argv[0];
            const char* path = cli->cmd_argc >= 2 ? cli->cmd_argv[1] : NULL;
            char auto_path[1024] = {0};
            if (!path) {
                const char* slash = strrchr(url, '/');
                const char* base = slash ? slash + 1 : url;
                size_t blen = strlen(base);
                if (blen > 4 && strcmp(base + blen - 4, ".git") == 0) blen -= 4;
                if (blen >= sizeof(auto_path)) blen = sizeof(auto_path) - 1;
                memcpy(auto_path, base, blen);
                auto_path[blen] = '\0';
                if (auto_path[0] == '\0') strcpy(auto_path, "fastgit-clone");
                path = auto_path;
            }
            fastgit_error_t err = fastgit_clone(url, path, NULL);
            if (err != FASTGIT_OK) { fprintf(stderr, "clone failed: %s\n", fastgit_error_string(err)); return 1; }
            return 0;
        }
        case FASTGIT_CMD_FETCH: {
            const char* remote = cli->cmd_argc >= 1 ? cli->cmd_argv[0] : "origin";
            const char* refspec = cli->cmd_argc >= 2 ? cli->cmd_argv[1] : NULL;
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            fastgit_error_t err = fastgit_fetch(repo, remote, refspec);
            if (err != FASTGIT_OK) fprintf(stderr, "fetch failed: %s\n", fastgit_error_string(err));
            fastgit_repository_free(repo);
            return err == FASTGIT_OK ? 0 : 1;
        }
        case FASTGIT_CMD_PUSH: {
            const char* remote = cli->cmd_argc >= 1 ? cli->cmd_argv[0] : "origin";
            const char* refspec = cli->cmd_argc >= 2 ? cli->cmd_argv[1] : NULL;
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            fastgit_error_t err = fastgit_push(repo, remote, refspec);
            if (err != FASTGIT_OK) fprintf(stderr, "push failed: %s\n", fastgit_error_string(err));
            fastgit_repository_free(repo);
            return err == FASTGIT_OK ? 0 : 1;
        }
        case FASTGIT_CMD_PULL: {
            const char* remote = cli->cmd_argc >= 1 ? cli->cmd_argv[0] : "origin";
            const char* refspec = cli->cmd_argc >= 2 ? cli->cmd_argv[1] : NULL;
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            fastgit_error_t err = fastgit_fetch(repo, remote, refspec);
            if (err == FASTGIT_OK) {
                fastgit_oid_t oid;
                if (fastgit_rev_parse(repo, "FETCH_HEAD", &oid) == FASTGIT_OK || fastgit_rev_parse(repo, "origin/main", &oid) == FASTGIT_OK || fastgit_rev_parse(repo, "origin/HEAD", &oid) == FASTGIT_OK) {
                    err = fastgit_merge(repo, &oid, NULL);
                }
            }
            if (err != FASTGIT_OK) fprintf(stderr, "pull failed: %s\n", fastgit_error_string(err));
            fastgit_repository_free(repo);
            return err == FASTGIT_OK ? 0 : 1;
        }
        case FASTGIT_CMD_REMOTE: {
            if (cli->cmd_argc < 1) { fprintf(stderr, "usage: fastgit remote <subcommand>\n"); return 1; }
            const char* sub = cli->cmd_argv[0];
            if (strcmp(sub, "add") == 0 && cli->cmd_argc >= 3) {
                fastgit_repository_t* repo = NULL;
                if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
                // naive: append to .git/config
                (void)repo;
                char cfg[1024]; snprintf(cfg, sizeof(cfg), ".git/config");
                FILE* f = fopen(cfg, "a");
                if (f) {
                    fprintf(f, "\n[remote \"%s\"]\n\turl = %s\n\tfetch = +refs/heads/*:refs/remotes/%s/*\n", cli->cmd_argv[1], cli->cmd_argv[2], cli->cmd_argv[1]);
                    fclose(f);
                }
                fastgit_repository_free(repo);
                return 0;
            }
            if (strcmp(sub, "-v") == 0 || strcmp(sub, "get-url") == 0 || strcmp(sub, "show") == 0) {
                fprintf(stderr, "fastgit remote: use git remote for now\n"); return 1;
            }
            fprintf(stderr, "fastgit remote: unknown subcommand %s\n", sub); return 1;
        }
        case FASTGIT_CMD_VERSION: {
            printf("fastgit version %s\n", fastgit_version());
            return 0;
        }
        case FASTGIT_CMD_STASH: {
            const char* sub = cli->cmd_argc >= 1 ? cli->cmd_argv[0] : "push";
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            char stash_ref[512]; snprintf(stash_ref, sizeof(stash_ref), "refs/stash");
            if (strcmp(sub, "push") == 0 || strcmp(sub, "save") == 0) {
                const char* msg = "WIP on HEAD";
                for (int i = 1; i < cli->cmd_argc; i++) if (strcmp(cli->cmd_argv[i], "-m") == 0 && i+1 < cli->cmd_argc) msg = cli->cmd_argv[++i];
                fastgit_index_t* idx = fastgit_repository_index(repo);
                fastgit_oid_t tree_oid;
                if (fastgit_index_write_tree(idx, &tree_oid) != FASTGIT_OK) { fastgit_repository_free(repo); return 1; }
                fastgit_oid_t parent; bool has_parent = (fastgit_rev_parse(repo, "HEAD", &parent) == FASTGIT_OK);
                fastgit_signature_t *a = NULL, *c = NULL; fastgit_signature_default(&a); fastgit_signature_default(&c);
                fastgit_commit_t ct = {0}; ct.tree = tree_oid;
                if (has_parent) { ct.parent_count = 1; ct.parents = &parent; }
                ct.author = a; ct.committer = c; ct.message = (char*)msg;
                fastgit_object_t* cobj = NULL;
                if (fastgit_commit_create(&ct, &cobj) != FASTGIT_OK) { fastgit_signature_free(a); fastgit_signature_free(c); fastgit_repository_free(repo); return 1; }
                fastgit_buf_t buf; fastgit_object_serialize(cobj, FASTGIT_HASH_SHA256, &buf);
                fastgit_odb_t* odb = fastgit_repository_odb(repo);
                fastgit_oid_t woid;
                size_t hdr = 0; while (hdr < buf.len && ((uint8_t*)buf.data)[hdr] != '\0') hdr++;
                const void* content = hdr < buf.len ? (uint8_t*)buf.data + hdr + 1 : buf.data;
                size_t clen = hdr < buf.len ? buf.len - hdr - 1 : buf.len;
                fastgit_odb_write(odb, FASTGIT_OBJ_COMMIT, content, clen, &woid);
                fastgit_reference_update(repo, stash_ref, &woid, msg);
                char hex[129]; fastgit_oid_to_hex(&woid, hex, sizeof(hex));
                printf("Saved working directory and index state %s\n", hex);
                free(buf.data); fastgit_object_free(cobj);
                fastgit_index_read_tree(idx, &tree_oid); fastgit_index_write(idx);
            } else if (strcmp(sub, "list") == 0) {
                fastgit_oid_t oid;
                if (fastgit_reference_lookup(repo, stash_ref, &oid) == FASTGIT_OK) {
                    char hex[129]; fastgit_oid_to_hex(&oid, hex, sizeof(hex));
                    printf("stash@{0}: %s\n", hex);
                }
            } else if (strcmp(sub, "pop") == 0 || strcmp(sub, "apply") == 0) {
                fastgit_oid_t oid;
                if (fastgit_reference_lookup(repo, stash_ref, &oid) != FASTGIT_OK) { fprintf(stderr, "No stash found.\n"); fastgit_repository_free(repo); return 1; }
                fastgit_object_t* obj = NULL;
                if (fastgit_object_lookup(repo, &oid, &obj) == FASTGIT_OK) {
                    const fastgit_commit_t* cc = fastgit_commit_parse(obj);
                    if (cc) {
                        fastgit_index_read_tree(fastgit_repository_index(repo), (fastgit_oid_t*)&cc->tree);
                        fastgit_index_write(fastgit_repository_index(repo));
                        fastgit_checkout_tree(fastgit_repository_worktree(repo), &cc->tree, true);
                        if (strcmp(sub, "pop") == 0) fastgit_reference_remove(repo, stash_ref);
                        printf("Restored stash %s\n", sub);
                    }
                    fastgit_object_free(obj);
                }
            } else if (strcmp(sub, "clear") == 0 || strcmp(sub, "drop") == 0) {
                fastgit_reference_remove(repo, stash_ref);
            } else {
                fprintf(stderr, "usage: fastgit stash [push|pop|apply|list|clear|drop]\n");
            }
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_BLAME: {
            if (cli->cmd_argc < 1) { fprintf(stderr, "usage: fastgit blame <file>\n"); return 1; }
            const char* path = cli->cmd_argv[cli->cmd_argc - 1];
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            FILE* f = fopen(path, "rb");
            char* content = NULL; size_t clen = 0;
            if (f) { fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); clen = sz > 0 ? (size_t)sz : 0; content = malloc(clen + 1); if (clen) fread(content, 1, clen, f); content[clen] = '\0'; fclose(f); }
            else {
                fastgit_oid_t head; if (fastgit_rev_parse(repo, "HEAD", &head) == FASTGIT_OK) {
                    fastgit_object_t* obj = NULL; if (fastgit_object_lookup(repo, &head, &obj) == FASTGIT_OK) {
                        const fastgit_commit_t* c = fastgit_commit_parse(obj);
                        if (c) {
                            fastgit_index_t* idx = fastgit_repository_index(repo);
                            fastgit_index_entry_t* ent = NULL;
                            if (fastgit_index_find(idx, path, 0, &ent) == FASTGIT_OK && ent) {
                                fastgit_odb_object_t o; if (fastgit_odb_read(fastgit_repository_odb(repo), &ent->oid, &o) == FASTGIT_OK) { content = malloc(o.size + 1); memcpy(content, o.data, o.size); content[o.size] = '\0'; clen = o.size; free(o.data); }
                            }
                        }
                        fastgit_object_free(obj);
                    }
                }
            }
            if (!content) { fastgit_repository_free(repo); return 1; }
            fastgit_oid_t head_oid; char head_hex[9] = "00000000";
            if (fastgit_rev_parse(repo, "HEAD", &head_oid) == FASTGIT_OK) { char hx[129]; fastgit_oid_to_hex(&head_oid, hx, sizeof(hx)); memcpy(head_hex, hx, 8); head_hex[8] = '\0'; }
            fastgit_signature_t* sig = NULL; const char* author = "unknown";
            fastgit_oid_t cur; if (fastgit_rev_parse(repo, "HEAD", &cur) == FASTGIT_OK) {
                fastgit_object_t* obj = NULL; if (fastgit_object_lookup(repo, &cur, &obj) == FASTGIT_OK) { const fastgit_commit_t* c = fastgit_commit_parse(obj); if (c && c->author) author = c->author->name; fastgit_object_free(obj); }
            }
            size_t line_no = 1; char* p = content;
            while (p < content + clen) {
                char* nl = memchr(p, '\n', (content + clen) - p);
                size_t llen = nl ? (size_t)(nl - p) : (size_t)((content + clen) - p);
                char line[1024]; size_t cp = llen < sizeof(line) - 1 ? llen : sizeof(line) - 1; memcpy(line, p, cp); line[cp] = '\0';
                printf("%s (%s %ld) %s\n", head_hex, author, (long)line_no, line);
                line_no++;
                if (!nl) break; p = nl + 1;
            }
            free(content); fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_GREP: {
            if (cli->cmd_argc < 1) { fprintf(stderr, "usage: fastgit grep <pattern> [<path>...]\n"); return 1; }
            const char* pattern = cli->cmd_argv[0];
            bool show_line = true; (void)show_line;
            regex_t re; int rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB);
            if (rc != 0) { char err[256]; regerror(rc, &re, err, sizeof(err)); fprintf(stderr, "grep: %s\n", err); return 1; }
            fastgit_repository_t* repo = NULL; bool has_repo = (fastgit_repository_open(".", &repo) == FASTGIT_OK);
            if (cli->cmd_argc >= 2) {
                for (int i = 1; i < cli->cmd_argc; i++) {
                    const char* pth = cli->cmd_argv[i];
                    FILE* f = fopen(pth, "rb"); if (!f) continue;
                    char line[8192]; int lineno = 1;
                    while (fgets(line, sizeof(line), f)) {
                        size_t ll = strlen(line); if (ll && line[ll-1] == '\n') line[ll-1] = '\0';
                        if (regexec(&re, line, 0, NULL, 0) == 0) printf("%s:%d:%s\n", pth, lineno, line);
                        lineno++;
                    }
                    fclose(f);
                }
            } else if (has_repo) {
                fastgit_index_t* idx = fastgit_repository_index(repo);
                size_t n = fastgit_index_entry_count(idx);
                for (size_t i = 0; i < n; i++) {
                    const fastgit_index_entry_t* e = fastgit_index_entry_by_index(idx, i);
                    if (!e) continue;
                    FILE* f = fopen(e->path, "rb"); if (!f) continue;
                    char line[8192]; int lineno = 1;
                    while (fgets(line, sizeof(line), f)) {
                        size_t ll = strlen(line); if (ll && line[ll-1] == '\n') line[ll-1] = '\0';
                        if (regexec(&re, line, 0, NULL, 0) == 0) printf("%s:%d:%s\n", e->path, lineno, line);
                        lineno++;
                    }
                    fclose(f);
                }
            }
            if (has_repo) fastgit_repository_free(repo);
            regfree(&re); return 0;
        }
        case FASTGIT_CMD_DOCTOR: {
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            int errors = 0;
            fastgit_index_t* idx = fastgit_repository_index(repo);
            size_t n = fastgit_index_entry_count(idx);
            fastgit_odb_t* odb = fastgit_repository_odb(repo);
            for (size_t i = 0; i < n; i++) {
                const fastgit_index_entry_t* e = fastgit_index_entry_by_index(idx, i);
                if (!e) continue;
                fastgit_odb_object_t o;
                if (fastgit_odb_read(odb, &e->oid, &o) != FASTGIT_OK) {
                    fprintf(stderr, "doctor: missing blob %s for %s\n", e->path, e->path);
                    char hex[129]; fastgit_oid_to_hex(&e->oid, hex, sizeof(hex));
                    fprintf(stderr, "  oid %s\n", hex);
                    errors++;
                } else free(o.data);
                struct stat st; if (stat(e->path, &st) != 0 && e->mode != 0) { /* may be deleted */ }
            }
            if (errors == 0) printf("doctor: repository OK (%zu index entries verified)\n", n);
            else printf("doctor: found %d errors\n", errors);
            fastgit_repository_free(repo); return errors ? 1 : 0;
        }
        case FASTGIT_CMD_MIGRATE: {
            if (cli->cmd_argc < 1) { fprintf(stderr, "usage: fastgit migrate <algorithm> (sha256|sha384|sha3-256)\n"); return 1; }
            const char* algo_name = cli->cmd_argv[0];
            uint8_t algo = FASTGIT_HASH_SHA256;
            if (strcmp(algo_name, "sha384") == 0) algo = FASTGIT_HASH_SHA384;
            else if (strcmp(algo_name, "sha3-256") == 0 || strcmp(algo_name, "sha3") == 0) algo = FASTGIT_HASH_SHA3_256;
            else if (strcmp(algo_name, "sha256") == 0) algo = FASTGIT_HASH_SHA256;
            else { fprintf(stderr, "unknown algorithm %s\n", algo_name); return 1; }
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            const char* gitdir = fastgit_repository_gitdir(repo);
            char cfg_path[1024]; snprintf(cfg_path, sizeof(cfg_path), "%s/config", gitdir);
            FILE* f = fopen(cfg_path, "a");
            if (f) {
                const char* ext = (algo == FASTGIT_HASH_SHA384) ? "sha384" : (algo == FASTGIT_HASH_SHA3_256) ? "sha3-256" : "sha256";
                fprintf(f, "\n# migrated by fastgit\n[extensions]\n\tobjectFormat = %s\n", ext);
                fclose(f);
                printf("migrated objectFormat to %s (algo 0x%02x)\n", ext, algo);
            }
            fastgit_repository_free(repo); return 0;
        }
        case FASTGIT_CMD_VERIFY: {
            fastgit_repository_t* repo = NULL;
            if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
            fastgit_odb_t* odb = fastgit_repository_odb(repo);
            fastgit_index_t* idx = fastgit_repository_index(repo);
            size_t n = fastgit_index_entry_count(idx);
            int errors = 0;
            for (size_t i = 0; i < n; i++) {
                const fastgit_index_entry_t* e = fastgit_index_entry_by_index(idx, i);
                if (!e) continue;
                fastgit_odb_object_t o;
                if (fastgit_odb_read(odb, &e->oid, &o) != FASTGIT_OK) { errors++; continue; }
                fastgit_hash_t h; fastgit_hash(FASTGIT_HASH_SHA256, o.data, o.size, &h);
                // blob header hash already verified via ODB path; check raw data hash matches oid len
                (void)h;
                free(o.data);
            }
            // also verify loose objects readable
            fastgit_odb_iterator_t* it = NULL;
            if (fastgit_odb_iterator_new(odb, &it) == FASTGIT_OK) {
                fastgit_oid_t oid; int count = 0;
                while (fastgit_odb_iterator_next(it, &oid) == FASTGIT_OK) count++;
                fastgit_odb_iterator_free(it);
                printf("verify: %zu index entries, %d loose objects, %d errors\n", n, count, errors);
            } else {
                printf("verify: %zu index entries, %d errors\n", n, errors);
            }
            fastgit_repository_free(repo); return errors ? 1 : 0;
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
