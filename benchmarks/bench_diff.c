#include "fastgit/worktree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>

#define ITERATIONS 1000

static double time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void) {
    printf("Diff benchmarks (iterations: %d)\n", ITERATIONS);

    char tmpdir[] = "/tmp/fastgit_diff_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != NULL);

    fastgit_worktree_t* wt;
    fastgit_error_t err = fastgit_worktree_new(dir, &wt);
    (void)err;
    assert(err == FASTGIT_OK);

    char* data1 = malloc(10000);
    char* data2 = malloc(10000);
    for (size_t i = 0; i < 10000; i++) {
        data1[i] = (char)('a' + (i % 26));
        data2[i] = (i % 100 == 0) ? 'X' : data1[i];
    }

    char f1path[4096], f2path[4096];
    snprintf(f1path, sizeof(f1path), "%s/old.txt", dir);
    snprintf(f2path, sizeof(f2path), "%s/new.txt", dir);
    FILE* f1 = fopen(f1path, "w");
    fwrite(data1, 1, 10000, f1);
    fclose(f1);

    FILE* f2 = fopen(f2path, "w");
    fwrite(data2, 1, 10000, f2);
    fclose(f2);

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        char* diff;
        err = fastgit_diff_worktree(wt, "old.txt", &diff);
        assert(err == FASTGIT_OK);
        free(diff);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double diff_ms = time_diff(start, end);
    printf("Diff: %8.2f ms  %10.0f ops/s\n", diff_ms, ITERATIONS / (diff_ms / 1000.0));

    fastgit_worktree_free(wt);
    free(data1);
    free(data2);
    system("rm -rf /tmp/fastgit_diff_*");

    return 0;
}
