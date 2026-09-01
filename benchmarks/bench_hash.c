#include "fastgit/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ITERATIONS 100000

static double time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void) {
    printf("Hash benchmarks (iterations: %d)\n", ITERATIONS);

    char* data = malloc(1024 * 1024);
    for (size_t i = 0; i < 1024 * 1024; i++) {
        data[i] = (char)(i & 0xFF);
    }

    uint8_t algos[] = {
        FASTGIT_HASH_SHA256,
        FASTGIT_HASH_SHA384,
        FASTGIT_HASH_SHA3_256,
        FASTGIT_HASH_SHA3_384,
        FASTGIT_HASH_SHA3_512,
        FASTGIT_HASH_SHAKE128,
        FASTGIT_HASH_SHAKE256,
    };

    for (size_t a = 0; a < sizeof(algos); a++) {
        fastgit_hash_t hash;
        struct timespec start, end;

        clock_gettime(CLOCK_MONOTONIC, &start);
        for (int i = 0; i < ITERATIONS; i++) {
            fastgit_hash(algos[a], data, 1024, &hash);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);

        double ms = time_diff(start, end);
        double ops = ITERATIONS / (ms / 1000.0);
        double mbps = (ops * 1024) / (1024 * 1024);

        printf("%-12s: %8.2f ms  %10.0f ops/s  %8.2f MB/s\n",
               fastgit_hash_algo_name(algos[a]), ms, ops, mbps);
    }

    free(data);
    return 0;
}
