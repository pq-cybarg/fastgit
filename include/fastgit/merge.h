#ifndef FASTGIT_MERGE_H
#define FASTGIT_MERGE_H

#include "fastgit/object.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FASTGIT_MERGE_RECURSIVE = 1,
    FASTGIT_MERGE_OURS = 2,
    FASTGIT_MERGE_THEIRS = 3,
    FASTGIT_MERGE_ORT = 4,
} fastgit_merge_strategy_t;

typedef struct fastgit_merge_options {
    fastgit_merge_strategy_t strategy;
    bool no_commit;
    bool no_ff;
    bool ff_only;
    bool squash;
    char* message;
    fastgit_signature_t* author;
    int rename_threshold;
    bool find_renames;
    char** xopts;
    size_t xopts_count;
} fastgit_merge_options_t;

fastgit_error_t fastgit_merge_options_init(fastgit_merge_options_t* opts, fastgit_merge_strategy_t strategy);
void fastgit_merge_options_free(fastgit_merge_options_t* opts);

fastgit_error_t fastgit_merge(fastgit_repository_t* repo, const fastgit_oid_t* their_head, const fastgit_merge_options_t* opts);

typedef enum {
    FASTGIT_MERGE_ANALYSIS_NONE = 0,
    FASTGIT_MERGE_ANALYSIS_NORMAL = 1,
    FASTGIT_MERGE_ANALYSIS_UP_TO_DATE = 2,
    FASTGIT_MERGE_ANALYSIS_FASTFORWARD = 4,
    FASTGIT_MERGE_ANALYSIS_UNBORN = 8,
} fastgit_merge_analysis_t;

fastgit_error_t fastgit_merge_analysis(fastgit_repository_t* repo, const fastgit_oid_t* their_head, fastgit_merge_analysis_t* out, fastgit_oid_t** merge_heads, size_t* merge_heads_count);

typedef struct {
    char* path;
    fastgit_oid_t ancestor_oid;
    fastgit_oid_t our_oid;
    fastgit_oid_t their_oid;
    uint32_t ancestor_mode;
    uint32_t our_mode;
    uint32_t their_mode;
} fastgit_merge_conflict_t;

fastgit_error_t fastgit_merge_conflicts(fastgit_repository_t* repo, fastgit_merge_conflict_t** conflicts, size_t* count);
void fastgit_merge_conflicts_free(fastgit_merge_conflict_t* conflicts, size_t count);

typedef struct {
    fastgit_oid_t tree;
    fastgit_oid_t commit;
} fastgit_merge_result_t;

fastgit_error_t fastgit_merge_commit(fastgit_repository_t* repo, const fastgit_merge_options_t* opts, fastgit_merge_result_t* out);

#ifdef __cplusplus
}
#endif

#endif
