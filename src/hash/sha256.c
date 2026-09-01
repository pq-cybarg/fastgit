#include "sha256.h"
#include <string.h>
#include <stdlib.h>

#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
#include <openssl/sha.h>
#include <openssl/evp.h>
#else
#include <stdint.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHR(x, n) ((x) >> (n))
#define SIGMA0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIGMA1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define sigma0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define sigma1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))
#define CH(x, y, z) ((x & y) ^ (~x & z))
#define MAJ(x, y, z) ((x & y) ^ (x & z) ^ (y & z))

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} sha256_ctx_t;

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               (uint32_t)block[i*4+3];
    }

    for (int i = 16; i < 64; i++) {
        W[i] = sigma1(W[i-2]) + W[i-7] + sigma0(W[i-15]) + W[i-16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t T1 = h + SIGMA1(e) + CH(e, f, g) + K[i] + W[i];
        uint32_t T2 = SIGMA0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}
#endif

typedef struct {
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    EVP_MD_CTX* ctx;
#else
    sha256_ctx_t ctx;
#endif
} fastgit_sha256_impl_t;

static fastgit_error_t sha256_init(void* impl) {
    fastgit_sha256_impl_t* ctx = (fastgit_sha256_impl_t*)impl;
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    ctx->ctx = EVP_MD_CTX_new();
    if (!ctx->ctx) return FASTGIT_ENOMEM;
    if (EVP_DigestInit_ex(ctx->ctx, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(ctx->ctx);
        ctx->ctx = NULL;
        return FASTGIT_ERROR;
    }
#else
    ctx->ctx.state[0] = 0x6a09e667;
    ctx->ctx.state[1] = 0xbb67ae85;
    ctx->ctx.state[2] = 0x3c6ef372;
    ctx->ctx.state[3] = 0xa54ff53a;
    ctx->ctx.state[4] = 0x510e527f;
    ctx->ctx.state[5] = 0x9b05688c;
    ctx->ctx.state[6] = 0x1f83d9ab;
    ctx->ctx.state[7] = 0x5be0cd19;
    ctx->ctx.count = 0;
#endif
    return FASTGIT_OK;
}

static fastgit_error_t sha256_update(void* impl, const void* data, size_t len) {
    fastgit_sha256_impl_t* ctx = (fastgit_sha256_impl_t*)impl;
    const uint8_t* input = (const uint8_t*)data;

#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    return EVP_DigestUpdate(ctx->ctx, input, len) == 1 ? FASTGIT_OK : FASTGIT_ERROR;
#else
    while (len > 0) {
        size_t buf_pos = ctx->ctx.count % 64;
        size_t take = 64 - buf_pos;
        if (take > len) take = len;

        memcpy(ctx->ctx.buffer + buf_pos, input, take);
        ctx->ctx.count += take;
        input += take;
        len -= take;

        if (ctx->ctx.count % 64 == 0) {
            sha256_transform(ctx->ctx.state, ctx->ctx.buffer);
        }
    }
    return FASTGIT_OK;
#endif
}

static fastgit_error_t sha256_final(void* impl, fastgit_hash_t* out) {
    if (!out) return FASTGIT_EINVAL;
    fastgit_sha256_impl_t* ctx = (fastgit_sha256_impl_t*)impl;

#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    unsigned int len = 32;
    int ret = EVP_DigestFinal_ex(ctx->ctx, out->digest, &len);
    EVP_MD_CTX_free(ctx->ctx);
    ctx->ctx = NULL;
    return ret == 1 ? FASTGIT_OK : FASTGIT_ERROR;
#else
    size_t buf_pos = ctx->ctx.count % 64;
    ctx->ctx.buffer[buf_pos++] = 0x80;

    if (buf_pos > 56) {
        memset(ctx->ctx.buffer + buf_pos, 0, 64 - buf_pos);
        sha256_transform(ctx->ctx.state, ctx->ctx.buffer);
        buf_pos = 0;
    }

    memset(ctx->ctx.buffer + buf_pos, 0, 56 - buf_pos);

    uint64_t bits = ctx->ctx.count * 8;
    for (int i = 0; i < 8; i++) {
        ctx->ctx.buffer[56 + i] = (bits >> (56 - i * 8)) & 0xFF;
    }
    sha256_transform(ctx->ctx.state, ctx->ctx.buffer);

    for (int i = 0; i < 8; i++) {
        out->digest[i*4] = (ctx->ctx.state[i] >> 24) & 0xFF;
        out->digest[i*4+1] = (ctx->ctx.state[i] >> 16) & 0xFF;
        out->digest[i*4+2] = (ctx->ctx.state[i] >> 8) & 0xFF;
        out->digest[i*4+3] = ctx->ctx.state[i] & 0xFF;
    }
    return FASTGIT_OK;
#endif
}

static fastgit_error_t sha256_reset(void* impl) {
    return sha256_init(impl);
}

static void sha256_free(void* impl) {
    fastgit_sha256_impl_t* ctx = (fastgit_sha256_impl_t*)impl;
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    if (ctx->ctx) {
        EVP_MD_CTX_free(ctx->ctx);
        ctx->ctx = NULL;
    }
#else
    (void)ctx;
#endif
}

static const fastgit_hash_vtable_t sha256_vtable = {
    .algo = FASTGIT_HASH_SHA256,
    .name = "sha256",
    .digest_len = 32,
    .block_size = 64,
    .ctx_size = sizeof(fastgit_sha256_impl_t),
    .init = sha256_init,
    .update = sha256_update,
    .final = sha256_final,
    .reset = sha256_reset,
    .free = sha256_free,
};

const fastgit_hash_vtable_t* fastgit_sha256_vtable(void) {
    return &sha256_vtable;
}
