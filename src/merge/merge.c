#include "fastgit/merge.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include <stdlib.h>
#include <string.h>

fastgit_error_t fastgit_merge_options_init(fastgit_merge_options_t* opts, fastgit_merge_strategy_t strategy) {
    if (!opts) return FASTGIT_EINVAL;
    memset(opts, 0, sizeof(fastgit_merge_options_t));
    opts->strategy = strategy;
    opts->rename_threshold = 50;
    opts->find_renames = true;
    return FASTGIT_OK;
}

void fastgit_merge_options_free(fastgit_merge_options_t* opts) {
    if (!opts) return;
    free(opts->message);
    if (opts->author) fastgit_signature_free(opts->author);
    for (size_t i = 0; i < opts->xopts_count; i++) {
        free(opts->xopts[i]);
    }
    free(opts->xopts);
}

fastgit_error_t fastgit_merge(fastgit_repository_t* repo, const fastgit_oid_t* their_head, const fastgit_merge_options_t* opts) {
    (void)repo; (void)their_head; (void)opts;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_merge_analysis(fastgit_repository_t* repo, const fastgit_oid_t* their_head, fastgit_merge_analysis_t* out, fastgit_oid_t** merge_heads, size_t* merge_heads_count) {
    (void)repo; (void)their_head; (void)out; (void)merge_heads; (void)merge_heads_count;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_merge_conflicts(fastgit_repository_t* repo, fastgit_merge_conflict_t** conflicts, size_t* count) {
    (void)repo; (void)conflicts; (void)count;
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_merge_conflicts_free(fastgit_merge_conflict_t* conflicts, size_t count) {
    if (!conflicts) return;
    for (size_t i = 0; i < count; i++) {
        free(conflicts[i].path);
    }
    free(conflicts);
}

fastgit_error_t fastgit_merge_commit(fastgit_repository_t* repo, const fastgit_merge_options_t* opts, fastgit_merge_result_t* out) {
    (void)repo; (void)opts; (void)out;
    return FASTGIT_EUNSUPPORTED;
}
