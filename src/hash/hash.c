#include "fastgit/hash.h"
#include "sha1.h"
#include "sha256.h"
#include "sha384.h"
#include "sha3.h"
#include <stdlib.h>
#include <string.h>
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
#include <openssl/evp.h>
#endif

struct fastgit_hash_ctx {
    const fastgit_hash_vtable_t* vtable;
    void* impl;
};

static const fastgit_hash_vtable_t* g_hash_vtables[256] = {0};

void fastgit_hash_register(const fastgit_hash_vtable_t* vtable) {
    if (vtable) {
        g_hash_vtables[vtable->algo] = vtable;
    }
}

const fastgit_hash_vtable_t* fastgit_hash_get_vtable(uint8_t algo) {
    return g_hash_vtables[algo];
}

bool fastgit_hash_is_supported(uint8_t algo) {
    return g_hash_vtables[algo] != NULL;
}

fastgit_error_t fastgit_hash(uint8_t algo, const void* data, size_t len, fastgit_hash_t* out) {
    if (!out) return FASTGIT_EINVAL;
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    if (algo == FASTGIT_HASH_SHA256) {
        static __thread EVP_MD_CTX* tls = NULL;
        if (!tls) tls = EVP_MD_CTX_new();
        if (tls) {
            unsigned int olen = 32;
            if (EVP_DigestInit_ex(tls, EVP_sha256(), NULL) == 1 &&
                EVP_DigestUpdate(tls, data, len) == 1 &&
                EVP_DigestFinal_ex(tls, out->digest, &olen) == 1) {
                out->algo = algo;
                out->len = 32;
                return FASTGIT_OK;
            }
        }
    }
    if (algo == FASTGIT_HASH_SHA384) {
        static __thread EVP_MD_CTX* tls384 = NULL;
        if (!tls384) tls384 = EVP_MD_CTX_new();
        if (tls384) {
            unsigned int olen = 48;
            if (EVP_DigestInit_ex(tls384, EVP_sha384(), NULL) == 1 &&
                EVP_DigestUpdate(tls384, data, len) == 1 &&
                EVP_DigestFinal_ex(tls384, out->digest, &olen) == 1) {
                out->algo = algo;
                out->len = 48;
                return FASTGIT_OK;
            }
        }
    }
    if (algo == FASTGIT_HASH_SHA3_256) {
        static __thread EVP_MD_CTX* tls3_256 = NULL;
        if (!tls3_256) tls3_256 = EVP_MD_CTX_new();
        if (tls3_256) {
            unsigned int olen = 32;
            if (EVP_DigestInit_ex(tls3_256, EVP_sha3_256(), NULL) == 1 &&
                EVP_DigestUpdate(tls3_256, data, len) == 1 &&
                EVP_DigestFinal_ex(tls3_256, out->digest, &olen) == 1) {
                out->algo = algo;
                out->len = 32;
                return FASTGIT_OK;
            }
        }
    }
    if (algo == FASTGIT_HASH_SHA3_384) {
        static __thread EVP_MD_CTX* tls3_384 = NULL;
        if (!tls3_384) tls3_384 = EVP_MD_CTX_new();
        if (tls3_384) {
            unsigned int olen = 48;
            if (EVP_DigestInit_ex(tls3_384, EVP_sha3_384(), NULL) == 1 &&
                EVP_DigestUpdate(tls3_384, data, len) == 1 &&
                EVP_DigestFinal_ex(tls3_384, out->digest, &olen) == 1) {
                out->algo = algo;
                out->len = 48;
                return FASTGIT_OK;
            }
        }
    }
    if (algo == FASTGIT_HASH_SHA3_512) {
        static __thread EVP_MD_CTX* tls3_512 = NULL;
        if (!tls3_512) tls3_512 = EVP_MD_CTX_new();
        if (tls3_512) {
            unsigned int olen = 64;
            if (EVP_DigestInit_ex(tls3_512, EVP_sha3_512(), NULL) == 1 &&
                EVP_DigestUpdate(tls3_512, data, len) == 1 &&
                EVP_DigestFinal_ex(tls3_512, out->digest, &olen) == 1) {
                out->algo = algo;
                out->len = 64;
                return FASTGIT_OK;
            }
        }
    }
#endif
    const fastgit_hash_vtable_t* vtable = fastgit_hash_get_vtable(algo);
    if (!vtable) return FASTGIT_EUNSUPPORTED;

    fastgit_hash_ctx_t* ctx = fastgit_hash_ctx_new(algo);
    if (!ctx) return FASTGIT_ENOMEM;

    fastgit_error_t err = fastgit_hash_ctx_init(ctx);
    if (err == FASTGIT_OK) {
        err = fastgit_hash_ctx_update(ctx, data, len);
    }
    if (err == FASTGIT_OK) {
        err = fastgit_hash_ctx_final(ctx, out);
    }
    fastgit_hash_ctx_free(ctx);
    return err;
}

