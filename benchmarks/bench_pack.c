#include "fastgit/pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define ITERATIONS 10000

static double time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void) {
    printf("Pack/delta benchmarks (iterations: %d)\n", ITERATIONS);

    char* base = malloc(10000);
    char* target = malloc(10000);
    for (size_t i = 0; i < 10000; i++) {
        base[i] = (char)('a' + (i % 26));
        target[i] = (i % 100 == 0) ? 'X' : base[i];
    }

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        void* delta;
        size_t delta_len;
        fastgit_error_t err = fastgit_delta_compress(base, 10000, target, 10000, &delta, &delta_len);
        (void)err;
    assert(err == FASTGIT_OK);
        free(delta);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double compress_ms = time_diff(start, end);
    printf("Compress: %8.2f ms  %10.0f ops/s\n", compress_ms, ITERATIONS / (compress_ms / 1000.0));

    void* delta;
    size_t delta_len;
    fastgit_error_t err2 = fastgit_delta_compress(base, 10000, target, 10000, &delta, &delta_len);
    (void)err2;
    assert(err2 == FASTGIT_OK);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        void* result;
        size_t result_len;
        err2 = fastgit_delta_apply(base, 10000, delta, delta_len, &result, &result_len);
        assert(err2 == FASTGIT_OK);
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double apply_ms = time_diff(start, end);
    printf("Apply:    %8.2f ms  %10.0f ops/s\n", apply_ms, ITERATIONS / (apply_ms / 1000.0));

    free(delta);
    free(base);
    free(target);
    return 0;
}
