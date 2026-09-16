#include "fastgit/submodule.h"
#include "fastgit/fastgit.h"
#include "fastgit/odb.h"
#include "fastgit/index.h"
#include "fastgit/hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

static fastgit_error_t parse_gitmodules(const char* data, size_t len, fastgit_submodule_info_t*** out_info, size_t* out_count) {
    *out_info = NULL;
    *out_count = 0;
    
    fastgit_submodule_info_t** info = NULL;
    size_t count = 0, cap = 0;
    
    char* buf = strdup(data);
    if (!buf) return FASTGIT_ENOMEM;
    
    char* line = strtok(buf, "\n");
    fastgit_submodule_info_t* current = NULL;
    
    while (line) {
        // Trim whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        char* end = start + strlen(start) - 1;
        while (end > start && (*end == ' ' || *end == '\t' || *end == '\r')) *end-- = '\0';
        
        if (strncmp(start, "[submodule \"", 12) == 0) {
            if (current) {
                if (count >= cap) {
                    size_t ncap = cap ? cap * 2 : 8;
                    fastgit_submodule_info_t** nb = realloc(info, ncap * sizeof(fastgit_submodule_info_t*));
                    if (!nb) { free(buf); for (size_t i = 0; i < count; i++) { free(info[i]->name); free(info[i]->path); free(info[i]->url); free(info[i]->branch); free(info[i]); } free(info); return FASTGIT_ENOMEM; }
                    info = nb; cap = ncap;
                }
                info[count++] = current;
            }
            current = calloc(1, sizeof(fastgit_submodule_info_t));
            if (!current) { free(buf); for (size_t i = 0; i < count; i++) { free(info[i]->name); free(info[i]->path); free(info[i]->url); free(info[i]->branch); free(info[i]); } free(info); return FASTGIT_ENOMEM; }
            
            char* name_start = start + 12;
            char* name_end = strchr(name_start, '"');
            if (name_end) {
                *name_end = '\0';
                current->name = strdup(name_start);
            }
        } else if (current) {
            char* eq = strchr(start, '=');
            if (eq) {
                *eq = '\0';
                char* key = start;
                char* value = eq + 1;
                while (*key == ' ' || *key == '\t') key++;
                char* ke = key + strlen(key) - 1;
                while (ke > key && (*ke == ' ' || *ke == '\t')) *ke-- = '\0';
                while (*value == ' ' || *value == '\t') value++;
                char* ve = value + strlen(value) - 1;
                while (ve > value && (*ve == ' ' || *ve == '\t' || *ve == '\r')) *ve-- = '\0';
                
                if (strcmp(key, "path") == 0) current->path = strdup(value);
                else if (strcmp(key, "url") == 0) current->url = strdup(value);
                else if (strcmp(key, "branch") == 0) current->branch = strdup(value);
            }
        }
        line = strtok(NULL, "\n");
    }
    
    if (current) {
        if (count >= cap) {
            size_t ncap = cap ? cap * 2 : 8;
            fastgit_submodule_info_t** nb = realloc(info, ncap * sizeof(fastgit_submodule_info_t*));
            if (!nb) { free(buf); for (size_t i = 0; i < count; i++) { free(info[i]->name); free(info[i]->path); free(info[i]->url); free(info[i]->branch); free(info[i]); } free(info); return FASTGIT_ENOMEM; }
            info = nb; cap = ncap;
        }
        info[count++] = current;
    }
    
    free(buf);
    *out_info = info;
    *out_count = count;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_submodule_parse_gitmodules(fastgit_repository_t* repo, fastgit_submodule_info_t*** out_info, size_t* out_count) {
    if (!repo || !out_info || !out_count) return FASTGIT_EINVAL;
    
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    if (!odb) return FASTGIT_EIO;
    
    // Try to find .gitmodules in index or HEAD
    fastgit_index_t* index = fastgit_repository_index(repo);
    fastgit_index_entry_t* entry = NULL;
    if (index && fastgit_index_find(index, ".gitmodules", 0, &entry) == FASTGIT_OK) {
        fastgit_odb_object_t obj;
        if (fastgit_odb_read(odb, &entry->oid, &obj) == FASTGIT_OK) {
            fastgit_error_t err = parse_gitmodules(obj.data, obj.size, out_info, out_count);
            free(obj.data);
            return err;
        }
    }
    
    // Fallback: try to read from worktree
    char path[1024];
    snprintf(path, sizeof(path), "%s/.gitmodules", fastgit_repository_worktree(repo) ? "." : ".");
    FILE* f = fopen(path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            char* data = malloc(sz + 1);
            if (data) {
                if (fread(data, 1, sz, f) == (size_t)sz) {
                    data[sz] = '\0';
                    fastgit_error_t err = parse_gitmodules(data, sz, out_info, out_count);
                    free(data);
                    fclose(f);
                    return err;
                }
                free(data);
            }
        }
        fclose(f);
    }
    
    return FASTGIT_ENOENT;
}

