#ifndef FASTGIT_REBASE_H
#define FASTGIT_REBASE_H

#include "fastgit/object.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FASTGIT_REBASE_MERGE = 1,
    FASTGIT_REBASE_INTERACTIVE = 2,
    FASTGIT_REBASE_PRESERVE = 3,
} fastgit_rebase_type_t;

typedef struct fastgit_rebase_options {
    fastgit_rebase_type_t type;
    fastgit_oid_t upstream;
    fastgit_oid_t onto;
    bool autosquash;
    bool update_refs;
    char** exec;
    size_t exec_count;
    char* strategy;
    char** strategy_options;
    size_t strategy_options_count;
} fastgit_rebase_options_t;

fastgit_error_t fastgit_rebase_options_init(fastgit_rebase_options_t* opts, fastgit_rebase_type_t type);
void fastgit_rebase_options_free(fastgit_rebase_options_t* opts);

fastgit_error_t fastgit_rebase(fastgit_repository_t* repo, const fastgit_oid_t* upstream, const fastgit_rebase_options_t* opts);

typedef enum {
    FASTGIT_REBASE_OPERATION_PICK = 1,
    FASTGIT_REBASE_OPERATION_REWORD = 2,
    FASTGIT_REBASE_OPERATION_EDIT = 3,
    FASTGIT_REBASE_OPERATION_SQUASH = 4,
    FASTGIT_REBASE_OPERATION_FIXUP = 5,
    FASTGIT_REBASE_OPERATION_DROP = 6,
    FASTGIT_REBASE_OPERATION_LABEL = 7,
    FASTGIT_REBASE_OPERATION_RESET = 8,
    FASTGIT_REBASE_OPERATION_MERGE = 9,
} fastgit_rebase_operation_t;

typedef struct {
    fastgit_rebase_operation_t op;
    fastgit_oid_t commit;
    char* message;
    char* label;
} fastgit_rebase_step_t;

fastgit_error_t fastgit_rebase_interactive(fastgit_repository_t* repo, const fastgit_oid_t* upstream, fastgit_rebase_step_t** steps, size_t* count);
void fastgit_rebase_steps_free(fastgit_rebase_step_t* steps, size_t count);

fastgit_error_t fastgit_rebase_continue(fastgit_repository_t* repo);
fastgit_error_t fastgit_rebase_abort(fastgit_repository_t* repo);
fastgit_error_t fastgit_rebase_skip(fastgit_repository_t* repo);

#ifdef __cplusplus
}
#endif

#endif
