#include "fastgit/rebase.h"
#include "fastgit/fastgit.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include "fastgit/worktree.h"
#include <stdlib.h>
#include <string.h>

static bool rebase_oid_equal(const fastgit_oid_t* a, const fastgit_oid_t* b) {
    if (a->len != b->len) return false;
    return memcmp(a->hash, b->hash, a->len) == 0;
}
static bool rebase_is_ancestor(fastgit_repository_t* repo, const fastgit_oid_t* ancestor, const fastgit_oid_t* desc) {
    if (rebase_oid_equal(ancestor, desc)) return true;
    fastgit_oid_t cur = *desc;
    bool visited[256] = {0};
    (void)visited;
    for (int depth=0; depth<256; depth++) {
        if (rebase_oid_equal(&cur, ancestor)) return true;
        fastgit_object_t* obj = NULL;
        if (fastgit_object_lookup(repo, &cur, &obj) != FASTGIT_OK || !obj) break;
        const fastgit_commit_t* c = fastgit_commit_parse(obj);
        if (!c || c->parent_count==0) { fastgit_object_free(obj); break; }
        fastgit_oid_t next = c->parents[0];
        fastgit_object_free(obj);
        if (rebase_oid_equal(&next, &cur)) break;
        cur = next;
    }
    return false;
}

fastgit_error_t fastgit_rebase_options_init(fastgit_rebase_options_t* opts, fastgit_rebase_type_t type) {
    if (!opts) return FASTGIT_EINVAL;
    memset(opts, 0, sizeof(fastgit_rebase_options_t));
    opts->type = type;
    opts->autosquash = true;
    return FASTGIT_OK;
}

void fastgit_rebase_options_free(fastgit_rebase_options_t* opts) {
    if (!opts) return;
    for (size_t i = 0; i < opts->exec_count; i++) {
        free(opts->exec[i]);
    }
    free(opts->exec);
    free(opts->strategy);
    for (size_t i = 0; i < opts->strategy_options_count; i++) {
        free(opts->strategy_options[i]);
    }
    free(opts->strategy_options);
}

fastgit_error_t fastgit_rebase(fastgit_repository_t* repo, const fastgit_oid_t* upstream, const fastgit_rebase_options_t* opts) {
    (void)opts;
    if (!repo || !upstream) return FASTGIT_EINVAL;
    fastgit_oid_t head_oid;
    fastgit_error_t herr = fastgit_rev_parse(repo, "HEAD", &head_oid);
    if (herr == FASTGIT_ENOENT) {
        return fastgit_reference_update(repo, "HEAD", upstream, "rebase: onto");
    }
    if (herr != FASTGIT_OK) return herr;
    if (rebase_oid_equal(&head_oid, upstream)) return FASTGIT_OK;
    if (rebase_is_ancestor(repo, &head_oid, upstream)) {
        fastgit_object_t* obj = NULL;
        if (fastgit_object_lookup(repo, upstream, &obj) != FASTGIT_OK || !obj) return FASTGIT_ENOENT;
        const fastgit_commit_t* c = fastgit_commit_parse(obj);
        if (!c) { fastgit_object_free(obj); return FASTGIT_EINVAL; }
        fastgit_oid_t tree_oid = c->tree;
        fastgit_object_free(obj);
        fastgit_index_t* idx = fastgit_repository_index(repo);
        if (!idx) return FASTGIT_EINVAL;
        fastgit_error_t err = fastgit_index_read_tree(idx, &tree_oid);
        if (err != FASTGIT_OK) return err;
        err = fastgit_index_write(idx);
        if (err != FASTGIT_OK) return err;
        fastgit_worktree_t* wt = fastgit_repository_worktree(repo);
        err = fastgit_checkout_tree(wt, &tree_oid, false);
        if (err != FASTGIT_OK) return err;
        return fastgit_reference_update(repo, "HEAD", upstream, "rebase: fast-forward");
    }
    if (rebase_is_ancestor(repo, upstream, &head_oid)) return FASTGIT_OK;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_rebase_interactive(fastgit_repository_t* repo, const fastgit_oid_t* upstream, fastgit_rebase_step_t** steps, size_t* count) {
    (void)repo; (void)upstream;
    if (steps) *steps = NULL;
    if (count) *count = 0;
    return FASTGIT_OK;
}

void fastgit_rebase_steps_free(fastgit_rebase_step_t* steps, size_t count) {
    if (!steps) return;
    for (size_t i = 0; i < count; i++) {
        free(steps[i].message);
        free(steps[i].label);
    }
    free(steps);
}

fastgit_error_t fastgit_rebase_continue(fastgit_repository_t* repo) {
    (void)repo;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_rebase_abort(fastgit_repository_t* repo) {
    (void)repo;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_rebase_skip(fastgit_repository_t* repo) {
    (void)repo;
    return FASTGIT_OK;
}
