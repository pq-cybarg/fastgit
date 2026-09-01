#include "fastgit/fastgit.h"
#include "fastgit/object.h"
#include "fastgit/odb.h"
#include "fastgit/index.h"
#include "fastgit/worktree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

struct fastgit_repository {
    char* path;
    char* gitdir;
    bool bare;
    fastgit_odb_t* odb;
    fastgit_index_t* index;
    fastgit_worktree_t* worktree;
};

static fastgit_stats_t g_stats = {0};

const char* fastgit_version(void) {
    return "0.1.0";
}

const char* fastgit_error_string(fastgit_error_t err) {
    switch (err) {
        case FASTGIT_OK: return "ok";
        case FASTGIT_ERROR: return "generic error";
        case FASTGIT_ENOMEM: return "out of memory";
        case FASTGIT_EINVAL: return "invalid argument";
        case FASTGIT_ENOENT: return "not found";
        case FASTGIT_EEXIST: return "already exists";
        case FASTGIT_EIO: return "I/O error";
        case FASTGIT_EBUSY: return "busy";
        case FASTGIT_EAGAIN: return "try again";
        case FASTGIT_EOVERFLOW: return "overflow";
        case FASTGIT_EUNSUPPORTED: return "unsupported";
        default: return "unknown";
    }
}

void fastgit_stats_reset(void) { memset(&g_stats, 0, sizeof(g_stats)); }
fastgit_stats_t fastgit_stats_get(void) { return g_stats; }

static void ensure_dir(const char* p) {
#if defined(_WIN32)
    mkdir(p);
#else
    mkdir(p, 0755);
#endif
}

