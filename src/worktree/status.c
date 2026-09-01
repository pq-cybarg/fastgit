#include "fastgit/worktree.h"
#include <stdlib.h>
#include <string.h>

fastgit_error_t fastgit_status_options_init(fastgit_status_options_t* opts) {
    if (!opts) return FASTGIT_EINVAL;
    opts->use_fsmonitor = true;
    opts->ignore_submodules = false;
    opts->rename_threshold = 50;
    opts->show_untracked = true;
    return FASTGIT_OK;
}
