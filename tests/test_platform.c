#include "fastgit/platform.h"
#include <stdio.h>
#include <assert.h>

static void inc_counter(void* arg) {
    int* c = (int*)arg;
    (*c)++;
}

int main(void) {
    printf("Testing platform...\n");
    fastgit_simd_level_t level = fastgit_simd_detect();
    printf("SIMD level: %s\n", fastgit_simd_name(level));
    fastgit_simd_caps_t caps = fastgit_simd_capabilities();
    printf("SHA-256 HW: %s\n", caps.sha256_hw_accel ? "yes" : "no");
    printf("SHA-512 HW: %s\n", caps.sha512_hw_accel ? "yes" : "no");
    printf("AES HW: %s\n", caps.aes_hw_accel ? "yes" : "no");
    printf("CRC32 HW: %s\n", caps.crc32_hw_accel ? "yes" : "no");
    fastgit_thread_pool_config_t config = { .worker_count = 2, .max_queue_depth = 100 };
    fastgit_thread_pool_t* pool;
    fastgit_error_t err = fastgit_thread_pool_new(&config, &pool);
    assert(err == FASTGIT_OK);
    int counter = 0;
    err = fastgit_thread_pool_submit(pool, inc_counter, &counter);
    assert(err == FASTGIT_OK);
    err = fastgit_thread_pool_submit(pool, inc_counter, &counter);
    assert(err == FASTGIT_OK);
    err = fastgit_thread_pool_wait(pool);
    assert(err == FASTGIT_OK);
    assert(counter == 2);
    fastgit_thread_pool_free(pool);
    fastgit_arena_t* arena = fastgit_arena_new(1024);
    assert(arena != NULL);
    void* ptr1 = fastgit_arena_alloc(arena, 100, 16);
    assert(ptr1 != NULL);
    void* ptr2 = fastgit_arena_alloc(arena, 200, 32);
    assert(ptr2 != NULL);
    fastgit_arena_reset(arena);
    void* ptr3 = fastgit_arena_alloc(arena, 50, 8);
    assert(ptr3 == arena->base);
    fastgit_arena_free(arena);
    printf("All platform tests passed!\n");
    return 0;
}
