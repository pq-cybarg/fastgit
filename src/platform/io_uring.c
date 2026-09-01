#include "fastgit/platform.h"
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && defined(FASTGIT_HAVE_IO_URING)
#include <liburing.h>

struct fastgit_io_uring_context {
    struct io_uring ring;
    bool initialized;
};

fastgit_error_t fastgit_io_uring_init(fastgit_io_context_t* ctx, int entries) {
    if (!ctx) return FASTGIT_EINVAL;
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_io_uring_cleanup(fastgit_io_context_t* ctx) {
    (void)ctx;
}

fastgit_error_t fastgit_io_uring_submit(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count) {
    (void)ctx; (void)reqs; (void)count;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_io_uring_wait(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count, int* completed) {
    (void)ctx; (void)reqs; (void)count; (void)completed;
    return FASTGIT_EUNSUPPORTED;
}

#else

fastgit_error_t fastgit_io_uring_init(fastgit_io_context_t* ctx, int entries) {
    (void)ctx; (void)entries;
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_io_uring_cleanup(fastgit_io_context_t* ctx) {
    (void)ctx;
}

fastgit_error_t fastgit_io_uring_submit(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count) {
    (void)ctx; (void)reqs; (void)count;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_io_uring_wait(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count, int* completed) {
    (void)ctx; (void)reqs; (void)count; (void)completed;
    return FASTGIT_EUNSUPPORTED;
}

#endif
