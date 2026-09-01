#include "fastgit/platform.h"
#include <stdlib.h>
#include <string.h>

#if (defined(__APPLE__) || defined(__FreeBSD__)) && defined(FASTGIT_HAVE_KQUEUE)
#include <sys/event.h>
#include <sys/time.h>

struct fastgit_kqueue_context {
    int kq;
    struct kevent* events;
    int max_events;
    bool initialized;
};

fastgit_error_t fastgit_kqueue_init(fastgit_io_context_t* ctx, int max_events) {
    (void)ctx; (void)max_events;
    if (!ctx) return FASTGIT_EINVAL;
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_kqueue_cleanup(fastgit_io_context_t* ctx) {
    (void)ctx;
}

fastgit_error_t fastgit_kqueue_submit(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count) {
    (void)ctx; (void)reqs; (void)count;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_kqueue_wait(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count, int* completed) {
    (void)ctx; (void)reqs; (void)count; (void)completed;
    return FASTGIT_EUNSUPPORTED;
}

#else

fastgit_error_t fastgit_kqueue_init(fastgit_io_context_t* ctx, int max_events) {
    (void)ctx; (void)max_events;
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_kqueue_cleanup(fastgit_io_context_t* ctx) {
    (void)ctx;
}

fastgit_error_t fastgit_kqueue_submit(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count) {
    (void)ctx; (void)reqs; (void)count;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_kqueue_wait(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count, int* completed) {
    (void)ctx; (void)reqs; (void)count; (void)completed;
    return FASTGIT_EUNSUPPORTED;
}

#endif
