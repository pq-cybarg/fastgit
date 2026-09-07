#include "fastgit/merge.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include "fastgit/odb.h"
#include "fastgit/fastgit.h"
#include "fastgit/worktree.h"
#include <stdlib.h>
#include <string.h>

static bool oid_equal(const fastgit_oid_t* a, const fastgit_oid_t* b) { return fastgit_oid_equal(a,b); }

static bool is_ancestor(fastgit_repository_t* repo, const fastgit_oid_t* anc, const fastgit_oid_t* desc) {
    if (oid_equal(anc, desc)) return true;
    fastgit_oid_t stack[256];
    size_t sp = 0;
    fastgit_oid_t visited[256];
    size_t vc = 0;
    stack[sp++] = *desc;
    while (sp > 0) {
        fastgit_oid_t cur = stack[--sp];
        bool seen = false;
        for (size_t i=0;i<vc;i++) if (oid_equal(&visited[i], &cur)) { seen=true; break; }
        if (seen) continue;
        if (vc < 256) visited[vc++] = cur;
        if (oid_equal(&cur, anc)) return true;
         fastgit_object_t* obj = NULL;
        if (fastgit_object_lookup(repo, &cur, &obj) != FASTGIT_OK) continue;
        const fastgit_commit_t* c = fastgit_commit_parse(obj);
        if (!c) { fastgit_object_free(obj); continue; }
        for (size_t i=0;i<c->parent_count && sp<256;i++) stack[sp++] = c->parents[i];
        fastgit_object_free(obj);
    }
    return false;
}

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
    if (!repo || !their_head) return FASTGIT_EINVAL;
    (void)opts;
    fastgit_oid_t head_oid;
    fastgit_error_t err = fastgit_rev_parse_single(repo, "HEAD", &head_oid);
    if (err == FASTGIT_ENOENT) {
        // UNBORN - just create HEAD pointing to their_head
        return fastgit_reference_update(repo, "HEAD", their_head, "merge (unborn)");
    }
    if (err != FASTGIT_OK) return err;
    if (oid_equal(&head_oid, their_head)) return FASTGIT_OK; // UP_TO_DATE
    if (is_ancestor(repo, &head_oid, their_head)) {
        // FASTFORWARD
        fastgit_object_t* obj = NULL;
        err = fastgit_object_lookup(repo, their_head, &obj);
        if (err != FASTGIT_OK) return err;
        fastgit_commit_t* c = fastgit_commit_parse(obj);
        if (!c) { fastgit_object_free(obj); return FASTGIT_EINVAL; }
        fastgit_oid_t tree_oid = c->tree;
        fastgit_object_free(obj);
        fastgit_index_t* idx = fastgit_repository_index(repo);
        if (idx) {
            err = fastgit_index_read_tree(idx, &tree_oid);
            if (err != FASTGIT_OK) return err;
            err = fastgit_index_write(idx);
            if (err != FASTGIT_OK) return err;
        }
         fastgit_worktree_t* wt = fastgit_repository_worktree(repo);
        if (wt) {
            (void)fastgit_checkout_tree(wt, &tree_oid, false);
        }
        return fastgit_reference_update(repo, "HEAD", their_head, "merge (fast-forward)");
    }
    if (is_ancestor(repo, their_head, &head_oid)) return FASTGIT_OK; // ancestor -> up_to_date
    // True merge requires conflict handling - not yet implemented beyond ff
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_merge_analysis(fastgit_repository_t* repo, const fastgit_oid_t* their_head, fastgit_merge_analysis_t* out, fastgit_oid_t** merge_heads, size_t* merge_heads_count) {
    if (!repo || !their_head || !out) return FASTGIT_EINVAL;
    *out = FASTGIT_MERGE_ANALYSIS_NONE;
    if (merge_heads) *merge_heads = NULL;
    if (merge_heads_count) *merge_heads_count = 0;
    fastgit_oid_t head_oid;
    fastgit_error_t err = fastgit_rev_parse_single(repo, "HEAD", &head_oid);
    if (err == FASTGIT_ENOENT) {
        *out = FASTGIT_MERGE_ANALYSIS_UNBORN;
        if (merge_heads && merge_heads_count) {
            fastgit_oid_t* h = (fastgit_oid_t*)malloc(sizeof(fastgit_oid_t));
            if (!h) return FASTGIT_ENOMEM;
            *h = *their_head;
            *merge_heads = h;
            *merge_heads_count = 1;
        }
        return FASTGIT_OK;
    }
    if (err != FASTGIT_OK) return err;
    if (oid_equal(&head_oid, their_head)) {
        *out = FASTGIT_MERGE_ANALYSIS_UP_TO_DATE;
        return FASTGIT_OK;
    }
    if (is_ancestor(repo, &head_oid, their_head)) {
        *out = FASTGIT_MERGE_ANALYSIS_FASTFORWARD;
        if (merge_heads && merge_heads_count) {
            fastgit_oid_t* h = (fastgit_oid_t*)malloc(sizeof(fastgit_oid_t));
            if (!h) return FASTGIT_ENOMEM;
            *h = *their_head;
            *merge_heads = h;
            *merge_heads_count = 1;
        }
        return FASTGIT_OK;
    }
    if (is_ancestor(repo, their_head, &head_oid)) {
        *out = FASTGIT_MERGE_ANALYSIS_UP_TO_DATE;
        return FASTGIT_OK;
    }
    *out = FASTGIT_MERGE_ANALYSIS_NORMAL;
    if (merge_heads && merge_heads_count) {
        fastgit_oid_t* h = (fastgit_oid_t*)malloc(sizeof(fastgit_oid_t));
        if (!h) return FASTGIT_ENOMEM;
        *h = *their_head;
        *merge_heads = h;
        *merge_heads_count = 1;
    }
    return FASTGIT_OK;
}

fastgit_error_t fastgit_merge_conflicts(fastgit_repository_t* repo, fastgit_merge_conflict_t** conflicts, size_t* count) {
    if (!repo || !conflicts || !count) return FASTGIT_EINVAL;
    *conflicts = NULL;
    *count = 0;
    fastgit_index_t* idx = fastgit_repository_index(repo);
    if (!idx) return FASTGIT_OK;
    if (fastgit_index_has_conflicts(idx)) {
        // For now report generic conflict - real stage scan pending
        return FASTGIT_OK;
    }
    return FASTGIT_OK;
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
