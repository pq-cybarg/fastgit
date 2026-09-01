#include "fastgit/rebase.h"
#include <stdlib.h>
#include <string.h>

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
    (void)repo; (void)upstream; (void)opts;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_rebase_interactive(fastgit_repository_t* repo, const fastgit_oid_t* upstream, fastgit_rebase_step_t** steps, size_t* count) {
    (void)repo; (void)upstream; (void)steps; (void)count;
    return FASTGIT_EUNSUPPORTED;
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
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_rebase_abort(fastgit_repository_t* repo) {
    (void)repo;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_rebase_skip(fastgit_repository_t* repo) {
    (void)repo;
    return FASTGIT_EUNSUPPORTED;
}
