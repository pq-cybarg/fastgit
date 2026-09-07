#ifndef FASTGIT_H
#define FASTGIT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FASTGIT_VERSION_MAJOR 0
#define FASTGIT_VERSION_MINOR 1
#define FASTGIT_VERSION_PATCH 0

#define FASTGIT_HASH_SHA1 0x00
#define FASTGIT_HASH_SHA256 0x01
#define FASTGIT_HASH_SHA384 0x02
#define FASTGIT_HASH_SHA3_256 0x03
#define FASTGIT_HASH_SHA3_384 0x04
#define FASTGIT_HASH_SHA3_512 0x05
#define FASTGIT_HASH_SHAKE128 0x06
#define FASTGIT_HASH_SHAKE256 0x07

#define FASTGIT_OID_MAX_LEN 64

typedef enum {
    FASTGIT_OK = 0,
    FASTGIT_ERROR = -1,
    FASTGIT_ENOMEM = -2,
    FASTGIT_EINVAL = -3,
    FASTGIT_ENOENT = -4,
    FASTGIT_EEXIST = -5,
    FASTGIT_EIO = -6,
    FASTGIT_EBUSY = -7,
    FASTGIT_EAGAIN = -8,
    FASTGIT_EOVERFLOW = -9,
    FASTGIT_EUNSUPPORTED = -10,
} fastgit_error_t;

typedef enum {
    FASTGIT_OBJ_BLOB = 1,
    FASTGIT_OBJ_TREE = 2,
    FASTGIT_OBJ_COMMIT = 3,
    FASTGIT_OBJ_TAG = 4,
    FASTGIT_OBJ_OFS_DELTA = 6,
    FASTGIT_OBJ_REF_DELTA = 7,
} fastgit_obj_type_t;

typedef struct fastgit_oid {
    uint8_t hash[FASTGIT_OID_MAX_LEN];
    size_t len;
    uint8_t algo;
} fastgit_oid_t;

typedef struct fastgit_repository fastgit_repository_t;
typedef struct fastgit_object fastgit_object_t;
typedef struct fastgit_odb fastgit_odb_t;
typedef struct fastgit_index fastgit_index_t;
typedef struct fastgit_worktree fastgit_worktree_t;
typedef struct fastgit_merge_options fastgit_merge_options_t;
typedef struct fastgit_rebase_options fastgit_rebase_options_t;
typedef struct fastgit_commit_t fastgit_commit_t;
typedef struct fastgit_tag_t fastgit_tag_t;
typedef struct fastgit_signature fastgit_signature_t;
struct fastgit_status_entry;
typedef struct fastgit_status_entry fastgit_status_entry_t;

const char* fastgit_version(void);
const char* fastgit_error_string(fastgit_error_t err);

fastgit_error_t fastgit_repository_init(const char* path, bool bare, fastgit_repository_t** out);
fastgit_error_t fastgit_repository_open(const char* path, fastgit_repository_t** out);
fastgit_error_t fastgit_repository_free(fastgit_repository_t* repo);

fastgit_odb_t* fastgit_repository_odb(fastgit_repository_t* repo);
fastgit_index_t* fastgit_repository_index(fastgit_repository_t* repo);
fastgit_worktree_t* fastgit_repository_worktree(fastgit_repository_t* repo);
const char* fastgit_repository_path(fastgit_repository_t* repo);
const char* fastgit_repository_gitdir(fastgit_repository_t* repo);

fastgit_error_t fastgit_object_lookup(fastgit_repository_t* repo, const fastgit_oid_t* oid, fastgit_object_t** out);
fastgit_error_t fastgit_object_free(fastgit_object_t* obj);
fastgit_obj_type_t fastgit_object_type(fastgit_object_t* obj);
const fastgit_oid_t* fastgit_object_id(fastgit_object_t* obj);
const void* fastgit_object_data(fastgit_object_t* obj);
size_t fastgit_object_size(fastgit_object_t* obj);

fastgit_error_t fastgit_blob_create_from_buffer(const void* data, size_t len, fastgit_object_t** out);
fastgit_error_t fastgit_blob_create_from_file(const char* path, fastgit_object_t** out);

fastgit_error_t fastgit_commit_create(const fastgit_commit_t* commit, fastgit_object_t** out);

fastgit_error_t fastgit_tree_create(fastgit_object_t** out);

fastgit_error_t fastgit_index_add(fastgit_index_t* index, const char* path);
fastgit_error_t fastgit_index_remove(fastgit_index_t* index, const char* path, uint32_t stage);
fastgit_error_t fastgit_index_write(fastgit_index_t* index);

fastgit_error_t fastgit_checkout_head(fastgit_worktree_t* wt, bool force);
fastgit_error_t fastgit_status(fastgit_worktree_t* wt, fastgit_status_entry_t** entries, size_t* count);
fastgit_error_t fastgit_diff_worktree(fastgit_worktree_t* wt, const char* path, char** out);

fastgit_error_t fastgit_merge(fastgit_repository_t* repo, const fastgit_oid_t* their_head, const fastgit_merge_options_t* opts);
fastgit_error_t fastgit_rebase(fastgit_repository_t* repo, const fastgit_oid_t* upstream, const fastgit_rebase_options_t* opts);

fastgit_error_t fastgit_reference_lookup(fastgit_repository_t* repo, const char* name, fastgit_oid_t* out);
fastgit_error_t fastgit_reference_create(fastgit_repository_t* repo, const char* name, const fastgit_oid_t* oid, bool force, const char* log_message);
fastgit_error_t fastgit_reference_update(fastgit_repository_t* repo, const char* name, const fastgit_oid_t* oid, const char* log_message);
fastgit_error_t fastgit_reference_remove(fastgit_repository_t* repo, const char* name);
fastgit_error_t fastgit_reference_list(fastgit_repository_t* repo, const char* pattern, char*** out, size_t* count);
void fastgit_reference_list_free(char** list, size_t count);
fastgit_error_t fastgit_rev_parse(fastgit_repository_t* repo, const char* spec, fastgit_oid_t* out);
fastgit_error_t fastgit_rev_parse_single(fastgit_repository_t* repo, const char* spec, fastgit_oid_t* out);

fastgit_error_t fastgit_clone(const char* url, const char* path, const char* ref);
fastgit_error_t fastgit_fetch(fastgit_repository_t* repo, const char* remote, const char* refspec);
fastgit_error_t fastgit_push(fastgit_repository_t* repo, const char* remote, const char* refspec);

typedef struct {
    uint64_t objects_read;
    uint64_t objects_written;
    uint64_t bytes_read;
    uint64_t bytes_written;
    double cpu_time_ms;
    double wall_time_ms;
} fastgit_stats_t;

void fastgit_stats_reset(void);
fastgit_stats_t fastgit_stats_get(void);

#ifdef __cplusplus
}
#endif

#endif
