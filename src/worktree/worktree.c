#include "fastgit/worktree.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include "fastgit/odb.h"
#include "fastgit/hash.h"
#include "fastgit/platform.h"
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
    bool index_owned;
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
        wt->index_owned = true;
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
    wt->index_owned = true;

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
    if (wt->index && wt->index_owned) fastgit_index_free(wt->index);
    if (wt->odb) fastgit_odb_free(wt->odb);
    free(wt->path);
    free(wt->gitdir);
    free(wt);
}

fastgit_error_t fastgit_worktree_attach_index(fastgit_worktree_t* wt, fastgit_index_t* index) {
    if (!wt || !index) return FASTGIT_EINVAL;
    if (wt->index && wt->index_owned) fastgit_index_free(wt->index);
    wt->index = index;
    wt->index_owned = false;
    return FASTGIT_OK;
}

const char* fastgit_worktree_path(fastgit_worktree_t* wt) {
    return wt ? wt->path : NULL;
}

const char* fastgit_worktree_gitdir(fastgit_worktree_t* wt) {
    return wt ? wt->gitdir : NULL;
}

static void mkdir_p(const char* path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
#if defined(_WIN32)
            CreateDirectoryA(tmp, NULL);
#else
            mkdir(tmp, 0755);
#endif
            tmp[i] = '/';
        }
    }
#if defined(_WIN32)
    CreateDirectoryA(tmp, NULL);
#else
    mkdir(tmp, 0755);
#endif
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
        mkdir_p(dir);
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