fastgit_error_t fastgit_submodule_add(fastgit_repository_t* repo, const char* url, const char* path, const char* branch) {
    if (!repo || !url || !path) return FASTGIT_EINVAL;
    
    // Clone the submodule
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", fastgit_repository_worktree(repo) ? "." : ".", path);
    
    // Add to .gitmodules
    char gitmodules_path[1024];
    snprintf(gitmodules_path, sizeof(gitmodules_path), "%s/.gitmodules", fastgit_repository_worktree(repo) ? "." : ".");
    
    FILE* f = fopen(gitmodules_path, "a");
    if (!f) {
        f = fopen(gitmodules_path, "w");
        if (!f) return FASTGIT_EIO;
    }
    
    char name[512];
    const char* name_start = strrchr(path, '/');
    if (name_start) name_start++;
    else name_start = path;
    snprintf(name, sizeof(name), "%s", name_start);
    
    fprintf(f, "[submodule \"%s\"]\n", name);
    fprintf(f, "\tpath = %s\n", path);
    fprintf(f, "\turl = %s\n", url);
    if (branch) fprintf(f, "\tbranch = %s\n", branch);
    fprintf(f, "\n");
    fclose(f);
    
    // Stage .gitmodules
    fastgit_index_t* index = fastgit_repository_index(repo);
    fastgit_index_add(index, ".gitmodules");
    fastgit_index_write(index);
    
    // Clone submodule
    char clone_path[1024];
    snprintf(clone_path, sizeof(clone_path), "%s/%s", fastgit_repository_worktree(repo) ? "." : ".", path);
    fastgit_clone(url, clone_path, NULL);
    
    return FASTGIT_OK;
}

fastgit_error_t fastgit_submodule_init(fastgit_repository_t* repo, const char* name) {
    if (!repo || !name) return FASTGIT_EINVAL;
    
    fastgit_submodule_info_t** info = NULL;
    size_t count = 0;
    if (fastgit_submodule_parse_gitmodules(repo, &info, &count) != FASTGIT_OK) {
        return FASTGIT_ENOENT;
    }
    
    for (size_t i = 0; i < count; i++) {
        if (info[i]->name && strcmp(info[i]->name, name) == 0) {
            // Clone if not already present
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", fastgit_repository_worktree(repo) ? "." : ".", info[i]->path);
            if (access(path, F_OK) != 0) {
                fastgit_clone(info[i]->url, path, NULL);
            }
            fastgit_submodule_info_free(info, count);
            return FASTGIT_OK;
        }
    }
    fastgit_submodule_info_free(info, count);
    return FASTGIT_ENOENT;
}

fastgit_error_t fastgit_submodule_update(fastgit_repository_t* repo, const char* name, bool init) {
    if (!repo || !name) return FASTGIT_EINVAL;
    
    fastgit_submodule_info_t** info = NULL;
    size_t count = 0;
    if (fastgit_submodule_parse_gitmodules(repo, &info, &count) != FASTGIT_OK) {
        return FASTGIT_ENOENT;
    }
    
    for (size_t i = 0; i < count; i++) {
        if (info[i]->name && strcmp(info[i]->name, name) == 0) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", fastgit_repository_worktree(repo) ? "." : ".", info[i]->path);
            
            // Fetch latest
            fastgit_repository_t* subrepo = NULL;
            if (fastgit_repository_open(path, &subrepo) == FASTGIT_OK) {
                fastgit_fetch(subrepo, "origin", NULL);
                if (info[i]->branch) {
                    fastgit_oid_t oid;
                    fastgit_rev_parse(subrepo, info[i]->branch, &oid);
                    fastgit_merge(subrepo, &oid, NULL);
                }
                fastgit_repository_free(subrepo);
            } else if (init) {
                fastgit_clone(info[i]->url, path, NULL);
            }
            fastgit_submodule_info_free(info, count);
            return FASTGIT_OK;
        }
    }
    fastgit_submodule_info_free(info, count);
    return FASTGIT_ENOENT;
}

fastgit_error_t fastgit_submodule_foreach(fastgit_repository_t* repo, int (*callback)(fastgit_submodule_info_t* info, void* payload), void* payload) {
    if (!repo || !callback) return FASTGIT_EINVAL;
    
    fastgit_submodule_info_t** info = NULL;
    size_t count = 0;
    if (fastgit_submodule_parse_gitmodules(repo, &info, &count) != FASTGIT_OK) {
        return FASTGIT_ENOENT;
    }
    
    for (size_t i = 0; i < count; i++) {
        callback(info[i], payload);
    }
    
    fastgit_submodule_info_free(info, count);
    return FASTGIT_OK;
}

fastgit_error_t fastgit_submodule_status(fastgit_repository_t* repo, fastgit_submodule_info_t*** out_info, size_t* count) {
    return fastgit_submodule_parse_gitmodules(repo, out_info, count);
}

void fastgit_submodule_info_free(fastgit_submodule_info_t** info, size_t count) {
    if (!info) return;
    for (size_t i = 0; i < count; i++) {
        free(info[i]->name);
        free(info[i]->path);
        free(info[i]->url);
        free(info[i]->branch);
        free(info[i]);
    }
    free(info);
}