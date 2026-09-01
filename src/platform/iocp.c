#include "fastgit/platform.h"
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && defined(FASTGIT_HAVE_IOCP)
#include <windows.h>

struct fastgit_iocp_context {
    HANDLE iocp;
    bool initialized;
};

fastgit_error_t fastgit_iocp_init(fastgit_io_context_t* ctx, int max_concurrency) {
    if (!ctx) return FASTGIT_EINVAL;
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_iocp_cleanup(fastgit_io_context_t* ctx) {
    (void)ctx;
}

fastgit_error_t fastgit_iocp_submit(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count) {
    (void)ctx; (void)reqs; (void)count;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_iocp_wait(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count, int* completed) {
    (void)ctx; (void)reqs; (void)count; (void)completed;
    return FASTGIT_EUNSUPPORTED;
}

#else

fastgit_error_t fastgit_iocp_init(fastgit_io_context_t* ctx, int max_concurrency) {
    (void)ctx; (void)max_concurrency;
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_iocp_cleanup(fastgit_io_context_t* ctx) {
    (void)ctx;
}

fastgit_error_t fastgit_iocp_submit(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count) {
    (void)ctx; (void)reqs; (void)count;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_iocp_wait(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count, int* completed) {
    (void)ctx; (void)reqs; (void)count; (void)completed;
    return FASTGIT_EUNSUPPORTED;
}

#endif