static fastgit_error_t checkout_tree_recursive(fastgit_worktree_t* wt, const fastgit_oid_t* tree_oid, const char* prefix, bool force) {
    fastgit_odb_object_t obj;
    fastgit_error_t err = fastgit_odb_read(wt->odb, tree_oid, &obj);
    if (err != FASTGIT_OK) return err;
    if (obj.type != FASTGIT_OBJ_TREE) { free(obj.data); return FASTGIT_EINVAL; }
    const uint8_t* p = (const uint8_t*)obj.data;
    const uint8_t* end = p + obj.size;
    while (p < end) {
        const char* sp = memchr(p, ' ', (size_t)(end - p));
        if (!sp) { free(obj.data); return FASTGIT_EINVAL; }
        uint32_t mode = (uint32_t)strtoul((const char*)p, NULL, 8);
        const char* name = sp + 1;
        const char* nul = memchr(name, '\0', (size_t)((const uint8_t*)end - (const uint8_t*)name));
        if (!nul) { free(obj.data); return FASTGIT_EINVAL; }
        size_t namelen = (size_t)(nul - name);
        const uint8_t* oid_raw = (const uint8_t*)(nul + 1);
        if (oid_raw + 32 > end) { free(obj.data); return FASTGIT_EINVAL; }
        fastgit_oid_t eoid; memset(&eoid, 0, sizeof(eoid)); memcpy(eoid.hash, oid_raw, 32); eoid.len = 32; eoid.algo = FASTGIT_HASH_SHA256;
        char full_path[4096];
        if (prefix[0] == '\0') snprintf(full_path, sizeof(full_path), "%.*s", (int)namelen, name);
        else snprintf(full_path, sizeof(full_path), "%s/%.*s", prefix, (int)namelen, name);
        if (S_ISDIR(mode)) {
            err = checkout_tree_recursive(wt, &eoid, full_path, force);
            if (err != FASTGIT_OK && !force) { free(obj.data); return err; }
        } else {
            fastgit_index_entry_t entry = {0};
            entry.path = full_path;
            entry.oid = eoid;
            entry.mode = mode;
            entry.stage = FASTGIT_INDEX_STAGE_NORMAL;
            err = checkout_entry(wt, &entry, force);
            if (err != FASTGIT_OK && !force) { free(obj.data); return err; }
        }
        p = oid_raw + 32;
    }
    free(obj.data);
    return FASTGIT_OK;
}
fastgit_error_t fastgit_checkout_tree(fastgit_worktree_t* wt, const fastgit_oid_t* tree_oid, bool force) {
    if (!wt || !tree_oid) return FASTGIT_EINVAL;
    return checkout_tree_recursive(wt, tree_oid, "", force);
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

    void* data = malloc(file_size ? file_size : 1);
    if (!data) {
        close(fd);
        return FASTGIT_ENOMEM;
    }

    ssize_t n = 0;
    if (file_size > 0) n = read(fd, data, file_size);
    close(fd);

    if (file_size > 0 && n != (ssize_t)file_size) {
        free(data);
        return FASTGIT_EIO;
    }

    char hdr[32];
    int hdr_len = snprintf(hdr, sizeof(hdr), "blob %zu", file_size);
    hdr[hdr_len++] = '\0';
    fastgit_hash_ctx_t* ctx = fastgit_hash_ctx_new(FASTGIT_HASH_SHA256);
    if (!ctx) { free(data); return FASTGIT_ENOMEM; }
    fastgit_hash_ctx_init(ctx);
    fastgit_hash_ctx_update(ctx, hdr, (size_t)hdr_len);
    if (file_size > 0) fastgit_hash_ctx_update(ctx, data, file_size);
    fastgit_hash_t h;
    fastgit_hash_ctx_final(ctx, &h);
    fastgit_hash_ctx_free(ctx);
    free(data);
    fastgit_error_t err = FASTGIT_OK;
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

typedef struct { fastgit_worktree_t* wt; const fastgit_index_entry_t* entry; fastgit_status_entry_t out; fastgit_error_t err; } status_task_t;
static void status_task_fn(void* arg) {
    status_task_t* t = (status_task_t*)arg;
    fastgit_status_entry_t* se = NULL;
    t->err = status_compare_entry(t->wt, t->entry, &se);
    if (t->err == FASTGIT_OK && se) { t->out = *se; free(se); } else { memset(&t->out, 0, sizeof(t->out)); }
}

fastgit_error_t fastgit_status(fastgit_worktree_t* wt, fastgit_status_entry_t** entries, size_t* count) {
    if (!wt || !entries || !count) return FASTGIT_EINVAL;
    if (!wt->index) return FASTGIT_EINVAL;

    size_t n = fastgit_index_entry_count(wt->index);
    size_t capacity = n + 1024;
    fastgit_status_entry_t* status_entries = calloc(capacity, sizeof(fastgit_status_entry_t));
    if (!status_entries) return FASTGIT_ENOMEM;

    size_t status_count = 0;

    if (n >= 128) {
        status_task_t* tasks = calloc(n, sizeof(status_task_t));
        size_t task_n = 0;
        for (size_t i = 0; i < n; i++) {
            const fastgit_index_entry_t* e = fastgit_index_entry_by_index(wt->index, i);
            if (e->stage != FASTGIT_INDEX_STAGE_NORMAL) continue;
            tasks[task_n].wt = wt;
            tasks[task_n].entry = e;
            tasks[task_n].err = FASTGIT_OK;
            task_n++;
        }
        fastgit_thread_pool_t* pool = NULL;
        size_t workers = task_n < 16 ? task_n : 16;
        fastgit_thread_pool_config_t pcfg = {0};
        pcfg.worker_count = (int)workers;
        pcfg.max_queue_depth = (int)task_n + 4;
        if (workers > 0 && fastgit_thread_pool_new(&pcfg, &pool) == FASTGIT_OK) {
            for (size_t i = 0; i < task_n; i++) fastgit_thread_pool_submit(pool, status_task_fn, &tasks[i]);
            fastgit_thread_pool_wait(pool);
            fastgit_thread_pool_free(pool);
            bool ok = true;
            for (size_t i = 0; i < task_n; i++) if (tasks[i].err != FASTGIT_OK) ok = false;
            if (ok) {
                for (size_t i = 0; i < task_n; i++) status_entries[status_count++] = tasks[i].out;
                free(tasks);
                goto untracked;
            }
            for (size_t i = 0; i < task_n; i++) free(tasks[i].out.path);
        }
        free(tasks);
        /* fallback to sequential */
        status_count = 0;
    }

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
untracked:;

    // recursive untracked scan (skip .git, handle subdirs)
    // simple stack-based DFS to avoid nftw dependency
    {
        size_t stack_cap = 32;
        char** stack = malloc(stack_cap * sizeof(char*));
        if (stack) {
            size_t stack_len = 0;
            stack[stack_len++] = strdup("");
            while (stack_len > 0) {
                char* rel = stack[--stack_len];
                char dir_path[4096];
                if (rel[0] == '\0') snprintf(dir_path, sizeof(dir_path), "%s", wt->path);
                else snprintf(dir_path, sizeof(dir_path), "%s/%s", wt->path, rel);
                DIR* d = opendir(dir_path);
                if (!d) { free(rel); continue; }
                struct dirent* de;
                while ((de = readdir(d)) != NULL) {
                    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                    if (rel[0] == '\0' && strcmp(de->d_name, ".git") == 0) continue;
                    char child_rel[4096];
                    if (rel[0] == '\0') snprintf(child_rel, sizeof(child_rel), "%s", de->d_name);
                    else snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name);
                    char child_full[4096];
                    snprintf(child_full, sizeof(child_full), "%s/%s", wt->path, child_rel);
                    struct stat st;
                    if (lstat(child_full, &st) < 0) continue;
                    if (S_ISDIR(st.st_mode)) {
                        if (stack_len >= stack_cap) {
                            stack_cap *= 2;
                            char** ns = realloc(stack, stack_cap * sizeof(char*));
                            if (!ns) break;
                            stack = ns;
                        }
                        stack[stack_len++] = strdup(child_rel);
                    } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
                        fastgit_index_entry_t* found = NULL;
                        bool in_index = (fastgit_index_find(wt->index, child_rel, FASTGIT_INDEX_STAGE_NORMAL, &found) == FASTGIT_OK);
                        if (!in_index) {
                            if (status_count >= capacity) {
                                capacity *= 2;
                                fastgit_status_entry_t* ne = realloc(status_entries, capacity * sizeof(fastgit_status_entry_t));
                                if (!ne) { for (size_t j=0;j<status_count;j++) free(status_entries[j].path); free(status_entries); for(size_t k=0;k<stack_len;k++) free(stack[k]); free(stack); free(rel); closedir(d); return FASTGIT_ENOMEM; }
                                status_entries = ne;
                            }
                            fastgit_status_entry_t* se = &status_entries[status_count++];
                            se->path = strdup(child_rel);
                            se->index_status = FASTGIT_STATUS_CURRENT;
                            se->worktree_status = FASTGIT_STATUS_WT_NEW;
                            se->index_oid = (fastgit_oid_t){0};
                            se->worktree_oid = (fastgit_oid_t){0};
                            se->index_mode = 0;
                            se->worktree_mode = st.st_mode & 0777;
                        }
                    }
                }
                closedir(d);
                free(rel);
            }
            free(stack);
        }
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
    if (path) {
        fastgit_index_entry_t* entry = NULL;
        if (wt->index) fastgit_index_find(wt->index, path, FASTGIT_INDEX_STAGE_NORMAL, &entry);
        if (entry) {
            fastgit_status_entry_t* se = NULL;
            fastgit_error_t err = status_compare_entry(wt, entry, &se);
            if (err != FASTGIT_OK) return err;
            char* diff = NULL;
            size_t diff_len = 0, diff_cap = 0;
            if (se->index_status != FASTGIT_STATUS_CURRENT || se->worktree_status != FASTGIT_STATUS_CURRENT) {
                char header[512];
                int hlen = snprintf(header, sizeof(header), "diff --git a/%s b/%s\n", se->path, se->path);
                if (hlen > 0) diff_append(&diff, &diff_len, &diff_cap, header, hlen);
                hlen = snprintf(header, sizeof(header), "index %06o..%06o\n", se->index_mode & 0777, se->worktree_mode & 0777);
                if (hlen > 0) diff_append(&diff, &diff_len, &diff_cap, header, hlen);
                hlen = snprintf(header, sizeof(header), "--- a/%s\n+++ b/%s\n", se->path, se->path);
                if (hlen > 0) diff_append(&diff, &diff_len, &diff_cap, header, hlen);
            } else {
                diff = calloc(1, 1);
            }
            free(se->path);
            free(se);
            *out = diff;
            return FASTGIT_OK;
        }
    }

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
