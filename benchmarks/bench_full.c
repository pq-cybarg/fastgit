#include "fastgit/fastgit.h"
#include "fastgit/hash.h"
#include "fastgit/object.h"
#include "fastgit/odb.h"
#include "fastgit/index.h"
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
    printf("Full workflow benchmarks (iterations: %d)\n", ITERATIONS);

    char tmpdir[] = "/tmp/fastgit_full_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != NULL);

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    fastgit_repository_t* repo;
    fastgit_error_t err = fastgit_repository_init(dir, false, &repo);
    (void)err;
    assert(err == FASTGIT_OK);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double init_ms = time_diff(start, end);
    printf("Init:     %8.2f ms\n", init_ms);

    fastgit_index_t* index = fastgit_repository_index(repo);
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    (void)odb;
    fastgit_worktree_t* wt = fastgit_repository_worktree(repo);

    char cwd_buf[1024];
    getcwd(cwd_buf, sizeof(cwd_buf));
    chdir(dir);

    char* file_content = malloc(1024);
    for (size_t i = 0; i < 1024; i++) file_content[i] = (char)(i & 0xFF);

    /* Create files first */
    char* add_paths[ITERATIONS];
    for (int i = 0; i < ITERATIONS; i++) {
        add_paths[i] = malloc(64);
        snprintf(add_paths[i], 64, "file%d.txt", i);
        FILE* f = fopen(add_paths[i], "w");
        if (!f) { fprintf(stderr, "fopen %s failed\n", add_paths[i]); assert(0); }
        fwrite(file_content, 1, 1024, f);
        fclose(f);
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    err = fastgit_index_add_many(index, (const char**)add_paths, ITERATIONS);
    if (err != FASTGIT_OK) fprintf(stderr, "add_many err=%d\n", err);
    assert(err == FASTGIT_OK);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double add_ms = time_diff(start, end);
    printf("Add %d files: %8.2f ms  %10.0f ops/s (bulk parallel)\n", ITERATIONS, add_ms, ITERATIONS / (add_ms / 1000.0));
    for (int i = 0; i < ITERATIONS; i++) free(add_paths[i]);

    clock_gettime(CLOCK_MONOTONIC, &start);
    err = fastgit_index_write(index);
    assert(err == FASTGIT_OK);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double write_ms = time_diff(start, end);
    printf("Index write: %8.2f ms\n", write_ms);

    fastgit_object_t* commit_obj = NULL;
    fastgit_signature_t* sig;
    fastgit_signature_new("Bench", "bench@test.com", 1234567890, 0, &sig);
    fastgit_signature_t* sig2;
    fastgit_signature_new("Bench", "bench@test.com", 1234567890, 0, &sig2);

    fastgit_commit_t commit_data = {0};
    commit_data.message = (char*)"Benchmark commit";
    commit_data.author = sig;
    commit_data.committer = sig2;

    clock_gettime(CLOCK_MONOTONIC, &start);
    err = fastgit_commit_create(&commit_data, &commit_obj);
    assert(err == FASTGIT_OK);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double commit_ms = time_diff(start, end);
    printf("Commit: %8.2f ms\n", commit_ms);

    if (commit_obj) fastgit_object_free(commit_obj);
    fastgit_signature_free(sig);
    fastgit_signature_free(sig2);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        char path[64];
        snprintf(path, sizeof(path), "file%d.txt", i);
        char* diff;
        err = fastgit_diff_worktree(wt, path, &diff);
        assert(err == FASTGIT_OK);
        free(diff);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double diff_ms = time_diff(start, end);
    printf("Diff %d files: %8.2f ms  %10.0f ops/s\n", ITERATIONS, diff_ms, ITERATIONS / (diff_ms / 1000.0));

    chdir(cwd_buf);
    fastgit_repository_free(repo);
    free(file_content);
    char rmcmd[128];
    snprintf(rmcmd, sizeof(rmcmd), "rm -rf %s", dir);
    system(rmcmd);
    system("rm -rf /tmp/fastgit_full_* 2>/dev/null; rm -f file*.txt 2>/dev/null");

    fastgit_stats_t stats = fastgit_stats_get();
    printf("\nTotal stats:\n");
    printf("  Objects read:    %llu\n", stats.objects_read);
    printf("  Objects written: %llu\n", stats.objects_written);
    printf("  Bytes read:      %llu\n", stats.bytes_read);
    printf("  Bytes written:   %llu\n", stats.bytes_written);
    printf("  CPU time:        %.2f ms\n", stats.cpu_time_ms);
    printf("  Wall time:       %.2f ms\n", stats.wall_time_ms);

    return 0;
}
