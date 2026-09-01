#include "fastgit/merge.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("Testing merge options...\n");

    fastgit_merge_options_t opts;
    fastgit_error_t err = fastgit_merge_options_init(&opts, FASTGIT_MERGE_ORT);
    assert(err == FASTGIT_OK);
    assert(opts.strategy == FASTGIT_MERGE_ORT);
    assert(opts.rename_threshold == 50);
    assert(opts.find_renames == true);

    fastgit_merge_options_free(&opts);

    printf("All merge tests passed!\n");
    return 0;
}
