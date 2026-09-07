#include "sha1.h"
#include <string.h>
#include <stdlib.h>
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
#include <openssl/evp.h>
#else
#include <stdint.h>
static const uint32_t K1[80] = {0};
#endif
typedef struct {
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    EVP_MD_CTX* ctx;
#else
    uint8_t _pad[128];
#endif
} fastgit_sha1_impl_t;
static fastgit_error_t sha1_init(void* impl){
    fastgit_sha1_impl_t* c=(fastgit_sha1_impl_t*)impl;
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    c->ctx=EVP_MD_CTX_new();
    if(!c->ctx) return FASTGIT_ENOMEM;
    if(EVP_DigestInit_ex(c->ctx, EVP_sha1(), NULL)!=1){ EVP_MD_CTX_free(c->ctx); c->ctx=NULL; return FASTGIT_ERROR; }
    return FASTGIT_OK;
#else
    (void)c; return FASTGIT_EUNSUPPORTED;
#endif
}
static fastgit_error_t sha1_update(void* impl, const void* data, size_t len){
    fastgit_sha1_impl_t* c=(fastgit_sha1_impl_t*)impl;
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    return EVP_DigestUpdate(c->ctx, data, len)==1?FASTGIT_OK:FASTGIT_ERROR;
#else
    (void)c;(void)data;(void)len; return FASTGIT_EUNSUPPORTED;
#endif
}
static fastgit_error_t sha1_final(void* impl, fastgit_hash_t* out){
    if(!out) return FASTGIT_EINVAL;
    fastgit_sha1_impl_t* c=(fastgit_sha1_impl_t*)impl;
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    unsigned int l=20;
    int r=EVP_DigestFinal_ex(c->ctx, out->digest, &l);
    EVP_MD_CTX_free(c->ctx); c->ctx=NULL;
    return r==1?FASTGIT_OK:FASTGIT_ERROR;
#else
    (void)c; return FASTGIT_EUNSUPPORTED;
#endif
}
static fastgit_error_t sha1_reset(void* impl){ return sha1_init(impl); }
static void sha1_free(void* impl){
    fastgit_sha1_impl_t* c=(fastgit_sha1_impl_t*)impl;
#if defined(FASTGIT_HAVE_OPENSSL) && FASTGIT_HAVE_OPENSSL
    if(c->ctx){ EVP_MD_CTX_free(c->ctx); c->ctx=NULL; }
#else
    (void)c;
#endif
}
static const fastgit_hash_vtable_t sha1_vtable = {
    .algo = FASTGIT_HASH_SHA1,
    .name = "sha1",
    .digest_len = 20,
    .block_size = 64,
    .ctx_size = sizeof(fastgit_sha1_impl_t),
    .init = sha1_init,
    .update = sha1_update,
    .final = sha1_final,
    .reset = sha1_reset,
    .free = sha1_free,
};
const fastgit_hash_vtable_t* fastgit_sha1_vtable(void){ return &sha1_vtable; }
