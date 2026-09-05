#ifndef FASTGIT_PLATFORM_H
#define FASTGIT_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include "fastgit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FASTGIT_IO_BACKEND_AUTO = 0,
    FASTGIT_IO_BACKEND_IO_URING = 1,
    FASTGIT_IO_BACKEND_IOCP = 2,
    FASTGIT_IO_BACKEND_KQUEUE = 3,
    FASTGIT_IO_BACKEND_EPOLL = 4,
    FASTGIT_IO_BACKEND_SELECT = 5,
} fastgit_io_backend_t;

struct fastgit_io_context {
    fastgit_io_backend_t backend;
    int fd;
    void* uring_ctx;
};
typedef struct fastgit_io_context fastgit_io_context_t;

fastgit_error_t fastgit_io_context_new(fastgit_io_context_t** out);
void fastgit_io_context_free(fastgit_io_context_t* ctx);

fastgit_error_t fastgit_io_context_set_backend(fastgit_io_context_t* ctx, fastgit_io_backend_t backend);
fastgit_io_backend_t fastgit_io_context_backend(fastgit_io_context_t* ctx);

typedef struct {
    int fd;
    void* buf;
    size_t len;
    size_t offset;
    void (*callback)(int result, void* user_data);
    void* user_data;
} fastgit_io_request_t;

fastgit_error_t fastgit_io_read(fastgit_io_context_t* ctx, fastgit_io_request_t* req);
fastgit_error_t fastgit_io_write(fastgit_io_context_t* ctx, fastgit_io_request_t* req);
fastgit_error_t fastgit_io_fsync(fastgit_io_context_t* ctx, fastgit_io_request_t* req);

fastgit_error_t fastgit_io_submit(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count);
fastgit_error_t fastgit_io_wait(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count, int* completed);

typedef struct {
    void* (*alloc)(size_t size, size_t alignment);
    void (*free)(void* ptr);
    void* (*mmap)(void* addr, size_t len, int prot, int flags, int fd, off_t offset);
    int (*munmap)(void* addr, size_t len);
    int (*madvise)(void* addr, size_t len, int advice);
} fastgit_memory_vfs_t;

fastgit_error_t fastgit_memory_set_vfs(const fastgit_memory_vfs_t* vfs);

typedef struct fastgit_arena_t {
    char* base;
    char* current;
    char* end;
    struct fastgit_arena_t* parent;
} fastgit_arena_t;

fastgit_arena_t* fastgit_arena_new(size_t initial_size);
void fastgit_arena_free(fastgit_arena_t* arena);
void* fastgit_arena_alloc(fastgit_arena_t* arena, size_t size, size_t alignment);
void fastgit_arena_reset(fastgit_arena_t* arena);

typedef enum {
    FASTGIT_SIMD_NONE = 0,
    FASTGIT_SIMD_SSE2 = 1,
    FASTGIT_SIMD_SSE42 = 2,
    FASTGIT_SIMD_AVX = 3,
    FASTGIT_SIMD_AVX2 = 4,
    FASTGIT_SIMD_AVX512 = 5,
    FASTGIT_SIMD_NEON = 6,
    FASTGIT_SIMD_SVE = 7,
} fastgit_simd_level_t;

fastgit_simd_level_t fastgit_simd_detect(void);
const char* fastgit_simd_name(fastgit_simd_level_t level);

typedef struct {
    fastgit_simd_level_t level;
    bool sha256_hw_accel;
    bool sha512_hw_accel;
    bool sha3_hw_accel;
    bool aes_hw_accel;
    bool crc32_hw_accel;
} fastgit_simd_caps_t;

fastgit_simd_caps_t fastgit_simd_capabilities(void);

void fastgit_simd_memcpy(void* dst, const void* src, size_t len);
void fastgit_simd_memset(void* dst, int val, size_t len);
int fastgit_simd_memcmp(const void* a, const void* b, size_t len);

uint32_t fastgit_simd_crc32(uint32_t crc, const void* buf, size_t len);
uint32_t fastgit_simd_crc32c(uint32_t crc, const void* buf, size_t len);

fastgit_error_t fastgit_simd_sha256(const void* data, size_t len, uint8_t* out);
fastgit_error_t fastgit_simd_sha512(const void* data, size_t len, uint8_t* out);

typedef struct {
    int worker_count;
    int max_queue_depth;
    bool cpu_affinity;
} fastgit_thread_pool_config_t;

typedef struct fastgit_thread_pool fastgit_thread_pool_t;

fastgit_error_t fastgit_thread_pool_new(const fastgit_thread_pool_config_t* config, fastgit_thread_pool_t** out);
void fastgit_thread_pool_free(fastgit_thread_pool_t* pool);

typedef void (*fastgit_task_fn)(void* arg);

fastgit_error_t fastgit_thread_pool_submit(fastgit_thread_pool_t* pool, fastgit_task_fn fn, void* arg);
fastgit_error_t fastgit_thread_pool_wait(fastgit_thread_pool_t* pool);

typedef struct {
    uint64_t cpu_time_ns;
    uint64_t wall_time_ns;
    uint64_t peak_rss_bytes;
    uint64_t context_switches;
    uint64_t page_faults;
} fastgit_thread_stats_t;

void fastgit_thread_pool_stats(fastgit_thread_pool_t* pool, fastgit_thread_stats_t* out);

#ifdef __cplusplus
}
#endif

#endif