fastgit_error_t fastgit_hash_file(uint8_t algo, const char* path, fastgit_hash_t* out) {
    FILE* f = fopen(path, "rb");
    if (!f) return FASTGIT_ENOENT;

    fastgit_error_t err = fastgit_hash_stream(algo, f, out);
    fclose(f);
    return err;
}

fastgit_error_t fastgit_hash_stream(uint8_t algo, FILE* stream, fastgit_hash_t* out) {
    const fastgit_hash_vtable_t* vtable = fastgit_hash_get_vtable(algo);
    if (!vtable) return FASTGIT_EUNSUPPORTED;

    fastgit_hash_ctx_t* ctx = fastgit_hash_ctx_new(algo);
    if (!ctx) return FASTGIT_ENOMEM;

    fastgit_error_t err = fastgit_hash_ctx_init(ctx);
    if (err != FASTGIT_OK) {
        fastgit_hash_ctx_free(ctx);
        return err;
    }

    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stream)) > 0) {
        err = fastgit_hash_ctx_update(ctx, buf, n);
        if (err != FASTGIT_OK) break;
    }

    if (err == FASTGIT_OK) {
        err = fastgit_hash_ctx_final(ctx, out);
    }

    fastgit_hash_ctx_free(ctx);
    return err;
}

fastgit_hash_ctx_t* fastgit_hash_ctx_new(uint8_t algo) {
    const fastgit_hash_vtable_t* vtable = fastgit_hash_get_vtable(algo);
    if (!vtable) return NULL;

    fastgit_hash_ctx_t* ctx = calloc(1, sizeof(fastgit_hash_ctx_t) + vtable->ctx_size);
    if (!ctx) return NULL;

    ctx->vtable = vtable;
    ctx->impl = (uint8_t*)ctx + sizeof(fastgit_hash_ctx_t);
    return ctx;
}

fastgit_error_t fastgit_hash_ctx_init(fastgit_hash_ctx_t* ctx) {
    if (!ctx || !ctx->vtable || !ctx->vtable->init) return FASTGIT_EINVAL;
    return ctx->vtable->init(ctx->impl);
}

fastgit_error_t fastgit_hash_ctx_update(fastgit_hash_ctx_t* ctx, const void* data, size_t len) {
    if (!ctx || !ctx->vtable || !ctx->vtable->update) return FASTGIT_EINVAL;
    return ctx->vtable->update(ctx->impl, data, len);
}

fastgit_error_t fastgit_hash_ctx_final(fastgit_hash_ctx_t* ctx, fastgit_hash_t* out) {
    if (!ctx || !ctx->vtable || !ctx->vtable->final) return FASTGIT_EINVAL;
    if (!out) return FASTGIT_EINVAL;
    out->algo = ctx->vtable->algo;
    out->len = ctx->vtable->digest_len;
    fastgit_error_t err = ctx->vtable->final(ctx->impl, out);
    return err;
}

fastgit_error_t fastgit_hash_ctx_reset(fastgit_hash_ctx_t* ctx) {
    if (!ctx || !ctx->vtable || !ctx->vtable->reset) return FASTGIT_EINVAL;
    return ctx->vtable->reset(ctx->impl);
}

void fastgit_hash_ctx_free(fastgit_hash_ctx_t* ctx) {
    if (ctx && ctx->vtable && ctx->vtable->free) {
        ctx->vtable->free(ctx->impl);
    }
    free(ctx);
}

