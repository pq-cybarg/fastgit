#include "fastgit/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#endif

struct fastgit_io_context {
    fastgit_io_backend_t backend;
    int fd;
};

struct fastgit_thread_pool {
    fastgit_thread_pool_config_t config;
#if defined(_WIN32)
    HANDLE* threads;
    HANDLE* events;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cond;
    CONDITION_VARIABLE done_cond;
    fastgit_task_fn* tasks;
    void** task_args;
    int head, tail, count;
    int pending;
    bool shutdown;
#else
    pthread_t* threads;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_cond_t done_cond;
    fastgit_task_fn* tasks;
    void** task_args;
    int head, tail, count;
    int pending;
    bool shutdown;
#endif
};

fastgit_error_t fastgit_io_context_new(fastgit_io_context_t** out) {
    if (!out) return FASTGIT_EINVAL;

    fastgit_io_context_t* ctx = calloc(1, sizeof(fastgit_io_context_t));
    if (!ctx) return FASTGIT_ENOMEM;

#if defined(__linux__)
    ctx->backend = FASTGIT_IO_BACKEND_IO_URING;
#elif defined(_WIN32)
    ctx->backend = FASTGIT_IO_BACKEND_IOCP;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    ctx->backend = FASTGIT_IO_BACKEND_KQUEUE;
#else
    ctx->backend = FASTGIT_IO_BACKEND_EPOLL;
#endif

    *out = ctx;
    return FASTGIT_OK;
}

void fastgit_io_context_free(fastgit_io_context_t* ctx) {
    if (!ctx) return;
    free(ctx);
}

fastgit_error_t fastgit_io_context_set_backend(fastgit_io_context_t* ctx, fastgit_io_backend_t backend) {
    if (!ctx) return FASTGIT_EINVAL;
    ctx->backend = backend;
    return FASTGIT_OK;
}

fastgit_io_backend_t fastgit_io_context_backend(fastgit_io_context_t* ctx) {
    return ctx ? ctx->backend : FASTGIT_IO_BACKEND_AUTO;
}

