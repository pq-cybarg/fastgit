#include "fastgit/index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define ITERATIONS 10000

static int cmp_strptr(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }
static double time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void) {
    printf("Index benchmarks (iterations: %d)\n", ITERATIONS);

    fastgit_index_t* index;
    fastgit_error_t err = fastgit_index_new(&index);
    (void)err;
    assert(err == FASTGIT_OK);

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        char path[32];
        snprintf(path, sizeof(path), "file%d.txt", i);
        err = fastgit_index_add_from_buffer(index, path, 0100644, "data", 4);
        assert(err == FASTGIT_OK);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double add_ms = time_diff(start, end);
    printf("Add:  %8.2f ms  %10.0f ops/s\n", add_ms, ITERATIONS / (add_ms / 1000.0));

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        char path[32];
        snprintf(path, sizeof(path), "file%d.txt", i);
        fastgit_index_entry_t* entry;
        err = fastgit_index_find(index, path, FASTGIT_INDEX_STAGE_NORMAL, &entry);
        assert(err == FASTGIT_OK);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double find_ms = time_diff(start, end);
    printf("Find: %8.2f ms  %10.0f ops/s\n", find_ms, ITERATIONS / (find_ms / 1000.0));

    char** sorted = malloc(ITERATIONS * sizeof(char*));
    assert(sorted);
    for (int i = 0; i < ITERATIONS; i++) {
        sorted[i] = malloc(32);
        snprintf(sorted[i], 32, "file%d.txt", i);
    }
    qsort(sorted, ITERATIONS, sizeof(char*), cmp_strptr);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = ITERATIONS - 1; i >= 0; i--) {
        err = fastgit_index_remove(index, sorted[i], FASTGIT_INDEX_STAGE_NORMAL);
        assert(err == FASTGIT_OK);
    }
    for (int i = 0; i < ITERATIONS; i++) free(sorted[i]);
    free(sorted);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double remove_ms = time_diff(start, end);
    printf("Remove: %8.2f ms  %10.0f ops/s\n", remove_ms, ITERATIONS / (remove_ms / 1000.0));

    fastgit_index_free(index);
    return 0;
}
