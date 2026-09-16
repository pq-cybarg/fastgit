#include "fastgit/cli.h"
#include "fastgit/fastgit.h"
#include "fastgit/merge.h"
#include "fastgit/rebase.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "fastgit/cli.h"
#include "fastgit/fastgit.h"
#include "fastgit/merge.h"
#include "fastgit/rebase.h"
#include "fastgit/network.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static fastgit_error_t parse_filter_spec(const char* filter_str, fastgit_filter_list_t* list) {
    if (!filter_str || !list) return FASTGIT_EINVAL;
    // Parse filter spec like "blob:none", "blob:limit=1m", "tree:2"
    fastgit_filter_spec_t spec = {0};
    if (strcmp(filter_str, "blob:none") == 0) {
        spec.type = FASTGIT_FILTER_BLOB_NONE;
    } else if (strncmp(filter_str, "blob:limit=", 11) == 0) {
        spec.type = FASTGIT_FILTER_BLOB_LIMIT;
        spec.blob_limit.max_size = strtoull(filter_str + 11, NULL, 10);
    } else if (strncmp(filter_str, "tree:", 5) == 0) {
        spec.type = FASTGIT_FILTER_TREE_DEPTH;
        spec.tree_depth.depth = (uint32_t)strtoul(filter_str + 5, NULL, 10);
    } else {
        return FASTGIT_EINVAL;
    }
    return fastgit_filter_list_append(list, &spec);
}

int fastgit_cmd_fetch(int argc, char **argv) {
    const char* remote = "origin";
    const char* refspec = NULL;
    fastgit_filter_list_t filter = {0};
    
    // Parse options first (git fetch syntax: git fetch [<options>] [<repository> [<refspec>...]])
    int arg = 0;
    while (arg < argc && argv[arg][0] == '-') {
        if (strcmp(argv[arg], "--filter") == 0 && arg + 1 < argc) {
            if (parse_filter_spec(argv[arg + 1], &filter) != FASTGIT_OK) {
                fprintf(stderr, "invalid filter spec: %s\n", argv[arg + 1]);
                fastgit_filter_list_free(&filter);
                return 1;
            }
            arg += 2; // skip --filter and its value
        } else if (strcmp(argv[arg], "--help") == 0 || strcmp(argv[arg], "-h") == 0) {
            printf("usage: fastgit fetch [<options>] [<repository> [<refspec>...]]\n");
            printf("Options:\n  --filter <spec>  Use partial clone filter (e.g., blob:none, blob:limit=1m, tree:2)\n");
            fastgit_filter_list_free(&filter);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[arg]);
            fastgit_filter_list_free(&filter);
            return 1;
        }
    }
    
    // Remaining args: [repository] [refspec...]
    if (arg < argc) {
        remote = argv[arg];
        arg++;
    }
    if (arg < argc) {
        refspec = argv[arg];
        arg++;
    }
    
    fastgit_repository_t* repo = NULL;
    if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); fastgit_filter_list_free(&filter); return 1; }
    
    fastgit_remote_t* r = NULL;
    fastgit_error_t err = fastgit_remote_lookup(repo, remote, &r);
    if (err != FASTGIT_OK) err = fastgit_remote_create(repo, remote, remote, &r);
    if (err == FASTGIT_OK && filter.count > 0) {
        err = fastgit_remote_set_filter(r, &filter);
    }
    if (err == FASTGIT_OK) {
        err = fastgit_fetch_remote(repo, r, refspec);
    }
    if (err != FASTGIT_OK) fprintf(stderr, "fetch failed: %s\n", fastgit_error_string(err));
    fastgit_filter_list_free(&filter);
    fastgit_remote_free(r);
    fastgit_repository_free(repo);
    return err == FASTGIT_OK ? 0 : 1;
}

int fastgit_cmd_push(int argc, char **argv) {
    const char* remote = argc >= 1 ? argv[0] : "origin";
    const char* refspec = argc >= 2 ? argv[1] : NULL;
    fastgit_repository_t* repo = NULL;
    if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
    fastgit_error_t err = fastgit_push(repo, remote, refspec);
    if (err != FASTGIT_OK) fprintf(stderr, "push failed: %s\n", fastgit_error_string(err));
    fastgit_repository_free(repo);
    return err == FASTGIT_OK ? 0 : 1;
}

int fastgit_cmd_pull(int argc, char **argv) {
    const char* remote = argc >= 1 ? argv[0] : "origin";
    const char* refspec = argc >= 2 ? argv[1] : NULL;
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

int fastgit_cmd_remote(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: fastgit remote <subcommand>\n"); return 1; }
    const char* sub = argv[0];
    if (strcmp(sub, "add") == 0 && argc >= 3) {
        fastgit_repository_t* repo = NULL;
        if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
        (void)repo;
        char cfg[1024]; snprintf(cfg, sizeof(cfg), ".git/config");
        FILE* f = fopen(cfg, "a");
        if (f) {
            fprintf(f, "\n[remote \"%s\"]\n\turl = %s\n\tfetch = +refs/heads/*:refs/remotes/%s/*\n", argv[1], argv[2], argv[1]);
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

int fastgit_cmd_merge(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: fastgit merge <commit>\n"); return 1; }
    fastgit_repository_t* repo = NULL;
    if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
    fastgit_oid_t oid;
    if (fastgit_rev_parse(repo, argv[0], &oid) != FASTGIT_OK) { fprintf(stderr, "fatal: could not parse %s\n", argv[0]); fastgit_repository_free(repo); return 1; }
    fastgit_error_t err = fastgit_merge(repo, &oid, NULL);
    if (err != FASTGIT_OK) { fprintf(stderr, "merge failed: %s\n", fastgit_error_string(err)); fastgit_repository_free(repo); return 1; }
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_rebase(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: fastgit rebase <upstream>\n"); return 1; }
    fastgit_repository_t* repo = NULL;
    if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
    fastgit_oid_t oid;
    if (fastgit_rev_parse(repo, argv[0], &oid) != FASTGIT_OK) { fprintf(stderr, "fatal: could not parse %s\n", argv[0]); fastgit_repository_free(repo); return 1; }
    fastgit_rebase_options_t opts = {0};
    fastgit_error_t err = fastgit_rebase(repo, &oid, &opts);
    if (err != FASTGIT_OK) { fprintf(stderr, "rebase failed: %s\n", fastgit_error_string(err)); fastgit_repository_free(repo); return 1; }
    fastgit_repository_free(repo); return 0;
}
