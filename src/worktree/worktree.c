#include "fastgit/worktree.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include "fastgit/odb.h"
#include "fastgit/hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IXUSR 0
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

struct fastgit_worktree {
    char* path;
    char* gitdir;
    fastgit_repository_t* repo;
    fastgit_index_t* index;
    fastgit_odb_t* odb;
};

fastgit_error_t fastgit_worktree_new(const char* path, fastgit_worktree_t** out) {
    if (!path || !out) return FASTGIT_EINVAL;

    fastgit_worktree_t* wt = calloc(1, sizeof(fastgit_worktree_t));
    if (!wt) return FASTGIT_ENOMEM;

    wt->path = strdup(path);
    if (!wt->path) {
        free(wt);
        return FASTGIT_ENOMEM;
    }

    size_t gitdir_len = strlen(path) + 6;
    wt->gitdir = malloc(gitdir_len);
    if (!wt->gitdir) {
        free(wt->path);
        free(wt);
        return FASTGIT_ENOMEM;
    }
    snprintf(wt->gitdir, gitdir_len, "%s/.git", path);
    /* Ensure worktree has an index so status/diff work without a repo. */
    fastgit_index_new(&wt->index);
    if (wt->index) {
        free(wt->index->path);
        char idx_path[4096];
        snprintf(idx_path, sizeof(idx_path), "%s/index", wt->gitdir);
        wt->index->path = strdup(idx_path);
    }

    *out = wt;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_worktree_open(const char* path, fastgit_worktree_t** out) {
    if (!path || !out) return FASTGIT_EINVAL;

    fastgit_worktree_t* wt = calloc(1, sizeof(fastgit_worktree_t));
    if (!wt) return FASTGIT_ENOMEM;

    wt->path = strdup(path);
    if (!wt->path) {
        free(wt);
        return FASTGIT_ENOMEM;
    }

    size_t gitdir_len = strlen(path) + 6;
    wt->gitdir = malloc(gitdir_len);
    if (!wt->gitdir) {
        free(wt->path);
        free(wt);
        return FASTGIT_ENOMEM;
    }
    snprintf(wt->gitdir, gitdir_len, "%s/.git", path);

    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/index", wt->gitdir);
    fastgit_error_t err = fastgit_index_open(index_path, &wt->index);
    if (err != FASTGIT_OK) {
        fastgit_worktree_free(wt);
        return err;
    }

    char odb_path[1024];
    snprintf(odb_path, sizeof(odb_path), "%s/objects", wt->gitdir);
    err = fastgit_odb_open(odb_path, &wt->odb);
    if (err != FASTGIT_OK) {
        fastgit_worktree_free(wt);
        return err;
    }

    *out = wt;
    return FASTGIT_OK;
}

void fastgit_worktree_free(fastgit_worktree_t* wt) {
    if (!wt) return;
    if (wt->index) fastgit_index_free(wt->index);
    if (wt->odb) fastgit_odb_free(wt->odb);
    free(wt->path);
    free(wt->gitdir);
    free(wt);
}

const char* fastgit_worktree_path(fastgit_worktree_t* wt) {
    return wt ? wt->path : NULL;
}

const char* fastgit_worktree_gitdir(fastgit_worktree_t* wt) {
    return wt ? wt->gitdir : NULL;
}

static fastgit_error_t checkout_entry(fastgit_worktree_t* wt, const fastgit_index_entry_t* entry, bool force __attribute__((unused))) {
    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%s/%s", wt->path, entry->path);

    fastgit_odb_object_t obj;
    fastgit_error_t err = fastgit_odb_read(wt->odb, &entry->oid, &obj);
    if (err != FASTGIT_OK) return err;

    char* dir = strdup(full_path);
    char* last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
#if defined(_WIN32)
        CreateDirectoryA(dir, NULL);
#else
        mkdir(dir, 0755);
#endif
    }
    free(dir);

    int fd;
#if defined(_WIN32)
    fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR);
#else
    fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, entry->mode & 0777);
#endif
    if (fd < 0) {
        free(obj.data);
        return FASTGIT_EIO;
    }

    ssize_t written = write(fd, obj.data, obj.size);
    close(fd);
    free(obj.data);

    if (written != (ssize_t)obj.size) return FASTGIT_EIO;

    return FASTGIT_OK;
}

fastgit_error_t fastgit_checkout_head(fastgit_worktree_t* wt, bool force) {
    if (!wt || !wt->index) return FASTGIT_EINVAL;

    for (size_t i = 0; i < fastgit_index_entry_count(wt->index); i++) {
        const fastgit_index_entry_t* entry = fastgit_index_entry_by_index(wt->index, i);
        if (entry->stage != FASTGIT_INDEX_STAGE_NORMAL) continue;

        fastgit_error_t err = checkout_entry(wt, entry, force);
        if (err != FASTGIT_OK && !force) return err;
    }

    return FASTGIT_OK;
}

