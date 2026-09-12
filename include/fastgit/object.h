#ifndef FASTGIT_OBJECT_H
#define FASTGIT_OBJECT_H

#include "fastgit/fastgit.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fastgit_object {
    fastgit_oid_t oid;
    fastgit_obj_type_t type;
    size_t size;
    void* data;
    void (*free_data)(void*);
};

typedef struct fastgit_object fastgit_object_t;

typedef struct {
    void* data;
    size_t len;
} fastgit_buf_t;

typedef struct {
    fastgit_oid_t oid;
    fastgit_obj_type_t type;
    size_t size;
    void* data;
} fastgit_object_raw_t;

fastgit_error_t fastgit_object_parse(fastgit_obj_type_t type, const void* data, size_t len, fastgit_object_t** out);
fastgit_error_t fastgit_object_raw_parse(const fastgit_oid_t* oid, fastgit_obj_type_t type, const void* data, size_t len, fastgit_object_raw_t* out);
void fastgit_object_raw_free(fastgit_object_raw_t* raw);
fastgit_error_t fastgit_object_serialize(const fastgit_object_t* obj, uint8_t hash_algo, fastgit_buf_t* out);
fastgit_error_t fastgit_object_hash(const fastgit_object_t* obj, uint8_t hash_algo, fastgit_oid_t* out);

fastgit_oid_t* fastgit_oid_dup(const fastgit_oid_t* oid);
void fastgit_oid_free(fastgit_oid_t* oid);
int fastgit_oid_cmp(const fastgit_oid_t* a, const fastgit_oid_t* b);
bool fastgit_oid_equal(const fastgit_oid_t* a, const fastgit_oid_t* b);
void fastgit_oid_to_hex(const fastgit_oid_t* oid, char* out, size_t out_len);
fastgit_error_t fastgit_oid_from_hex(const char* hex, fastgit_oid_t* out);
const char* fastgit_obj_type_name(fastgit_obj_type_t type);
fastgit_obj_type_t fastgit_obj_type_from_name(const char* name);

typedef struct {
    fastgit_oid_t oid;
    char* path;
    uint32_t mode;
} fastgit_tree_entry_t;

typedef struct fastgit_signature {
    char* name;
    char* email;
    int64_t when;
    int offset;
} fastgit_signature_t;

struct fastgit_commit {
    fastgit_oid_t tree;
    size_t parent_count;
    fastgit_oid_t* parents;
    fastgit_signature_t* author;
    fastgit_signature_t* committer;
    char* message;
    char* encoding;
    char* gpg_signature;
};

struct fastgit_tag {
    fastgit_oid_t object;
    fastgit_obj_type_t object_type;
    char* name;
    fastgit_signature_t* tagger;
    char* message;
    char* gpg_signature;
};

fastgit_error_t fastgit_tree_add_entry(fastgit_object_t* tree, const fastgit_tree_entry_t* entry);
fastgit_error_t fastgit_tree_remove_entry(fastgit_object_t* tree, const char* path);
size_t fastgit_tree_entry_count(const fastgit_object_t* tree);
const fastgit_tree_entry_t* fastgit_tree_entry_by_index(const fastgit_object_t* tree, size_t index);
const fastgit_tree_entry_t* fastgit_tree_entry_by_name(const fastgit_object_t* tree, const char* path);

const fastgit_commit_t* fastgit_commit_parse(const fastgit_object_t* obj);
void fastgit_commit_free(fastgit_commit_t* commit);

fastgit_error_t fastgit_tag_create(const fastgit_tag_t* tag, fastgit_object_t** out);
const fastgit_tag_t* fastgit_tag_parse(const fastgit_object_t* obj);
void fastgit_tag_free(fastgit_tag_t* tag);

fastgit_error_t fastgit_blob_create(const void* data, size_t len, fastgit_object_t** out);
const void* fastgit_blob_data(const fastgit_object_t* obj);
size_t fastgit_blob_size(const fastgit_object_t* obj);

fastgit_error_t fastgit_signature_new(const char* name, const char* email, int64_t when, int offset, fastgit_signature_t** out);
fastgit_error_t fastgit_signature_default(fastgit_signature_t** out);
void fastgit_signature_free(fastgit_signature_t* sig);
void fastgit_signature_to_buf(const fastgit_signature_t* sig, char** out, size_t* out_len);

typedef struct {
    uint8_t algo;
    uint8_t* data;
    size_t len;
} fastgit_signature_data_t;

fastgit_error_t fastgit_object_sign(const fastgit_object_t* obj, const fastgit_signature_data_t* sig, fastgit_buf_t* out);
fastgit_error_t fastgit_object_verify(const fastgit_object_t* obj, const fastgit_signature_data_t* sig);

#ifdef __cplusplus
}
#endif

#endif

