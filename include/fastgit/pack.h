#ifndef FASTGIT_PACK_H
#define FASTGIT_PACK_H

#include "fastgit/object.h"
#include "fastgit/odb.h"
#include "fastgit/hash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FASTGIT_PACK_VERSION 2
#define FASTGIT_PACK_SIGNATURE 0x5041434b

typedef enum {
    FASTGIT_PACK_OBJ_COMMIT = 1,
    FASTGIT_PACK_OBJ_TREE = 2,
    FASTGIT_PACK_OBJ_BLOB = 3,
    FASTGIT_PACK_OBJ_TAG = 4,
    FASTGIT_PACK_OBJ_OFS_DELTA = 6,
    FASTGIT_PACK_OBJ_REF_DELTA = 7,
} fastgit_pack_obj_type_t;

typedef struct fastgit_pack fastgit_pack_t;
typedef struct fastgit_pack_index fastgit_pack_index_t;
typedef struct fastgit_midx fastgit_midx_t;

typedef struct {
    uint32_t fanout[256];
    fastgit_oid_t* oids;
    uint32_t* crc32s;
    uint64_t* offsets;
    uint32_t count;
    uint32_t version;
    fastgit_hash_t pack_checksum;
} fastgit_pack_index_data_t;

struct fastgit_pack_index {
    fastgit_pack_index_data_t data;
    int fd;
    void* mapped;
    size_t mapped_size;
    bool own_mapping;
};

typedef struct fastgit_midx fastgit_midx_t;

fastgit_error_t fastgit_pack_open(const char* pack_file, const char* idx_file, fastgit_pack_t** out);
fastgit_error_t fastgit_pack_create(const char* pack_file, const char* idx_file, fastgit_pack_t** out);
void fastgit_pack_close(fastgit_pack_t* pack);

fastgit_error_t fastgit_pack_read_entry(fastgit_pack_t* pack, const fastgit_oid_t* oid, fastgit_odb_object_t* out);
fastgit_error_t fastgit_pack_read_header(fastgit_pack_t* pack, const fastgit_oid_t* oid, fastgit_obj_type_t* type, size_t* size);
fastgit_error_t fastgit_pack_exists(fastgit_pack_t* pack, const fastgit_oid_t* oid);

fastgit_error_t fastgit_pack_write(fastgit_pack_t* pack, const fastgit_oid_t* objects, size_t count);
fastgit_error_t fastgit_pack_add_object(fastgit_pack_t* pack, fastgit_obj_type_t type, const void* data, size_t len, fastgit_oid_t* out);

fastgit_error_t fastgit_pack_index_load(const char* idx_file, fastgit_pack_index_t** out);
fastgit_error_t fastgit_pack_index_create(const char* idx_file, fastgit_pack_t* pack);
void fastgit_pack_index_free(fastgit_pack_index_t* idx);

fastgit_error_t fastgit_pack_index_find(fastgit_pack_index_t* idx, const fastgit_oid_t* oid, uint32_t* index_out);

const fastgit_pack_index_data_t* fastgit_pack_index_data(fastgit_pack_index_t* idx);

fastgit_error_t fastgit_midx_create(const char* midx_file, const char** pack_dirs, size_t dir_count);
fastgit_error_t fastgit_midx_open(const char* midx_file, fastgit_midx_t** out);
void fastgit_midx_free(fastgit_midx_t* midx);

fastgit_error_t fastgit_midx_find(fastgit_midx_t* midx, const fastgit_oid_t* oid, fastgit_pack_t** pack_out, uint32_t* index_out);

typedef struct {
    uint32_t pack_count;
    uint32_t object_count;
    uint64_t total_size;
    fastgit_hash_t midx_checksum;
} fastgit_midx_stats_t;

void fastgit_midx_stats(fastgit_midx_t* midx, fastgit_midx_stats_t* out);

fastgit_error_t fastgit_delta_compress(const void* base, size_t base_len, const void* target, size_t target_len, void** out, size_t* out_len);
fastgit_error_t fastgit_delta_apply(const void* base, size_t base_len, const void* delta, size_t delta_len, void** out, size_t* out_len);

typedef struct {
    uint64_t objects_read;
    uint64_t objects_written;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t deltas_created;
    uint64_t deltas_applied;
    double compression_ratio;
} fastgit_pack_stats_t;

void fastgit_pack_stats(fastgit_pack_t* pack, fastgit_pack_stats_t* out);

static inline size_t fastgit_encode_varint(uint64_t val, uint8_t* buf) {
    size_t len = 0;
    while (val >= 0x80) {
        buf[len++] = (val & 0x7F) | 0x80;
        val >>= 7;
    }
    buf[len++] = val & 0x7F;
    return len;
}

static inline size_t fastgit_decode_varint(const uint8_t* buf, size_t len, uint64_t* out) {
    uint64_t val = 0;
    size_t shift = 0;
    for (size_t i = 0; i < len; i++) {
        val |= (uint64_t)(buf[i] & 0x7F) << shift;
        if ((buf[i] & 0x80) == 0) {
            *out = val;
            return i + 1;
        }
        shift += 7;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif

