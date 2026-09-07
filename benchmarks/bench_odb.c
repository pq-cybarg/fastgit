#include "fastgit/odb.h"
#include "fastgit/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <assert.h>
#include <unistd.h>

#define ITERATIONS 10000

static double time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void) {
    printf("ODB benchmarks (iterations: %d)\n", ITERATIONS);

    char tmpdir[] = "/tmp/fastgit_bench_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != NULL);

    fastgit_odb_t* odb;
    fastgit_error_t err = fastgit_odb_new(dir, &odb);
    (void)err;
    assert(err == FASTGIT_OK);

    char* data = malloc(1024);
    for (size_t i = 0; i < 1024; i++) data[i] = (char)(i & 0xFF);

    struct timespec start, end;

    /* Hot write: same blob repeatedly — hits 16K ODB cache after first, measures cache path. */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        fastgit_oid_t oid;
        err = fastgit_odb_write(odb, FASTGIT_OBJ_BLOB, data, 1024, &oid);
        assert(err == FASTGIT_OK);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double write_hot_ms = time_diff(start, end);
    printf("Write (hot cache, same blob): %8.2f ms  %10.0f ops/s\n", write_hot_ms, ITERATIONS / (write_hot_ms / 1000.0));

    /* Durable write: distinct blobs — cache misses, measures serialize+zlib+write. */
    fastgit_oid_t *oids = malloc(ITERATIONS * sizeof(fastgit_oid_t));
    assert(oids != NULL);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        data[0] = (char)(i & 0xFF);
        data[1] = (char)((i >> 8) & 0xFF);
        data[2] = (char)((i >> 16) & 0xFF);
        data[3] = (char)((i >> 24) & 0xFF);
        err = fastgit_odb_write(odb, FASTGIT_OBJ_BLOB, data, 1024, &oids[i]);
        assert(err == FASTGIT_OK);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double write_cold_ms = time_diff(start, end);
    printf("Write (durable distinct):     %8.2f ms  %10.0f ops/s\n", write_cold_ms, ITERATIONS / (write_cold_ms / 1000.0));

    /* Hot read: same OID — hits cache. */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        fastgit_odb_object_t obj;
        err = fastgit_odb_read(odb, &oids[0], &obj);
        assert(err == FASTGIT_OK);
        free(obj.data);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double read_hot_ms = time_diff(start, end);
    printf("Read  (hot cache):            %8.2f ms  %10.0f ops/s\n", read_hot_ms, ITERATIONS / (read_hot_ms / 1000.0));

    /* Cold-ish read: distinct OIDs sequentially — less cache locality. */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        fastgit_odb_object_t obj;
        err = fastgit_odb_read(odb, &oids[i], &obj);
        assert(err == FASTGIT_OK);
        free(obj.data);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double read_cold_ms = time_diff(start, end);
    printf("Read  (distinct):             %8.2f ms  %10.0f ops/s\n", read_cold_ms, ITERATIONS / (read_cold_ms / 1000.0));
    free(oids);

    fastgit_odb_free(odb);
    free(data);
    system("rm -rf /tmp/fastgit_bench_*");

    return 0;
}
