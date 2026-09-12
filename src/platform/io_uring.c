#include "fastgit/platform.h"
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && defined(FASTGIT_HAVE_IO_URING)
#include <liburing.h>
#include <unistd.h>

struct fastgit_io_uring_context {
    struct io_uring ring;
    bool initialized;
};

fastgit_error_t fastgit_io_uring_init(fastgit_io_context_t* ctx, int entries) {
    if (!ctx) return FASTGIT_EINVAL;
    struct fastgit_io_uring_context* uctx = calloc(1, sizeof(*uctx));
    if (!uctx) return FASTGIT_ENOMEM;
    int rc = io_uring_queue_init(entries > 0 ? entries : 64, &uctx->ring, 0);
    if (rc < 0) { free(uctx); return FASTGIT_EIO; }
    uctx->initialized = true;
    ctx->uring_ctx = uctx;
    return FASTGIT_OK;
}

void fastgit_io_uring_cleanup(fastgit_io_context_t* ctx) {
    if (!ctx || !ctx->uring_ctx) { (void)ctx; return; }
#if defined(__linux__) && defined(FASTGIT_HAVE_IO_URING)
    struct fastgit_io_uring_context* uctx = (struct fastgit_io_uring_context*)ctx->uring_ctx;
    if (uctx && uctx->initialized) io_uring_queue_exit(&uctx->ring);
    free(uctx);
    ctx->uring_ctx = NULL;
#else
    (void)ctx;
#endif
}

fastgit_error_t fastgit_io_uring_submit(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count) {
    if (!ctx || !reqs) return FASTGIT_EINVAL;
#if defined(FASTGIT_HAVE_IO_URING)
    struct fastgit_io_uring_context* uctx = (struct fastgit_io_uring_context*)ctx->uring_ctx;
    if (!uctx || !uctx->initialized) return FASTGIT_EUNSUPPORTED;
    for (size_t i = 0; i < count; i++) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&uctx->ring);
        if (!sqe) return FASTGIT_EBUSY;
        if (reqs[i]->op == FASTGIT_IO_OP_WRITE)
            io_uring_prep_write(sqe, reqs[i]->fd, reqs[i]->buf, (unsigned)reqs[i]->len, (off_t)reqs[i]->offset);
        else if (reqs[i]->op == FASTGIT_IO_OP_FSYNC)
            io_uring_prep_fsync(sqe, reqs[i]->fd, 0);
        else
            io_uring_prep_read(sqe, reqs[i]->fd, reqs[i]->buf, (unsigned)reqs[i]->len, (off_t)reqs[i]->offset);
        if (i + 1 < count && reqs[i+1]->op == FASTGIT_IO_OP_FSYNC && reqs[i]->op == FASTGIT_IO_OP_WRITE) sqe->flags |= IOSQE_IO_LINK;
        io_uring_sqe_set_data(sqe, reqs[i]);
    }
    int rc = io_uring_submit(&uctx->ring);
    if (rc < 0) return FASTGIT_EIO;
    return FASTGIT_OK;
#else
    (void)ctx; (void)reqs; (void)count;
    return FASTGIT_EUNSUPPORTED;
#endif
}

fastgit_error_t fastgit_io_uring_wait(fastgit_io_context_t* ctx, fastgit_io_request_t** reqs, size_t count, int* completed) {
    if (!ctx) return FASTGIT_EINVAL;
#if defined(FASTGIT_HAVE_IO_URING)
    struct fastgit_io_uring_context* uctx = (struct fastgit_io_uring_context*)ctx->uring_ctx;
    if (!uctx || !uctx->initialized) return FASTGIT_EUNSUPPORTED;
    int done = 0;
    for (size_t i = 0; i < count; i++) {
        struct io_uring_cqe* cqe;
        int rc = io_uring_wait_cqe(&uctx->ring, &cqe);
        if (rc < 0) break;
        fastgit_io_request_t* req = (fastgit_io_request_t*)io_uring_cqe_get_data(cqe);
        if (req && req->callback) req->callback(cqe->res, req->user_data);
        io_uring_cqe_seen(&uctx->ring, cqe);
        done++;
        (void)reqs;
    }
    if (completed) *completed = done;
    return FASTGIT_OK;
#else
    (void)ctx; (void)reqs; (void)count; (void)completed;
    return FASTGIT_EUNSUPPORTED;
#endif
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