fastgit_error_t fastgit_io_read(fastgit_io_context_t* ctx, fastgit_io_request_t* req) {
    (void)ctx; (void)req;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_io_write(fastgit_io_context_t* ctx, fastgit_io_request_t* req) {
    (void)ctx; (void)req;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_io_fsync(fastgit_io_context_t* ctx, fastgit_io_request_t* req) {
    (void)ctx; (void)req;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_io_submit(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count) {
    (void)ctx; (void)reqs; (void)count;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_io_wait(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count, int* completed) {
    (void)ctx; (void)reqs; (void)count; (void)completed;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_memory_set_vfs(const fastgit_memory_vfs_t* vfs) {
    (void)vfs;
    return FASTGIT_OK;
}

fastgit_arena_t* fastgit_arena_new(size_t initial_size) {
    fastgit_arena_t* arena = malloc(sizeof(fastgit_arena_t));
    if (!arena) return NULL;

    arena->base = malloc(initial_size);
    if (!arena->base) {
        free(arena);
        return NULL;
    }
    arena->current = arena->base;
    arena->end = arena->base + initial_size;
    arena->parent = NULL;
    return arena;
}

void fastgit_arena_free(fastgit_arena_t* arena) {
    if (!arena) return;
    free(arena->base);
    free(arena);
}

void* fastgit_arena_alloc(fastgit_arena_t* arena, size_t size, size_t alignment) {
    if (!arena) return NULL;

    uintptr_t addr = (uintptr_t)arena->current;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    size_t padding = aligned - addr;

    if (arena->current + padding + size > arena->end) {
        size_t new_size = (arena->end - arena->base) * 2;
        if (new_size < padding + size) new_size = padding + size + 4096;

        char* new_base = realloc(arena->base, new_size);
        if (!new_base) return NULL;

        arena->current = new_base + (arena->current - arena->base);
        arena->base = new_base;
        arena->end = new_base + new_size;
        aligned = (uintptr_t)arena->current;
        aligned = (aligned + alignment - 1) & ~(alignment - 1);
    }

    void* result = (void*)aligned;
    arena->current = (char*)aligned + size;
    return result;
}

void fastgit_arena_reset(fastgit_arena_t* arena) {
    if (arena) arena->current = arena->base;
}

fastgit_simd_level_t fastgit_simd_detect(void) {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(__AVX512F__)
    return FASTGIT_SIMD_AVX512;
#elif defined(__AVX2__)
    return FASTGIT_SIMD_AVX2;
#elif defined(__AVX__)
    return FASTGIT_SIMD_AVX;
#elif defined(__SSE4_2__)
    return FASTGIT_SIMD_SSE42;
#elif defined(__SSE2__)
    return FASTGIT_SIMD_SSE2;
#else
    return FASTGIT_SIMD_NONE;
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    return FASTGIT_SIMD_NEON;
#else
    return FASTGIT_SIMD_NONE;
#endif
}

const char* fastgit_simd_name(fastgit_simd_level_t level) {
    switch (level) {
        case FASTGIT_SIMD_SSE2: return "SSE2";
        case FASTGIT_SIMD_SSE42: return "SSE4.2";
        case FASTGIT_SIMD_AVX: return "AVX";
        case FASTGIT_SIMD_AVX2: return "AVX2";
        case FASTGIT_SIMD_AVX512: return "AVX-512";
        case FASTGIT_SIMD_NEON: return "NEON";
        case FASTGIT_SIMD_SVE: return "SVE";
        default: return "None";
    }
}

fastgit_simd_caps_t fastgit_simd_capabilities(void) {
    fastgit_simd_caps_t caps = {0};
    caps.level = fastgit_simd_detect();

#if defined(__x86_64__) || defined(_M_X64)
    caps.sha256_hw_accel = true;
    caps.sha512_hw_accel = true;
    caps.aes_hw_accel = true;
    caps.crc32_hw_accel = true;
#if defined(__AVX2__) || defined(__AVX512F__)
    caps.sha3_hw_accel = true;
#endif
#endif

    return caps;
}

static void* thread_worker(void* arg) {
    fastgit_thread_pool_t* pool = (fastgit_thread_pool_t*)arg;

    while (true) {
#if defined(_WIN32)
        EnterCriticalSection(&pool->lock);
        while (pool->count == 0 && !pool->shutdown) {
            SleepConditionVariableCS(&pool->cond, &pool->lock, INFINITE);
        }
        if (pool->shutdown && pool->count == 0) {
            LeaveCriticalSection(&pool->lock);
            break;
        }
        fastgit_task_fn fn = pool->tasks[pool->head];
        void* task_arg = pool->task_args[pool->head];
        pool->head = (pool->head + 1) % pool->config.max_queue_depth;
        pool->count--;
        LeaveCriticalSection(&pool->lock);
#else
        pthread_mutex_lock(&pool->lock);
        while (pool->count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->lock);
        }
        if (pool->shutdown && pool->count == 0) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }
        fastgit_task_fn fn = pool->tasks[pool->head];
        void* task_arg = pool->task_args[pool->head];
        pool->head = (pool->head + 1) % pool->config.max_queue_depth;
        pool->count--;
        pthread_mutex_unlock(&pool->lock);
#endif

        fn(task_arg);

#if defined(_WIN32)
        EnterCriticalSection(&pool->lock);
        pool->pending--;
        if (pool->pending == 0) WakeAllConditionVariable(&pool->done_cond);
        LeaveCriticalSection(&pool->lock);
#else
        pthread_mutex_lock(&pool->lock);
        pool->pending--;
        if (pool->pending == 0) pthread_cond_broadcast(&pool->done_cond);
        pthread_mutex_unlock(&pool->lock);
#endif
    }
    return NULL;
}

fastgit_error_t fastgit_thread_pool_new(const fastgit_thread_pool_config_t* config, fastgit_thread_pool_t** out) {
    if (!config || !out) return FASTGIT_EINVAL;

    fastgit_thread_pool_t* pool = calloc(1, sizeof(fastgit_thread_pool_t));
    if (!pool) return FASTGIT_ENOMEM;

    pool->config = *config;
    if (pool->config.worker_count <= 0) {
#if defined(_WIN32)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        pool->config.worker_count = si.dwNumberOfProcessors;
#else
        pool->config.worker_count = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    }
    if (pool->config.max_queue_depth <= 0) pool->config.max_queue_depth = 1024;

    pool->tasks = calloc(pool->config.max_queue_depth, sizeof(fastgit_task_fn));
    pool->task_args = calloc(pool->config.max_queue_depth, sizeof(void*));
    if (!pool->tasks || !pool->task_args) {
        fastgit_thread_pool_free(pool);
        return FASTGIT_ENOMEM;
    }

#if defined(_WIN32)
    InitializeCriticalSection(&pool->lock);
    InitializeConditionVariable(&pool->cond);
    InitializeConditionVariable(&pool->done_cond);
    pool->threads = calloc(pool->config.worker_count, sizeof(HANDLE));
    for (int i = 0; i < pool->config.worker_count; i++) {
        pool->threads[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thread_worker, pool, 0, NULL);
    }
#else
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);
    pthread_cond_init(&pool->done_cond, NULL);
    pool->threads = calloc(pool->config.worker_count, sizeof(pthread_t));
    for (int i = 0; i < pool->config.worker_count; i++) {
        pthread_create(&pool->threads[i], NULL, thread_worker, pool);
    }
#endif

    *out = pool;
    return FASTGIT_OK;
}

void fastgit_thread_pool_free(fastgit_thread_pool_t* pool) {
    if (!pool) return;

    pool->shutdown = true;
#if defined(_WIN32)
    WakeAllConditionVariable(&pool->cond);
    WakeAllConditionVariable(&pool->done_cond);
    WaitForMultipleObjects(pool->config.worker_count, pool->threads, TRUE, INFINITE);
    for (int i = 0; i < pool->config.worker_count; i++) {
        CloseHandle(pool->threads[i]);
    }
    DeleteCriticalSection(&pool->lock);
#else
    pthread_cond_broadcast(&pool->cond);
    pthread_cond_broadcast(&pool->done_cond);
    for (int i = 0; i < pool->config.worker_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    pthread_cond_destroy(&pool->done_cond);
#endif

    free(pool->threads);
    free(pool->tasks);
    free(pool->task_args);
    free(pool);
}

fastgit_error_t fastgit_thread_pool_submit(fastgit_thread_pool_t* pool, fastgit_task_fn fn, void* arg) {
    if (!pool || !fn) return FASTGIT_EINVAL;

#if defined(_WIN32)
    EnterCriticalSection(&pool->lock);
#else
    pthread_mutex_lock(&pool->lock);
#endif

    if (pool->count >= pool->config.max_queue_depth) {
#if defined(_WIN32)
        LeaveCriticalSection(&pool->lock);
#else
        pthread_mutex_unlock(&pool->lock);
#endif
        return FASTGIT_EBUSY;
    }

    pool->tasks[pool->tail] = fn;
    pool->task_args[pool->tail] = arg;
    pool->tail = (pool->tail + 1) % pool->config.max_queue_depth;
    pool->count++;
    pool->pending++;

#if defined(_WIN32)
    WakeConditionVariable(&pool->cond);
    LeaveCriticalSection(&pool->lock);
#else
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
#endif

    return FASTGIT_OK;
}

fastgit_error_t fastgit_thread_pool_wait(fastgit_thread_pool_t* pool) {
    if (!pool) return FASTGIT_EINVAL;

#if defined(_WIN32)
    EnterCriticalSection(&pool->lock);
    while (pool->pending > 0) {
        SleepConditionVariableCS(&pool->done_cond, &pool->lock, INFINITE);
    }
    LeaveCriticalSection(&pool->lock);
#else
    pthread_mutex_lock(&pool->lock);
    while (pool->pending > 0) {
        pthread_cond_wait(&pool->done_cond, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
#endif

    return FASTGIT_OK;
}

void fastgit_thread_pool_stats(fastgit_thread_pool_t* pool, fastgit_thread_stats_t* out) {
    if (!pool || !out) return;
    memset(out, 0, sizeof(fastgit_thread_stats_t));
}
