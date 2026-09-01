#include "fastgit/pack.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    printf("Testing pack delta...\n");

    const char* base = "Hello, world!";
    const char* target = "Hello, fastgit!";

    void* delta;
    size_t delta_len;
    fastgit_error_t err = fastgit_delta_compress(base, strlen(base), target, strlen(target), &delta, &delta_len);
    assert(err == FASTGIT_OK);
    printf("Delta size: %zu\n", delta_len);

    void* result;
    size_t result_len;
    err = fastgit_delta_apply(base, strlen(base), delta, delta_len, &result, &result_len);
    assert(err == FASTGIT_OK);
    assert(result_len == strlen(target));
    assert(memcmp(result, target, result_len) == 0);
    free(result);
    free(delta);

    printf("All pack tests passed!\n");
    return 0;
}
