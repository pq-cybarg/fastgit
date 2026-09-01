#ifndef FASTGIT_ODB_H
#define FASTGIT_ODB_H

#include "fastgit/object.h"
#include "fastgit/hash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fastgit_odb fastgit_odb_t;

typedef enum {
    FASTGIT_ODB_LOOSE = 1,
    FASTGIT_ODB_PACK = 2,
} fastgit_odb_type_t;

typedef struct {
    fastgit_oid_t oid;
    fastgit_obj_type_t type;
    size_t size;
    void* data;
    fastgit_odb_type_t source;
} fastgit_odb_object_t;

fastgit_error_t fastgit_odb_new(const char* path, fastgit_odb_t** out);
fastgit_error_t fastgit_odb_open(const char* path, fastgit_odb_t** out);
void fastgit_odb_free(fastgit_odb_t* odb);

fastgit_error_t fastgit_odb_read(fastgit_odb_t* odb, const fastgit_oid_t* oid, fastgit_odb_object_t* out);
fastgit_error_t fastgit_odb_read_header(fastgit_odb_t* odb, const fastgit_oid_t* oid, fastgit_obj_type_t* type, size_t* size);
fastgit_error_t fastgit_odb_write(fastgit_odb_t* odb, fastgit_obj_type_t type, const void* data, size_t len, fastgit_oid_t* out);
fastgit_error_t fastgit_odb_exists(fastgit_odb_t* odb, const fastgit_oid_t* oid);
fastgit_error_t fastgit_odb_delete(fastgit_odb_t* odb, const fastgit_oid_t* oid);

typedef struct fastgit_odb_iterator fastgit_odb_iterator_t;

fastgit_error_t fastgit_odb_iterator_new(fastgit_odb_t* odb, fastgit_odb_iterator_t** out);
fastgit_error_t fastgit_odb_iterator_next(fastgit_odb_iterator_t* iter, fastgit_oid_t* out);
void fastgit_odb_iterator_free(fastgit_odb_iterator_t* iter);

fastgit_error_t fastgit_odb_add_backend(fastgit_odb_t* odb, fastgit_odb_t* backend, int priority);
fastgit_error_t fastgit_odb_read_prefix(fastgit_odb_t* odb, const uint8_t* prefix, size_t prefix_len, fastgit_odb_object_t* out);

typedef struct {
    uint64_t loose_count;
    uint64_t pack_count;
    uint64_t loose_size;
    uint64_t pack_size;
    uint64_t reads;
    uint64_t writes;
    uint64_t cache_hits;
    uint64_t cache_misses;
} fastgit_odb_stats_t;

void fastgit_odb_stats(fastgit_odb_t* odb, fastgit_odb_stats_t* out);
void fastgit_odb_stats_reset(fastgit_odb_t* odb);

fastgit_error_t fastgit_odb_pack(fastgit_odb_t* odb, const fastgit_oid_t* objects, size_t count);
fastgit_error_t fastgit_odb_gc(fastgit_odb_t* odb);

#ifdef __cplusplus
}
#endif

#endif
