#include "sha3.h"
#include <string.h>
#include <stdlib.h>

#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
#include <openssl/evp.h>
#else
#include <stdint.h>

static const uint64_t KECCAK_RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL
};

static const int KECCAK_RHO[24] = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
    27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44
};

static const int KECCAK_PI[24] = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
    15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1
};

#define ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

typedef struct {
    uint64_t state[25];
    size_t rate;
    size_t capacity;
    size_t pos;
    uint8_t buffer[200];
    uint8_t delim;
    bool squeezing;
} keccak_ctx_t;

static void keccak_f1600(uint64_t state[25]) {
    uint64_t bc[5];
    uint64_t t;

    for (int round = 0; round < 24; round++) {
        for (int x = 0; x < 5; x++) {
            bc[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }

        for (int x = 0; x < 5; x++) {
            t = bc[(x + 4) % 5] ^ ROTL64(bc[(x + 1) % 5], 1);
            for (int y = 0; y < 25; y += 5) {
                state[y + x] ^= t;
            }
        }

        t = state[1];
        for (int i = 0; i < 24; i++) {
            int j = KECCAK_PI[i];
            bc[0] = state[j];
            state[j] = ROTL64(t, KECCAK_RHO[i]);
            t = bc[0];
        }

        for (int y = 0; y < 25; y += 5) {
            for (int x = 0; x < 5; x++) {
                bc[x] = state[y + x];
            }
            for (int x = 0; x < 5; x++) {
                state[y + x] = bc[x] ^ (~bc[(x + 1) % 5] & bc[(x + 2) % 5]);
            }
        }

        state[0] ^= KECCAK_RC[round];
    }
}

static void keccak_absorb(keccak_ctx_t* ctx, const uint8_t* data, size_t len) {
    while (len > 0) {
        size_t take = ctx->rate - ctx->pos;
        if (take > len) take = len;

        for (size_t i = 0; i < take; i++) {
            ctx->buffer[ctx->pos + i] ^= data[i];
        }
        ctx->pos += take;
        data += take;
        len -= take;

        if (ctx->pos == ctx->rate) {
            for (size_t i = 0; i < ctx->rate / 8; i++) {
                ctx->state[i] ^= *(uint64_t*)(ctx->buffer + i * 8);
            }
            keccak_f1600(ctx->state);
            ctx->pos = 0;
        }
    }
}

static void keccak_squeeze(keccak_ctx_t* ctx, uint8_t* out, size_t len) {
    if (!ctx->squeezing) {
        ctx->buffer[ctx->pos] ^= ctx->delim;
        ctx->buffer[ctx->rate - 1] ^= 0x80;
        for (size_t i = 0; i < ctx->rate / 8; i++) {
            ctx->state[i] ^= *(uint64_t*)(ctx->buffer + i * 8);
        }
        keccak_f1600(ctx->state);
        ctx->squeezing = true;
        ctx->pos = 0;
    }

    while (len > 0) {
        size_t take = ctx->rate - ctx->pos;
        if (take > len) take = len;

        for (size_t i = 0; i < take; i++) {
            out[i] = (ctx->state[ctx->pos / 8] >> ((ctx->pos % 8) * 8)) & 0xFF;
        }
        ctx->pos += take;
        out += take;
        len -= take;

        if (ctx->pos == ctx->rate) {
            keccak_f1600(ctx->state);
            ctx->pos = 0;
        }
    }
}

static fastgit_error_t keccak_init(void* impl, size_t rate, size_t capacity, uint8_t delim) {
    keccak_ctx_t* ctx = (keccak_ctx_t*)impl;
    memset(ctx->state, 0, sizeof(ctx->state));
    ctx->rate = rate;
    ctx->capacity = capacity;
    ctx->pos = 0;
    ctx->delim = delim;
    ctx->squeezing = false;
    return FASTGIT_OK;
}

static fastgit_error_t keccak_update(void* impl, const void* data, size_t len) {
    keccak_ctx_t* ctx = (keccak_ctx_t*)impl;
    if (ctx->squeezing) return FASTGIT_EINVAL;
    keccak_absorb(ctx, (const uint8_t*)data, len);
    return FASTGIT_OK;
}

static fastgit_error_t keccak_final(void* impl, fastgit_hash_t* out) {
    if (!out) return FASTGIT_EINVAL;
    keccak_ctx_t* ctx = (keccak_ctx_t*)impl;
    if (out->len == 0) return FASTGIT_EINVAL;
    if (!ctx->squeezing) {
        ctx->buffer[ctx->pos] ^= ctx->delim;
        ctx->buffer[ctx->rate - 1] ^= 0x80;
        for (size_t i = 0; i < ctx->rate / 8; i++) {
            ctx->state[i] ^= *(uint64_t*)(ctx->buffer + i * 8);
        }
        keccak_f1600(ctx->state);
        ctx->squeezing = true;
        ctx->pos = 0;
    }
    keccak_squeeze(ctx, out->digest, out->len);
    return FASTGIT_OK;
}

static fastgit_error_t keccak_reset(void* impl) {
    keccak_ctx_t* ctx = (keccak_ctx_t*)impl;
    memset(ctx->state, 0, sizeof(ctx->state));
    ctx->pos = 0;
    ctx->squeezing = false;
    return FASTGIT_OK;
}

static void keccak_free(void* impl) {
    (void)impl;
}

#define SHA3_IMPL(algo_name, algo_id, algo_name_str, algo_digest_len, algo_rate, algo_capacity, algo_delim) \
    typedef struct { keccak_ctx_t ctx; } fastgit_##algo_name##_impl_t; \
    static fastgit_error_t algo_name##_init(void* impl) { \
        return keccak_init(impl, algo_rate, algo_capacity, algo_delim); \
    } \
    static fastgit_error_t algo_name##_update(void* impl, const void* data, size_t len) { \
        return keccak_update(impl, data, len); \
    } \
    static fastgit_error_t algo_name##_final(void* impl, fastgit_hash_t* out) { \
        return keccak_final(impl, out); \
    } \
    static fastgit_error_t algo_name##_reset(void* impl) { \
        return keccak_reset(impl); \
    } \
    static void algo_name##_free(void* impl) { \
        keccak_free(impl); \
    } \
    static const fastgit_hash_vtable_t algo_name##_vtable = { \
        .algo = algo_id, \
        .name = algo_name_str, \
        .digest_len = algo_digest_len, \
        .block_size = algo_rate, \
        .ctx_size = sizeof(fastgit_##algo_name##_impl_t), \
        .init = algo_name##_init, \
        .update = algo_name##_update, \
        .final = algo_name##_final, \
        .reset = algo_name##_reset, \
        .free = algo_name##_free, \
    }; \
    const fastgit_hash_vtable_t* fastgit_##algo_name##_vtable(void) { \
        return &algo_name##_vtable; \
    }

SHA3_IMPL(sha3_256, FASTGIT_HASH_SHA3_256, "sha3-256", 32, 136, 64, 0x06)
SHA3_IMPL(sha3_384, FASTGIT_HASH_SHA3_384, "sha3-384", 48, 104, 96, 0x06)
SHA3_IMPL(sha3_512, FASTGIT_HASH_SHA3_512, "sha3-512", 64, 72, 128, 0x06)
SHA3_IMPL(shake128, FASTGIT_HASH_SHAKE128, "shake128", 32, 168, 32, 0x1F)
SHA3_IMPL(shake256, FASTGIT_HASH_SHAKE256, "shake256", 64, 136, 64, 0x1F)

#endif

#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
#include <openssl/evp.h>

typedef struct {
    EVP_MD_CTX* ctx;
    const EVP_MD* md;
    size_t digest_len;
    bool is_shake;
} fastgit_sha3_openssl_impl_t;

#define SHA3_OPENSSL_IMPL(impl_name, impl_algo_id, impl_name_str, impl_digest_len, evp_md_fn, impl_is_shake) \
    typedef struct { \
        EVP_MD_CTX* ctx; \
    } fastgit_##impl_name##_openssl_impl_t; \
    static fastgit_error_t impl_name##_openssl_init(void* impl) { \
        fastgit_##impl_name##_openssl_impl_t* ctx = (fastgit_##impl_name##_openssl_impl_t*)impl; \
        ctx->ctx = EVP_MD_CTX_new(); \
        if (!ctx->ctx) return FASTGIT_ENOMEM; \
        if (EVP_DigestInit_ex(ctx->ctx, evp_md_fn(), NULL) != 1) { \
            EVP_MD_CTX_free(ctx->ctx); \
            ctx->ctx = NULL; \
            return FASTGIT_ERROR; \
        } \
        return FASTGIT_OK; \
    } \
    static fastgit_error_t impl_name##_openssl_update(void* impl, const void* data, size_t len) { \
        fastgit_##impl_name##_openssl_impl_t* ctx = (fastgit_##impl_name##_openssl_impl_t*)impl; \
        return EVP_DigestUpdate(ctx->ctx, data, len) == 1 ? FASTGIT_OK : FASTGIT_ERROR; \
    } \
    static fastgit_error_t impl_name##_openssl_final(void* impl, fastgit_hash_t* out) { \
        fastgit_##impl_name##_openssl_impl_t* ctx = (fastgit_##impl_name##_openssl_impl_t*)impl; \
        unsigned int len = impl_digest_len; \
        int ret = impl_is_shake ? EVP_DigestFinalXOF(ctx->ctx, out->digest, len) \
                           : EVP_DigestFinal_ex(ctx->ctx, out->digest, &len); \
        EVP_MD_CTX_free(ctx->ctx); \
        ctx->ctx = NULL; \
        return ret == 1 ? FASTGIT_OK : FASTGIT_ERROR; \
    } \
    static fastgit_error_t impl_name##_openssl_reset(void* impl) { \
        return impl_name##_openssl_init(impl); \
    } \
    static void impl_name##_openssl_free(void* impl) { \
        fastgit_##impl_name##_openssl_impl_t* ctx = (fastgit_##impl_name##_openssl_impl_t*)impl; \
        if (ctx->ctx) { \
            EVP_MD_CTX_free(ctx->ctx); \
            ctx->ctx = NULL; \
        } \
    } \
    static const fastgit_hash_vtable_t impl_name##_openssl_vtable = { \
        .algo = impl_algo_id, \
        .name = impl_name_str, \
        .digest_len = impl_digest_len, \
        .block_size = 0, \
        .ctx_size = sizeof(fastgit_##impl_name##_openssl_impl_t), \
        .init = impl_name##_openssl_init, \
        .update = impl_name##_openssl_update, \
        .final = impl_name##_openssl_final, \
        .reset = impl_name##_openssl_reset, \
        .free = impl_name##_openssl_free, \
    }; \
    const fastgit_hash_vtable_t* fastgit_##impl_name##_vtable(void) { \
        return &impl_name##_openssl_vtable; \
    }

SHA3_OPENSSL_IMPL(sha3_256, FASTGIT_HASH_SHA3_256, "sha3-256", 32, EVP_sha3_256, false)
SHA3_OPENSSL_IMPL(sha3_384, FASTGIT_HASH_SHA3_384, "sha3-384", 48, EVP_sha3_384, false)
SHA3_OPENSSL_IMPL(sha3_512, FASTGIT_HASH_SHA3_512, "sha3-512", 64, EVP_sha3_512, false)
SHA3_OPENSSL_IMPL(shake128, FASTGIT_HASH_SHAKE128, "shake128", 32, EVP_shake128, true)
SHA3_OPENSSL_IMPL(shake256, FASTGIT_HASH_SHAKE256, "shake256", 64, EVP_shake256, true)

#endif
