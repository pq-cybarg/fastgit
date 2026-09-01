#include "fastgit/network.h"
#include <stdlib.h>
#include <string.h>

struct fastgit_remote {
    fastgit_repository_t* repo;
    char* name;
    char* url;
    fastgit_cred_callbacks_t cred_cb;
    fastgit_transfer_progress_cb progress_cb;
    void* progress_payload;
    fastgit_pqc_config_t pqc_config;
};



fastgit_error_t fastgit_remote_create(fastgit_repository_t* repo, const char* name, const char* url, fastgit_remote_t** out) {
    if (!repo || !name || !url || !out) return FASTGIT_EINVAL;

    fastgit_remote_t* remote = calloc(1, sizeof(fastgit_remote_t));
    if (!remote) return FASTGIT_ENOMEM;

    remote->repo = repo;
    remote->name = strdup(name);
    remote->url = strdup(url);
    if (!remote->name || !remote->url) {
        fastgit_remote_free(remote);
        return FASTGIT_ENOMEM;
    }

    remote->pqc_config.hybrid = true;
    remote->pqc_config.primary = FASTGIT_PQC_ALGO_ML_DSA_65;
    remote->pqc_config.secondary = FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128F;

    *out = remote;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_remote_lookup(fastgit_repository_t* repo, const char* name, fastgit_remote_t** out) {
    (void)repo; (void)name; (void)out;
    return FASTGIT_ENOENT;
}

void fastgit_remote_free(fastgit_remote_t* remote) {
    if (!remote) return;
    free(remote->name);
    free(remote->url);
    free(remote);
}

const char* fastgit_remote_name(fastgit_remote_t* remote) {
    return remote ? remote->name : NULL;
}

const char* fastgit_remote_url(fastgit_remote_t* remote) {
    return remote ? remote->url : NULL;
}

fastgit_error_t fastgit_remote_set_cred_callbacks(fastgit_remote_t* remote, const fastgit_cred_callbacks_t* callbacks) {
    if (!remote || !callbacks) return FASTGIT_EINVAL;
    remote->cred_cb = *callbacks;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_fetch(fastgit_repository_t* repo, const char* remote, const char* refspec) {
    (void)repo; (void)remote; (void)refspec;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_push(fastgit_repository_t* repo, const char* remote, const char* refspec) {
    (void)repo; (void)remote; (void)refspec;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_remote_ls(fastgit_remote_t* remote, fastgit_ref_t*** refs, size_t* count) {
    (void)remote; (void)refs; (void)count;
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_remote_ls_free(fastgit_ref_t** refs, size_t count) {
    if (!refs) return;
    for (size_t i = 0; i < count; i++) {
        free(refs[i]->name);
        free(refs[i]->target);
        free(refs[i]);
    }
    free(refs);
}

fastgit_error_t fastgit_remote_set_transfer_progress(fastgit_remote_t* remote, fastgit_transfer_progress_cb cb, void* payload) {
    if (!remote) return FASTGIT_EINVAL;
    remote->progress_cb = cb;
    remote->progress_payload = payload;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_remote_set_pqc_config(fastgit_remote_t* remote, const fastgit_pqc_config_t* config) {
    if (!remote || !config) return FASTGIT_EINVAL;
    remote->pqc_config = *config;
    return FASTGIT_OK;
}
