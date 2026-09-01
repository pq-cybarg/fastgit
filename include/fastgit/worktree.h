#ifndef FASTGIT_WORKTREE_H
#define FASTGIT_WORKTREE_H

#include "fastgit/fastgit.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fastgit_worktree fastgit_worktree_t;

typedef enum {
    FASTGIT_STATUS_CURRENT = 0,
    FASTGIT_STATUS_INDEX_NEW = 1,
    FASTGIT_STATUS_INDEX_MODIFIED = 2,
    FASTGIT_STATUS_INDEX_DELETED = 3,
    FASTGIT_STATUS_INDEX_RENAMED = 4,
    FASTGIT_STATUS_INDEX_TYPECHANGE = 5,
    FASTGIT_STATUS_WT_NEW = 6,
    FASTGIT_STATUS_WT_MODIFIED = 7,
    FASTGIT_STATUS_WT_DELETED = 8,
    FASTGIT_STATUS_WT_RENAMED = 9,
    FASTGIT_STATUS_WT_TYPECHANGE = 10,
    FASTGIT_STATUS_IGNORED = 11,
    FASTGIT_STATUS_CONFLICTED = 12,
} fastgit_status_t;

struct fastgit_status_entry {
    char* path;
    fastgit_status_t index_status;
    fastgit_status_t worktree_status;
    fastgit_oid_t index_oid;
    fastgit_oid_t worktree_oid;
    uint32_t index_mode;
    uint32_t worktree_mode;
};

fastgit_error_t fastgit_worktree_new(const char* path, fastgit_worktree_t** out);
fastgit_error_t fastgit_worktree_open(const char* path, fastgit_worktree_t** out);
void fastgit_worktree_free(fastgit_worktree_t* wt);

const char* fastgit_worktree_path(fastgit_worktree_t* wt);
const char* fastgit_worktree_gitdir(fastgit_worktree_t* wt);

fastgit_error_t fastgit_checkout_head(fastgit_worktree_t* wt, bool force);
fastgit_error_t fastgit_checkout_tree(fastgit_worktree_t* wt, const fastgit_oid_t* tree_oid, bool force);
fastgit_error_t fastgit_checkout_index(fastgit_worktree_t* wt, fastgit_index_t* index, bool force);

fastgit_error_t fastgit_status(fastgit_worktree_t* wt, fastgit_status_entry_t** entries, size_t* count);
void fastgit_status_free(fastgit_status_entry_t* entries, size_t count);

fastgit_error_t fastgit_diff_worktree(fastgit_worktree_t* wt, const char* path, char** out);
fastgit_error_t fastgit_diff_index(fastgit_worktree_t* wt, fastgit_index_t* index, const char* path, char** out);
fastgit_error_t fastgit_diff_trees(fastgit_worktree_t* wt, const fastgit_oid_t* old_tree, const fastgit_oid_t* new_tree, char** out);

typedef struct {
    bool use_fsmonitor;
    bool ignore_submodules;
    int rename_threshold;
    bool show_untracked;
} fastgit_status_options_t;

fastgit_error_t fastgit_status_ext(fastgit_worktree_t* wt, const fastgit_status_options_t* opts, fastgit_status_entry_t** entries, size_t* count);

typedef struct {
    bool use_patience;
    bool ignore_whitespace;
    bool ignore_whitespace_change;
    bool ignore_whitespace_at_eol;
    int context_lines;
    bool show_binary;
} fastgit_diff_options_t;

fastgit_error_t fastgit_diff_worktree_ext(fastgit_worktree_t* wt, const char* path, const fastgit_diff_options_t* opts, char** out);

#ifdef __cplusplus
}
#endif

#endif
