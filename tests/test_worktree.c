#include "fastgit/worktree.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    printf("Testing worktree...\n");

    char tmpdir[] = "/tmp/fastgit_wt_test_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != NULL);

    fastgit_worktree_t* wt;
    fastgit_error_t err = fastgit_worktree_new(dir, &wt);
    assert(err == FASTGIT_OK);
    assert(strcmp(fastgit_worktree_path(wt), dir) == 0);

    fastgit_worktree_free(wt);

    system("rm -rf /tmp/fastgit_wt_test_*");

    printf("All worktree tests passed!\n");
    return 0;
}