fastgit_error_t fastgit_repository_init(const char* path, bool bare, fastgit_repository_t** out) {
    if (!path || !out) return FASTGIT_EINVAL;
    fastgit_repository_t* repo = calloc(1, sizeof(*repo));
    if (!repo) return FASTGIT_ENOMEM;
    repo->path = strdup(path);
    repo->bare = bare;
    if (!repo->path) { free(repo); return FASTGIT_ENOMEM; }
    size_t gl = strlen(path) + 8;
    repo->gitdir = malloc(gl);
    if (!repo->gitdir) { free(repo->path); free(repo); return FASTGIT_ENOMEM; }
    if (bare) snprintf(repo->gitdir, gl, "%s", path);
    else snprintf(repo->gitdir, gl, "%s/.git", path);

    ensure_dir(repo->gitdir);
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s/objects", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/refs", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/refs/heads", repo->gitdir); ensure_dir(tmp);

    char odb_path[4096];
    snprintf(odb_path, sizeof(odb_path), "%s/objects", repo->gitdir);
    fastgit_error_t err = fastgit_odb_new(odb_path, &repo->odb);
    if (err != FASTGIT_OK) { free(repo->path); free(repo->gitdir); free(repo); return err; }
    char idx_path[4096];
    snprintf(idx_path, sizeof(idx_path), "%s/index", repo->gitdir);
    if (fastgit_index_open(idx_path, &repo->index) != FASTGIT_OK) {
        if (fastgit_index_new(&repo->index) == FASTGIT_OK) {
            free(repo->index->path);
            repo->index->path = strdup(idx_path);
        } else {
            repo->index = NULL;
        }
    }
    // worktree only for non-bare – share the same index object so bench_full Add→Diff is coherent
    if (!bare) {
        fastgit_worktree_new(path, &repo->worktree);
        if (repo->worktree && repo->index) {
            fastgit_worktree_attach_index(repo->worktree, repo->index);
        }
    }
    *out = repo;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_repository_open(const char* path, fastgit_repository_t** out) {
    if (!path || !out) return FASTGIT_EINVAL;
    fastgit_repository_t* repo = calloc(1, sizeof(*repo));
    if (!repo) return FASTGIT_ENOMEM;
    repo->path = strdup(path);
    if (!repo->path) { free(repo); return FASTGIT_ENOMEM; }
    size_t gl = strlen(path) + 8;
    repo->gitdir = malloc(gl);
    if (!repo->gitdir) { free(repo->path); free(repo); return FASTGIT_ENOMEM; }
    snprintf(repo->gitdir, gl, "%s/.git", path);
    struct stat st;
    if (stat(repo->gitdir, &st) != 0) {
        // try bare
        free(repo->gitdir);
        repo->gitdir = strdup(path);
        repo->bare = true;
    }
    char odb_path[4096];
    snprintf(odb_path, sizeof(odb_path), "%s/objects", repo->gitdir);
    fastgit_error_t err = fastgit_odb_open(odb_path, &repo->odb);
    if (err != FASTGIT_OK) { free(repo->path); free(repo->gitdir); free(repo); return err; }
    char idx_path[4096];
    snprintf(idx_path, sizeof(idx_path), "%s/index", repo->gitdir);
    if (fastgit_index_open(idx_path, &repo->index) != FASTGIT_OK) {
        if (fastgit_index_new(&repo->index) == FASTGIT_OK) {
            free(repo->index->path);
            repo->index->path = strdup(idx_path);
        }
    }
    if (!repo->bare) {
        fastgit_worktree_open(path, &repo->worktree);
        if (repo->worktree && repo->index) {
            fastgit_worktree_attach_index(repo->worktree, repo->index);
        }
    }
    *out = repo;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_repository_free(fastgit_repository_t* repo) {
    if (!repo) return FASTGIT_OK;
    if (repo->worktree) fastgit_worktree_free(repo->worktree);
    if (repo->index) fastgit_index_free(repo->index);
    if (repo->odb) fastgit_odb_free(repo->odb);
    free(repo->path);
    free(repo->gitdir);
    free(repo);
    return FASTGIT_OK;
}

fastgit_odb_t* fastgit_repository_odb(fastgit_repository_t* repo) { return repo ? repo->odb : NULL; }
fastgit_index_t* fastgit_repository_index(fastgit_repository_t* repo) { return repo ? repo->index : NULL; }
fastgit_worktree_t* fastgit_repository_worktree(fastgit_repository_t* repo) { return repo ? repo->worktree : NULL; }

// object shims
fastgit_error_t fastgit_object_free(fastgit_object_t* obj) {
    if (!obj) return FASTGIT_OK;
    if (obj->free_data && obj->data) obj->free_data(obj->data);
    else free(obj->data);
    free(obj);
    return FASTGIT_OK;
}
fastgit_obj_type_t fastgit_object_type(fastgit_object_t* obj) { return obj ? obj->type : 0; }
const fastgit_oid_t* fastgit_object_id(fastgit_object_t* obj) { return obj ? &obj->oid : NULL; }
const void* fastgit_object_data(fastgit_object_t* obj) { return obj ? obj->data : NULL; }
size_t fastgit_object_size(fastgit_object_t* obj) { return obj ? obj->size : 0; }

fastgit_error_t fastgit_object_lookup(fastgit_repository_t* repo, const fastgit_oid_t* oid, fastgit_object_t** out) {
    if (!repo || !oid || !out) return FASTGIT_EINVAL;
    fastgit_odb_object_t o;
    fastgit_error_t err = fastgit_odb_read(repo->odb, oid, &o);
    if (err != FASTGIT_OK) return err;
    fastgit_object_t* obj = calloc(1, sizeof(*obj));
    if (!obj) { free(o.data); return FASTGIT_ENOMEM; }
    obj->type = o.type; obj->size = o.size; obj->data = o.data; obj->free_data = free; obj->oid = *oid;
    *out = obj; return FASTGIT_OK;
}

fastgit_error_t fastgit_blob_create_from_buffer(const void* data, size_t len, fastgit_object_t** out) {
    return fastgit_object_parse(FASTGIT_OBJ_BLOB, data, len, out);
}



fastgit_error_t fastgit_clone(const char* url, const char* path, const char* ref) {
    (void)url;(void)path;(void)ref; return FASTGIT_EUNSUPPORTED;
}
