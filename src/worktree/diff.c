#include "fastgit/worktree.h"
#include <stdlib.h>
#include <string.h>

fastgit_error_t fastgit_diff_options_init(fastgit_diff_options_t* opts) {
    if (!opts) return FASTGIT_EINVAL;
    opts->use_patience = false;
    opts->ignore_whitespace = false;
    opts->ignore_whitespace_change = false;
    opts->ignore_whitespace_at_eol = false;
    opts->context_lines = 3;
    opts->show_binary = false;
    return FASTGIT_OK;
}