fastgit_error_t fastgit_checkout_tree(fastgit_worktree_t* wt __attribute__((unused)), const fastgit_oid_t* tree_oid __attribute__((unused)), bool force __attribute__((unused))) {
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_checkout_index(fastgit_worktree_t* wt, fastgit_index_t* index, bool force __attribute__((unused))) {
    if (!wt || !index) return FASTGIT_EINVAL;

    for (size_t i = 0; i < fastgit_index_entry_count(index); i++) {
        const fastgit_index_entry_t* entry = fastgit_index_entry_by_index(index, i);
        if (entry->stage != FASTGIT_INDEX_STAGE_NORMAL) continue;

        fastgit_error_t err = checkout_entry(wt, entry, force);
        if (err != FASTGIT_OK && !force) return err;
    }

    return FASTGIT_OK;
}

static fastgit_error_t status_compare_entry(fastgit_worktree_t* wt, const fastgit_index_entry_t* entry, fastgit_status_entry_t** out) {
    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%s/%s", wt->path, entry->path);

    struct stat st;
    if (stat(full_path, &st) < 0) {
        *out = calloc(1, sizeof(fastgit_status_entry_t));
        if (!*out) return FASTGIT_ENOMEM;
        (*out)->path = strdup(entry->path);
        (*out)->index_status = FASTGIT_STATUS_INDEX_MODIFIED;
        (*out)->worktree_status = FASTGIT_STATUS_WT_DELETED;
        (*out)->index_oid = entry->oid;
        (*out)->index_mode = entry->mode;
        return FASTGIT_OK;
    }

    int fd = open(full_path, O_RDONLY);
    if (fd < 0) {
        *out = calloc(1, sizeof(fastgit_status_entry_t));
        if (!*out) return FASTGIT_ENOMEM;
        (*out)->path = strdup(entry->path);
        (*out)->index_status = FASTGIT_STATUS_INDEX_MODIFIED;
        (*out)->worktree_status = FASTGIT_STATUS_WT_DELETED;
        (*out)->index_oid = entry->oid;
        (*out)->index_mode = entry->mode;
        return FASTGIT_OK;
    }

    struct stat fst;
    fstat(fd, &fst);
    size_t file_size = fst.st_size;

    void* data = malloc(file_size);
    if (!data) {
        close(fd);
        return FASTGIT_ENOMEM;
    }

    ssize_t n = read(fd, data, file_size);
    close(fd);

    if (n != (ssize_t)file_size) {
        free(data);
        return FASTGIT_EIO;
    }

    fastgit_hash_t h;
    fastgit_error_t err = fastgit_hash(FASTGIT_HASH_SHA256, data, file_size, &h);
    free(data);
    if (err != FASTGIT_OK) return err;
    fastgit_oid_t wt_oid;
    memset(&wt_oid, 0, sizeof(wt_oid));
    memcpy(wt_oid.hash, h.digest, h.len);
    wt_oid.len = h.len;
    wt_oid.algo = h.algo;

    bool same = fastgit_oid_equal(&entry->oid, &wt_oid);

    *out = calloc(1, sizeof(fastgit_status_entry_t));
    if (!*out) return FASTGIT_ENOMEM;
    (*out)->path = strdup(entry->path);
    (*out)->index_oid = entry->oid;
    (*out)->worktree_oid = wt_oid;
    (*out)->index_mode = entry->mode;
    (*out)->worktree_mode = st.st_mode & 0777;

    if (!same) {
        (*out)->index_status = FASTGIT_STATUS_INDEX_MODIFIED;
        (*out)->worktree_status = FASTGIT_STATUS_WT_MODIFIED;
    } else {
        (*out)->index_status = FASTGIT_STATUS_CURRENT;
        (*out)->worktree_status = FASTGIT_STATUS_CURRENT;
    }

    return FASTGIT_OK;
}

fastgit_error_t fastgit_status(fastgit_worktree_t* wt, fastgit_status_entry_t** entries, size_t* count) {
    if (!wt || !entries || !count) return FASTGIT_EINVAL;
    if (!wt->index) return FASTGIT_EINVAL;

    size_t capacity = fastgit_index_entry_count(wt->index) + 1024;
    fastgit_status_entry_t* status_entries = calloc(capacity, sizeof(fastgit_status_entry_t));
    if (!status_entries) return FASTGIT_ENOMEM;

    size_t status_count = 0;

    for (size_t i = 0; i < fastgit_index_entry_count(wt->index); i++) {
        const fastgit_index_entry_t* entry = fastgit_index_entry_by_index(wt->index, i);
        if (entry->stage != FASTGIT_INDEX_STAGE_NORMAL) continue;

        fastgit_status_entry_t* status_entry;
        fastgit_error_t err = status_compare_entry(wt, entry, &status_entry);
        if (err != FASTGIT_OK) {
            for (size_t j = 0; j < status_count; j++) {
                free(status_entries[j].path);
            }
            free(status_entries);
            return err;
        }

        if (status_count >= capacity) {
            capacity *= 2;
            fastgit_status_entry_t* new_entries = realloc(status_entries, capacity * sizeof(fastgit_status_entry_t));
            if (!new_entries) {
                for (size_t j = 0; j < status_count; j++) {
                    free(status_entries[j].path);
                }
                free(status_entries);
                free(status_entry->path);
                free(status_entry);
                return FASTGIT_ENOMEM;
            }
            status_entries = new_entries;
        }

        status_entries[status_count++] = *status_entry;
        free(status_entry);
    }

    DIR* dir = opendir(wt->path);
    if (dir) {
        struct dirent* de;
        while ((de = readdir(dir)) != NULL) {
            if (de->d_name[0] == '.') continue;

            char full_path[4096];
            snprintf(full_path, sizeof(full_path), "%s/%s", wt->path, de->d_name);

            struct stat st;
            if (stat(full_path, &st) < 0) continue;
            if (!S_ISREG(st.st_mode)) continue;

            bool in_index = false;
            for (size_t i = 0; i < fastgit_index_entry_count(wt->index); i++) {
                const fastgit_index_entry_t* entry = fastgit_index_entry_by_index(wt->index, i);
                if (strcmp(entry->path, de->d_name) == 0) {
                    in_index = true;
                    break;
                }
            }

            if (!in_index) {
                if (status_count >= capacity) {
                    capacity *= 2;
                    fastgit_status_entry_t* new_entries = realloc(status_entries, capacity * sizeof(fastgit_status_entry_t));
                    if (!new_entries) {
                        for (size_t j = 0; j < status_count; j++) {
                            free(status_entries[j].path);
                        }
                        free(status_entries);
                        return FASTGIT_ENOMEM;
                    }
                    status_entries = new_entries;
                }

                fastgit_status_entry_t* se = &status_entries[status_count++];
                se->path = strdup(de->d_name);
                se->index_status = FASTGIT_STATUS_CURRENT;
                se->worktree_status = FASTGIT_STATUS_WT_NEW;
            }
        }
        closedir(dir);
    }

    *entries = status_entries;
    *count = status_count;
    return FASTGIT_OK;
}

void fastgit_status_free(fastgit_status_entry_t* entries, size_t count) {
    if (!entries) return;
    for (size_t i = 0; i < count; i++) {
        free(entries[i].path);
    }
    free(entries);
}

static void diff_append(char** buf, size_t* len, size_t* cap, const char* str, size_t str_len) {
    if (*len + str_len + 1 >= *cap) {
        *cap = (*cap == 0) ? 4096 : *cap * 2;
        while (*len + str_len + 1 >= *cap) *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, str, str_len);
    *len += str_len;
    (*buf)[*len] = '\0';
}

fastgit_error_t fastgit_diff_worktree(fastgit_worktree_t* wt, const char* path, char** out) {
    if (!wt || !out) return FASTGIT_EINVAL;

    fastgit_status_entry_t* status_entries;
    size_t status_count;
    fastgit_error_t err = fastgit_status(wt, &status_entries, &status_count);
    if (err != FASTGIT_OK) return err;

    char* diff = NULL;
    size_t diff_len = 0;
    size_t diff_cap = 0;

    for (size_t i = 0; i < status_count; i++) {
        fastgit_status_entry_t* se = &status_entries[i];
        if (path && strcmp(se->path, path) != 0) continue;

        if (se->index_status != FASTGIT_STATUS_CURRENT || se->worktree_status != FASTGIT_STATUS_CURRENT) {
            char header[512];
            int hlen = snprintf(header, sizeof(header), "diff --git a/%s b/%s\n", se->path, se->path);
            if (hlen > 0) diff_append(&diff, &diff_len, &diff_cap, header, hlen);

            hlen = snprintf(header, sizeof(header), "index %06o..%06o\n", se->index_mode & 0777, se->worktree_mode & 0777);
            if (hlen > 0) diff_append(&diff, &diff_len, &diff_cap, header, hlen);

            hlen = snprintf(header, sizeof(header), "--- a/%s\n+++ b/%s\n", se->path, se->path);
            if (hlen > 0) diff_append(&diff, &diff_len, &diff_cap, header, hlen);
        }
    }

    fastgit_status_free(status_entries, status_count);

    *out = diff;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_diff_index(fastgit_worktree_t* wt, fastgit_index_t* index, const char* path, char** out) {
    (void)wt; (void)index; (void)path; (void)out;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_diff_trees(fastgit_worktree_t* wt, const fastgit_oid_t* old_tree, const fastgit_oid_t* new_tree, char** out) {
    (void)wt; (void)old_tree; (void)new_tree; (void)out;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_status_ext(fastgit_worktree_t* wt, const fastgit_status_options_t* opts, fastgit_status_entry_t** entries, size_t* count) {
    (void)opts;
    return fastgit_status(wt, entries, count);
}

fastgit_error_t fastgit_diff_worktree_ext(fastgit_worktree_t* wt, const char* path, const fastgit_diff_options_t* opts, char** out) {
    (void)opts;
    return fastgit_diff_worktree(wt, path, out);
}
