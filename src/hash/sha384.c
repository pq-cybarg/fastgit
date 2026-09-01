#include "sha384.h"
#include <string.h>
#include <stdlib.h>

#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
#include <openssl/sha.h>
#include <openssl/evp.h>
#else
#include <stdint.h>

static const uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
    0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
    0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
    0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
    0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
    0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
    0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
    0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
    0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
    0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
    0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

#define ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define SHR64(x, n) ((x) >> (n))
#define SIGMA0_64(x) (ROTR64(x, 28) ^ ROTR64(x, 34) ^ ROTR64(x, 39))
#define SIGMA1_64(x) (ROTR64(x, 14) ^ ROTR64(x, 18) ^ ROTR64(x, 41))
#define sigma0_64(x) (ROTR64(x, 1) ^ ROTR64(x, 8) ^ SHR64(x, 7))
#define sigma1_64(x) (ROTR64(x, 19) ^ ROTR64(x, 61) ^ SHR64(x, 6))
#define CH64(x, y, z) ((x & y) ^ (~x & z))
#define MAJ64(x, y, z) ((x & y) ^ (x & z) ^ (y & z))

typedef struct {
    uint64_t state[8];
    uint64_t count;
    uint8_t buffer[128];
} sha512_ctx_t;

static void sha512_transform(uint64_t state[8], const uint8_t block[128]) {
    uint64_t W[80];
    uint64_t a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; i++) {
        W[i] = ((uint64_t)block[i*8] << 56) |
               ((uint64_t)block[i*8+1] << 48) |
               ((uint64_t)block[i*8+2] << 40) |
               ((uint64_t)block[i*8+3] << 32) |
               ((uint64_t)block[i*8+4] << 24) |
               ((uint64_t)block[i*8+5] << 16) |
               ((uint64_t)block[i*8+6] << 8) |
               (uint64_t)block[i*8+7];
    }

    for (int i = 16; i < 80; i++) {
        W[i] = sigma1_64(W[i-2]) + W[i-7] + sigma0_64(W[i-15]) + W[i-16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (int i = 0; i < 80; i++) {
        uint64_t T1 = h + SIGMA1_64(e) + CH64(e, f, g) + K[i] + W[i];
        uint64_t T2 = SIGMA0_64(a) + MAJ64(a, b, c);
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
    sha512_ctx_t ctx;
#endif
} fastgit_sha384_impl_t;

static fastgit_error_t sha384_init(void* impl) {
    fastgit_sha384_impl_t* ctx = (fastgit_sha384_impl_t*)impl;
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    ctx->ctx = EVP_MD_CTX_new();
    if (!ctx->ctx) return FASTGIT_ENOMEM;
    if (EVP_DigestInit_ex(ctx->ctx, EVP_sha384(), NULL) != 1) {
        EVP_MD_CTX_free(ctx->ctx);
        ctx->ctx = NULL;
        return FASTGIT_ERROR;
    }
#else
    ctx->ctx.state[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->ctx.state[1] = 0x629a292a367cd507ULL;
    ctx->ctx.state[2] = 0x9159015a3070dd17ULL;
    ctx->ctx.state[3] = 0x152fecd8f70e5939ULL;
    ctx->ctx.state[4] = 0x67332667ffc00b31ULL;
    ctx->ctx.state[5] = 0x8eb44a8768581511ULL;
    ctx->ctx.state[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->ctx.state[7] = 0x47b5481dbefa4fa4ULL;
    ctx->ctx.count = 0;
#endif
    return FASTGIT_OK;
}

static fastgit_error_t sha384_update(void* impl, const void* data, size_t len) {
    fastgit_sha384_impl_t* ctx = (fastgit_sha384_impl_t*)impl;
    const uint8_t* input = (const uint8_t*)data;

#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    return EVP_DigestUpdate(ctx->ctx, input, len) == 1 ? FASTGIT_OK : FASTGIT_ERROR;
#else
    while (len > 0) {
        size_t buf_pos = ctx->ctx.count % 128;
        size_t take = 128 - buf_pos;
        if (take > len) take = len;

        memcpy(ctx->ctx.buffer + buf_pos, input, take);
        ctx->ctx.count += take;
        input += take;
        len -= take;

        if (ctx->ctx.count % 128 == 0) {
            sha512_transform(ctx->ctx.state, ctx->ctx.buffer);
        }
    }
    return FASTGIT_OK;
#endif
}

static fastgit_error_t sha384_final(void* impl, fastgit_hash_t* out) {
    if (!out) return FASTGIT_EINVAL;
    fastgit_sha384_impl_t* ctx = (fastgit_sha384_impl_t*)impl;

#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    unsigned int len = 48;
    int ret = EVP_DigestFinal_ex(ctx->ctx, out->digest, &len);
    EVP_MD_CTX_free(ctx->ctx);
    ctx->ctx = NULL;
    return ret == 1 ? FASTGIT_OK : FASTGIT_ERROR;
#else
    size_t buf_pos = ctx->ctx.count % 128;
    ctx->ctx.buffer[buf_pos++] = 0x80;

    if (buf_pos > 112) {
        memset(ctx->ctx.buffer + buf_pos, 0, 128 - buf_pos);
        sha512_transform(ctx->ctx.state, ctx->ctx.buffer);
        buf_pos = 0;
    }

    memset(ctx->ctx.buffer + buf_pos, 0, 112 - buf_pos);

    uint64_t bits = ctx->ctx.count * 8;
    for (int i = 0; i < 16; i++) {
        ctx->ctx.buffer[112 + i] = (bits >> (120 - i * 8)) & 0xFF;
    }
    sha512_transform(ctx->ctx.state, ctx->ctx.buffer);

    for (int i = 0; i < 6; i++) {
        out->digest[i*8] = (ctx->ctx.state[i] >> 56) & 0xFF;
        out->digest[i*8+1] = (ctx->ctx.state[i] >> 48) & 0xFF;
        out->digest[i*8+2] = (ctx->ctx.state[i] >> 40) & 0xFF;
        out->digest[i*8+3] = (ctx->ctx.state[i] >> 32) & 0xFF;
        out->digest[i*8+4] = (ctx->ctx.state[i] >> 24) & 0xFF;
        out->digest[i*8+5] = (ctx->ctx.state[i] >> 16) & 0xFF;
        out->digest[i*8+6] = (ctx->ctx.state[i] >> 8) & 0xFF;
        out->digest[i*8+7] = ctx->ctx.state[i] & 0xFF;
    }
    return FASTGIT_OK;
#endif
}

static fastgit_error_t sha384_reset(void* impl) {
    return sha384_init(impl);
}

static void sha384_free(void* impl) {
    fastgit_sha384_impl_t* ctx = (fastgit_sha384_impl_t*)impl;
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    if (ctx->ctx) {
        EVP_MD_CTX_free(ctx->ctx);
        ctx->ctx = NULL;
    }
#else
    (void)ctx;
#endif
}

static const fastgit_hash_vtable_t sha384_vtable = {
    .algo = FASTGIT_HASH_SHA384,
    .name = "sha384",
    .digest_len = 48,
    .block_size = 128,
    .ctx_size = sizeof(fastgit_sha384_impl_t),
    .init = sha384_init,
    .update = sha384_update,
    .final = sha384_final,
    .reset = sha384_reset,
    .free = sha384_free,
};

const fastgit_hash_vtable_t* fastgit_sha384_vtable(void) {
    return &sha384_vtable;
}