fastgit_error_t fastgit_hash_oid(uint8_t algo, const void* data, size_t len, uint8_t* out, size_t* out_len) {
    fastgit_hash_t hash;
    fastgit_error_t err = fastgit_hash(algo, data, len, &hash);
    if (err != FASTGIT_OK) return err;

    if (*out_len < hash.len) return FASTGIT_EOVERFLOW;
    memcpy(out, hash.digest, hash.len);
    *out_len = hash.len;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_hash_oid_from_hash(const fastgit_hash_t* hash, uint8_t* out, size_t* out_len) {
    if (!hash || !out || !out_len) return FASTGIT_EINVAL;
    if (*out_len < hash->len) return FASTGIT_EOVERFLOW;
    memcpy(out, hash->digest, hash->len);
    *out_len = hash->len;
    return FASTGIT_OK;
}

bool fastgit_hash_equal(const fastgit_hash_t* a, const fastgit_hash_t* b) {
    if (!a || !b) return false;
    if (a->algo != b->algo || a->len != b->len) return false;
    return memcmp(a->digest, b->digest, a->len) == 0;
}

int fastgit_hash_compare(const fastgit_hash_t* a, const fastgit_hash_t* b) {
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    if (a->algo != b->algo) return a->algo - b->algo;
    size_t min_len = a->len < b->len ? a->len : b->len;
    int cmp = memcmp(a->digest, b->digest, min_len);
    if (cmp != 0) return cmp;
    return (int)a->len - (int)b->len;
}

static const char* hex_chars = "0123456789abcdef";

void fastgit_hash_to_hex(const fastgit_hash_t* hash, char* out, size_t out_len) {
    if (!hash || !out || out_len < hash->len * 2 + 1) return;
    for (size_t i = 0; i < hash->len; i++) {
        out[i * 2] = hex_chars[hash->digest[i] >> 4];
        out[i * 2 + 1] = hex_chars[hash->digest[i] & 0x0F];
    }
    out[hash->len * 2] = '\0';
}

fastgit_error_t fastgit_hash_from_hex(uint8_t algo, const char* hex, fastgit_hash_t* out) {
    if (!hex || !out) return FASTGIT_EINVAL;
    const fastgit_hash_vtable_t* vtable = fastgit_hash_get_vtable(algo);
    if (!vtable) return FASTGIT_EUNSUPPORTED;

    size_t hex_len = strlen(hex);
    if (hex_len != vtable->digest_len * 2) return FASTGIT_EINVAL;

    out->algo = algo;
    out->len = vtable->digest_len;

    for (size_t i = 0; i < vtable->digest_len; i++) {
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        uint8_t val = 0;

        if (high >= '0' && high <= '9') val = (high - '0') << 4;
        else if (high >= 'a' && high <= 'f') val = (high - 'a' + 10) << 4;
        else if (high >= 'A' && high <= 'F') val = (high - 'A' + 10) << 4;
        else return FASTGIT_EINVAL;

        if (low >= '0' && low <= '9') val |= (low - '0');
        else if (low >= 'a' && low <= 'f') val |= (low - 'a' + 10);
        else if (low >= 'A' && low <= 'F') val |= (low - 'A' + 10);
        else return FASTGIT_EINVAL;

        out->digest[i] = val;
    }
    return FASTGIT_OK;
}

const char* fastgit_hash_algo_name(uint8_t algo) {
    const fastgit_hash_vtable_t* vtable = fastgit_hash_get_vtable(algo);
    return vtable ? vtable->name : "unknown";
}

size_t fastgit_hash_algo_digest_len(uint8_t algo) {
    const fastgit_hash_vtable_t* vtable = fastgit_hash_get_vtable(algo);
    return vtable ? vtable->digest_len : 0;
}

size_t fastgit_hash_algo_block_size(uint8_t algo) {
    const fastgit_hash_vtable_t* vtable = fastgit_hash_get_vtable(algo);
    return vtable ? vtable->block_size : 0;
}

__attribute__((constructor))
static void fastgit_hash_register_all(void) {
    fastgit_hash_register(fastgit_sha1_vtable());
    fastgit_hash_register(fastgit_sha256_vtable());
    fastgit_hash_register(fastgit_sha384_vtable());
    fastgit_hash_register(fastgit_sha3_256_vtable());
    fastgit_hash_register(fastgit_sha3_384_vtable());
    fastgit_hash_register(fastgit_sha3_512_vtable());
    fastgit_hash_register(fastgit_shake128_vtable());
    fastgit_hash_register(fastgit_shake256_vtable());
}

