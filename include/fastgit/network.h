#ifndef FASTGIT_NETWORK_H
#define FASTGIT_NETWORK_H

#include "fastgit/object.h"
#include "fastgit/hash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fastgit_remote fastgit_remote_t;
typedef struct fastgit_transport fastgit_transport_t;
typedef struct fastgit_cred fastgit_cred_t;

typedef enum {
    FASTGIT_CRED_USERPASS_PLAINTEXT = 1,
    FASTGIT_CRED_SSH_KEY = 2,
    FASTGIT_CRED_SSH_MEMORY = 4,
    FASTGIT_CRED_DEFAULT = 8,
    FASTGIT_CRED_SSH_INTERACTIVE = 16,
    FASTGIT_CRED_USERNAME = 32,
    FASTGIT_CRED_SSH_AGENT = 64,
    FASTGIT_CRED_TOKEN = 128,
    FASTGIT_CRED_PQC = 256,
} fastgit_cred_type_t;

typedef struct {
    fastgit_cred_type_t type;
    char* username;
    char* password;
    char* public_key;
    char* private_key;
    char* passphrase;
    char* token;
    void* pqc_data;
    size_t pqc_data_len;
} fastgit_cred_data_t;

typedef fastgit_error_t (*fastgit_cred_acquire_cb)(fastgit_cred_data_t** cred, const char* url, const char* username, unsigned int allowed_types, void* payload);
typedef void (*fastgit_cred_release_cb)(fastgit_cred_data_t* cred, void* payload);

typedef struct {
    fastgit_cred_acquire_cb acquire;
    fastgit_cred_release_cb release;
    void* payload;
} fastgit_cred_callbacks_t;

typedef enum {
    FASTGIT_TRANSPORT_HTTP = 1,
    FASTGIT_TRANSPORT_SSH = 2,
    FASTGIT_TRANSPORT_LOCAL = 3,
} fastgit_transport_type_t;

struct fastgit_transport {
    fastgit_transport_type_t type;
    void *handle;
};

fastgit_error_t fastgit_remote_create(fastgit_repository_t* repo, const char* name, const char* url, fastgit_remote_t** out);
fastgit_error_t fastgit_remote_lookup(fastgit_repository_t* repo, const char* name, fastgit_remote_t** out);
void fastgit_remote_free(fastgit_remote_t* remote);

const char* fastgit_remote_name(fastgit_remote_t* remote);
const char* fastgit_remote_url(fastgit_remote_t* remote);

fastgit_error_t fastgit_remote_set_cred_callbacks(fastgit_remote_t* remote, const fastgit_cred_callbacks_t* callbacks);

fastgit_error_t fastgit_fetch(fastgit_repository_t* repo, const char* remote, const char* refspec);
fastgit_error_t fastgit_push(fastgit_repository_t* repo, const char* remote, const char* refspec);

typedef struct {
    char* name;
    fastgit_oid_t oid;
    char* target;
} fastgit_ref_t;

fastgit_error_t fastgit_remote_ls(fastgit_remote_t* remote, fastgit_ref_t*** refs, size_t* count);
void fastgit_remote_ls_free(fastgit_ref_t** refs, size_t count);

typedef struct {
    uint64_t total_objects;
    uint64_t indexed_objects;
    uint64_t received_objects;
    uint64_t received_bytes;
    uint64_t local_objects;
    uint64_t local_bytes;
    uint64_t delta_objects;
    double progress;
} fastgit_transfer_progress_t;

typedef void (*fastgit_transfer_progress_cb)(const fastgit_transfer_progress_t* stats, void* payload);

fastgit_error_t fastgit_remote_set_transfer_progress(fastgit_remote_t* remote, fastgit_transfer_progress_cb cb, void* payload);

typedef enum {
    FASTGIT_PQC_ALGO_ML_DSA_44 = 1,
    FASTGIT_PQC_ALGO_ML_DSA_65 = 2,
    FASTGIT_PQC_ALGO_ML_DSA_87 = 3,
    FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128F = 4,
    FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128S = 5,
    FASTGIT_PQC_ALGO_SLH_DSA_SHA2_192F = 6,
    FASTGIT_PQC_ALGO_SLH_DSA_SHA2_256F = 7,
    FASTGIT_PQC_ALGO_FALCON_512 = 8,
    FASTGIT_PQC_ALGO_FALCON_1024 = 9,
} fastgit_pqc_algo_t;

typedef struct {
    fastgit_pqc_algo_t algo;
    uint8_t* public_key;
    size_t public_key_len;
    uint8_t* private_key;
    size_t private_key_len;
} fastgit_pqc_key_t;

fastgit_error_t fastgit_pqc_key_generate(fastgit_pqc_algo_t algo, fastgit_pqc_key_t** out);
void fastgit_pqc_key_free(fastgit_pqc_key_t* key);

fastgit_error_t fastgit_pqc_sign(const fastgit_pqc_key_t* key, const void* data, size_t len, uint8_t** sig, size_t* sig_len);
fastgit_error_t fastgit_pqc_verify(const fastgit_pqc_key_t* key, const void* data, size_t len, const uint8_t* sig, size_t sig_len);

typedef struct {
    fastgit_pqc_algo_t primary;
    fastgit_pqc_algo_t secondary;
    bool hybrid;
} fastgit_pqc_config_t;

fastgit_error_t fastgit_remote_set_pqc_config(fastgit_remote_t* remote, const fastgit_pqc_config_t* config);

#ifdef __cplusplus
}
#endif

#endif
