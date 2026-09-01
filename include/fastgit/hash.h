#ifndef FASTGIT_HASH_H
#define FASTGIT_HASH_H

#include "fastgit/fastgit.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FASTGIT_HASH_SHA256 0x01
#define FASTGIT_HASH_SHA384 0x02
#define FASTGIT_HASH_SHA3_256 0x03
#define FASTGIT_HASH_SHA3_384 0x04
#define FASTGIT_HASH_SHA3_512 0x05
#define FASTGIT_HASH_SHAKE128 0x06
#define FASTGIT_HASH_SHAKE256 0x07

#define FASTGIT_HASH_MAX_DIGEST 64

typedef struct {
    uint8_t digest[FASTGIT_HASH_MAX_DIGEST];
    size_t len;
    uint8_t algo;
} fastgit_hash_t;

typedef struct fastgit_hash_ctx fastgit_hash_ctx_t;

typedef fastgit_error_t (*fastgit_hash_init_fn)(void* ctx);
typedef fastgit_error_t (*fastgit_hash_update_fn)(void* ctx, const void* data, size_t len);
typedef fastgit_error_t (*fastgit_hash_final_fn)(void* ctx, fastgit_hash_t* out);
typedef fastgit_error_t (*fastgit_hash_reset_fn)(void* ctx);
typedef void (*fastgit_hash_free_fn)(void* ctx);

typedef struct {
    uint8_t algo;
    const char* name;
    size_t digest_len;
    size_t block_size;
    size_t ctx_size;
    fastgit_hash_init_fn init;
    fastgit_hash_update_fn update;
    fastgit_hash_final_fn final;
    fastgit_hash_reset_fn reset;
    fastgit_hash_free_fn free;
} fastgit_hash_vtable_t;

const fastgit_hash_vtable_t* fastgit_hash_get_vtable(uint8_t algo);
bool fastgit_hash_is_supported(uint8_t algo);

fastgit_error_t fastgit_hash(uint8_t algo, const void* data, size_t len, fastgit_hash_t* out);
fastgit_error_t fastgit_hash_file(uint8_t algo, const char* path, fastgit_hash_t* out);
fastgit_error_t fastgit_hash_stream(uint8_t algo, FILE* stream, fastgit_hash_t* out);

fastgit_hash_ctx_t* fastgit_hash_ctx_new(uint8_t algo);
fastgit_error_t fastgit_hash_ctx_init(fastgit_hash_ctx_t* ctx);
fastgit_error_t fastgit_hash_ctx_update(fastgit_hash_ctx_t* ctx, const void* data, size_t len);
fastgit_error_t fastgit_hash_ctx_final(fastgit_hash_ctx_t* ctx, fastgit_hash_t* out);
fastgit_error_t fastgit_hash_ctx_reset(fastgit_hash_ctx_t* ctx);
void fastgit_hash_ctx_free(fastgit_hash_ctx_t* ctx);

fastgit_error_t fastgit_hash_oid(uint8_t algo, const void* data, size_t len, uint8_t* out, size_t* out_len);
fastgit_error_t fastgit_hash_oid_from_hash(const fastgit_hash_t* hash, uint8_t* out, size_t* out_len);

bool fastgit_hash_equal(const fastgit_hash_t* a, const fastgit_hash_t* b);
int fastgit_hash_compare(const fastgit_hash_t* a, const fastgit_hash_t* b);

void fastgit_hash_to_hex(const fastgit_hash_t* hash, char* out, size_t out_len);
fastgit_error_t fastgit_hash_from_hex(uint8_t algo, const char* hex, fastgit_hash_t* out);

const char* fastgit_hash_algo_name(uint8_t algo);
size_t fastgit_hash_algo_digest_len(uint8_t algo);
size_t fastgit_hash_algo_block_size(uint8_t algo);

#ifdef __cplusplus
}
#endif

#endif
