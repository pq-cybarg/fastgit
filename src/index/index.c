#include "fastgit/index.h"
#include "fastgit/object.h"
#include "fastgit/hash.h"
#include "fastgit/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#define stat _stat
#define S_IFMT _S_IFMT
#define S_IFREG _S_IFREG
#define S_IFDIR _S_IFDIR
#define S_IFLNK _S_IFLNK
#define mkdir(path, mode) _mkdir(path)
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IXUSR 0
#else
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/mman.h>
#endif

static int index_entry_cmp(const void* a, const void* b) {
    const fastgit_index_entry_t* ea = (const fastgit_index_entry_t*)a;
    const fastgit_index_entry_t* eb = (const fastgit_index_entry_t*)b;
    int cmp = strcmp(ea->path, eb->path);
    if (cmp != 0) return cmp;
    return (int)ea->stage - (int)eb->stage;
}

static void index_entry_from_disk(const fastgit_index_entry_disk_t* disk, fastgit_index_entry_t* entry) {
    entry->ctime_sec = __builtin_bswap32(disk->ctime_sec);
    entry->ctime_nsec = __builtin_bswap32(disk->ctime_nsec);
    entry->mtime_sec = __builtin_bswap32(disk->mtime_sec);
    entry->mtime_nsec = __builtin_bswap32(disk->mtime_nsec);
    entry->dev = __builtin_bswap32(disk->dev);
    entry->ino = __builtin_bswap32(disk->ino);
    entry->mode = __builtin_bswap32(disk->mode);
    entry->uid = __builtin_bswap32(disk->uid);
    entry->gid = __builtin_bswap32(disk->gid);
    entry->size = __builtin_bswap32(disk->size);
    entry->flags = __builtin_bswap16(disk->flags);
    entry->flags_extended = 0;
    entry->stage = (entry->flags >> 12) & 0x3;
    entry->oid.algo = FASTGIT_HASH_SHA256;
    entry->oid.len = 32;
    memcpy(entry->oid.hash, disk->oid, 32);
    entry->path = NULL;
}

static void index_entry_to_disk(const fastgit_index_entry_t* entry, fastgit_index_entry_disk_t* disk) {
    disk->ctime_sec = __builtin_bswap32(entry->ctime_sec);
    disk->ctime_nsec = __builtin_bswap32(entry->ctime_nsec);
    disk->mtime_sec = __builtin_bswap32(entry->mtime_sec);
    disk->mtime_nsec = __builtin_bswap32(entry->mtime_nsec);
    disk->dev = __builtin_bswap32(entry->dev);
    disk->ino = __builtin_bswap32(entry->ino);
    disk->mode = __builtin_bswap32(entry->mode);
    disk->uid = __builtin_bswap32(entry->uid);
    disk->gid = __builtin_bswap32(entry->gid);
    disk->size = __builtin_bswap32(entry->size);
    memcpy(disk->oid, entry->oid.hash, 32);
    disk->flags = __builtin_bswap16(entry->flags);
}

__attribute__((unused)) static void index_entry_free(fastgit_index_entry_t* entry) {
    if (entry) {
        free(entry->path);
    }
}

fastgit_error_t fastgit_index_new(fastgit_index_t** out) {
    if (!out) return FASTGIT_EINVAL;

    fastgit_index_t* index = calloc(1, sizeof(fastgit_index_t));
    if (!index) return FASTGIT_ENOMEM;

    index->capacity = 1024;
    index->entries = calloc(index->capacity, sizeof(fastgit_index_entry_t));
    if (!index->entries) {
        free(index);
        return FASTGIT_ENOMEM;
    }

    index->vfs.stat = stat;
    index->vfs.lstat = lstat;
    index->vfs.fstat = fstat;
    index->vfs.open = open;
    index->vfs.read = read;
    index->vfs.write = write;
    index->vfs.close = close;
    index->vfs.mkdir = mkdir;
    index->vfs.unlink = unlink;
    index->vfs.rename = rename;
    index->vfs.opendir = opendir;
    index->vfs.readdir = readdir;
    index->vfs.closedir = closedir;
    index->sorted = true;

    *out = index;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_index_open(const char* path, fastgit_index_t** out) {
    if (!path || !out) return FASTGIT_EINVAL;

    fastgit_index_t* index = calloc(1, sizeof(fastgit_index_t));
    if (!index) return FASTGIT_ENOMEM;

    index->path = strdup(path);
    if (!index->path) {
        free(index);
        return FASTGIT_ENOMEM;
    }

    index->capacity = 1024;
    index->entries = calloc(index->capacity, sizeof(fastgit_index_entry_t));
    if (!index->entries) {
        free(index->path);
        free(index);
        return FASTGIT_ENOMEM;
    }

    index->vfs.stat = stat;
    index->vfs.lstat = lstat;
    index->vfs.fstat = fstat;
    index->vfs.open = open;
    index->vfs.read = read;
    index->vfs.write = write;
    index->vfs.close = close;
    index->vfs.mkdir = mkdir;
    index->vfs.unlink = unlink;
    index->vfs.rename = rename;
    index->vfs.opendir = opendir;
    index->vfs.readdir = readdir;
    index->vfs.closedir = closedir;
    index->sorted = true;

    fastgit_error_t err = fastgit_index_read(index, path);
    if (err != FASTGIT_OK) {
        fastgit_index_free(index);
        return err;
    }

    *out = index;
    return FASTGIT_OK;
}

void fastgit_index_free(fastgit_index_t* index) {
    if (!index) return;
    for (size_t i = 0; i < index->count; i++) {
        free(index->entries[i].path);
    }
    free(index->entries);
    free(index->path);
    free(index);
}

fastgit_error_t fastgit_index_read(fastgit_index_t* index, const char* path) {
    if (!index || !path) return FASTGIT_EINVAL;

    int fd = index->vfs.open(path, O_RDONLY);
    if (fd < 0) return FASTGIT_ENOENT;

    struct stat st;
    index->vfs.fstat(fd, &st);
    index->mtime = st.st_mtime;

    size_t file_size = st.st_size;
    if (file_size < sizeof(fastgit_index_header_t) + 32) {
        close(fd);
        return FASTGIT_ERROR;
    }

    void* mapped;
#if defined(_WIN32)
    HANDLE hMap = CreateFileMapping((HANDLE)_get_osfhandle(fd), NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        close(fd);
        return FASTGIT_EIO;
    }
    mapped = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap);
#else
    mapped = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return FASTGIT_EIO;
    }
#endif

    uint8_t* ptr = (uint8_t*)mapped;
    uint8_t* end = ptr + file_size;

    fastgit_index_header_t header;
    memcpy(&header, ptr, sizeof(fastgit_index_header_t));
    ptr += sizeof(fastgit_index_header_t);

    header.signature = __builtin_bswap32(header.signature);
    header.version = __builtin_bswap32(header.version);
    header.count = __builtin_bswap32(header.count);

    if (header.signature != FASTGIT_INDEX_SIGNATURE) {
        munmap(mapped, file_size);
        close(fd);
        return FASTGIT_ERROR;
    }

    if (header.version != FASTGIT_INDEX_VERSION) {
        munmap(mapped, file_size);
        close(fd);
        return FASTGIT_EUNSUPPORTED;
    }

    if (header.count > 0) {
        if (index->capacity < header.count) {
            index->capacity = header.count * 2;
            fastgit_index_entry_t* new_entries = realloc(index->entries, index->capacity * sizeof(fastgit_index_entry_t));
            if (!new_entries) {
                munmap(mapped, file_size);
                close(fd);
                return FASTGIT_ENOMEM;
            }
            index->entries = new_entries;
        }

        for (uint32_t i = 0; i < header.count; i++) {
            if (ptr + sizeof(fastgit_index_entry_disk_t) > end) break;

            fastgit_index_entry_disk_t disk_entry;
            memcpy(&disk_entry, ptr, sizeof(fastgit_index_entry_disk_t));
            ptr += sizeof(fastgit_index_entry_disk_t);

            fastgit_index_entry_t* entry = &index->entries[index->count++];
            index_entry_from_disk(&disk_entry, entry);
            const char* path_start = (const char*)ptr;
            entry->path = strdup(path_start ? path_start : "");
            if (!entry->path) {
                munmap(mapped, file_size);
                close(fd);
                return FASTGIT_ENOMEM;
            }
            size_t path_len = entry->flags & 0x0FFF;
            size_t entry_len;
            if (path_len == 0x0FFF) {
                size_t actual = 0;
                while (ptr + actual < end && ptr[actual]) actual++;
                entry_len = sizeof(fastgit_index_entry_disk_t) + actual + 1;
                ptr += actual + 1;
            } else {
                size_t actual = strlen(path_start);
                entry_len = sizeof(fastgit_index_entry_disk_t) + actual + 1;
                ptr += actual + 1;
            }
            size_t pad = (8 - (entry_len % 8)) % 8;
            ptr += pad;
        }
    }

    if (ptr + 32 <= end) {
        index->checksum.algo = FASTGIT_HASH_SHA256;
        index->checksum.len = 32;
        memcpy(index->checksum.digest, ptr, 32);
    }

    index->sorted = true;
    munmap(mapped, file_size);
    close(fd);
    return FASTGIT_OK;
}

fastgit_error_t fastgit_index_write(fastgit_index_t* index) {
    if (!index || !index->path) return FASTGIT_EINVAL;
    return fastgit_index_write_to(index, index->path);
}

fastgit_error_t fastgit_index_write_to(fastgit_index_t* index, const char* path) {
    if (!index || !path) return FASTGIT_EINVAL;

    if (!index->sorted && index->count > 1) {
        qsort(index->entries, index->count, sizeof(fastgit_index_entry_t), index_entry_cmp);
    }
    index->sorted = true;

    size_t total_size = sizeof(fastgit_index_header_t);
    for (size_t i = 0; i < index->count; i++) {
        size_t entry_len = sizeof(fastgit_index_entry_disk_t) + strlen(index->entries[i].path) + 1;
        size_t pad = (8 - (entry_len % 8)) % 8;
        total_size += entry_len + pad;
    }
    total_size += 32;

    void* buf = malloc(total_size);
    if (!buf) return FASTGIT_ENOMEM;

    uint8_t* ptr = (uint8_t*)buf;

    fastgit_index_header_t header = {
        .signature = __builtin_bswap32(FASTGIT_INDEX_SIGNATURE),
        .version = __builtin_bswap32(FASTGIT_INDEX_VERSION),
        .count = __builtin_bswap32(index->count),
    };
    memcpy(ptr, &header, sizeof(fastgit_index_header_t));
    ptr += sizeof(fastgit_index_header_t);

    for (size_t i = 0; i < index->count; i++) {
        fastgit_index_entry_disk_t disk_entry;
        index_entry_to_disk(&index->entries[i], &disk_entry);
        memcpy(ptr, &disk_entry, sizeof(fastgit_index_entry_disk_t));
        ptr += sizeof(fastgit_index_entry_disk_t);

        size_t path_len = strlen(index->entries[i].path);
        memcpy(ptr, index->entries[i].path, path_len + 1);
        ptr += path_len + 1;

        size_t entry_len = sizeof(fastgit_index_entry_disk_t) + path_len + 1;
        size_t pad = (8 - (entry_len % 8)) % 8;
        for (size_t p = 0; p < pad; p++) *ptr++ = 0;
    }

    fastgit_hash(FASTGIT_HASH_SHA256, buf, ptr - (uint8_t*)buf, &index->checksum);
    memcpy(ptr, index->checksum.digest, 32);

    int fd = index->vfs.open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(buf);
        return FASTGIT_EIO;
    }

    ssize_t written = index->vfs.write(fd, buf, total_size);
    index->vfs.close(fd);
    free(buf);

    if (written != (ssize_t)total_size) return FASTGIT_EIO;

    index->dirty = false;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_index_add(fastgit_index_t* index, const char* path) {
    if (!index || !path) return FASTGIT_EINVAL;

    int fd = index->vfs.open(path, O_RDONLY);
    if (fd < 0) return FASTGIT_ENOENT;

    struct stat fst;
    index->vfs.fstat(fd, &fst);
    size_t file_size = (size_t)fst.st_size;
    void* data = NULL;
    if (file_size > 0) {
        data = malloc(file_size);
        if (!data) {
            index->vfs.close(fd);
            return FASTGIT_ENOMEM;
        }
        ssize_t n = index->vfs.read(fd, data, file_size);
        if (n != (ssize_t)file_size) {
            free(data);
            index->vfs.close(fd);
            return FASTGIT_EIO;
        }
    }
    index->vfs.close(fd);

    fastgit_hash_t hash;
    fastgit_error_t err;
    if (file_size == 0) {
        err = fastgit_hash(FASTGIT_HASH_SHA256, "", 0, &hash);
    } else {
        err = fastgit_hash(FASTGIT_HASH_SHA256, data, file_size, &hash);
    }
    if (err != FASTGIT_OK) {
        free(data);
        return err;
    }
    free(data);

    fastgit_oid_t oid;
    oid.algo = FASTGIT_HASH_SHA256;
    oid.len = hash.len;
    memcpy(oid.hash, hash.digest, hash.len);

    uint32_t mode = S_IFREG | 0644;
    if (fst.st_mode & S_IXUSR) mode = S_IFREG | 0755;

    if (index->count >= index->capacity) {
        size_t new_cap = index->capacity ? index->capacity * 2 : 1024;
        fastgit_index_entry_t* new_entries = realloc(index->entries, new_cap * sizeof(fastgit_index_entry_t));
        if (!new_entries) return FASTGIT_ENOMEM;
        index->entries = new_entries;
        index->capacity = new_cap;
    }

    fastgit_index_entry_t* entry = &index->entries[index->count++];
    entry->oid = oid;
    entry->path = strdup(path);
    if (!entry->path) {
        index->count--;
        return FASTGIT_ENOMEM;
    }
    entry->mode = mode;
    entry->stage = FASTGIT_INDEX_STAGE_NORMAL;
    entry->flags = (uint16_t)(strlen(path) & 0xFFF);
    entry->flags_extended = 0;
    entry->ctime_sec = (uint32_t)fst.st_ctime;
#if defined(__APPLE__)
    entry->ctime_nsec = 0;
    entry->mtime_sec = (uint32_t)fst.st_mtime;
    entry->mtime_nsec = 0;
#else
    entry->ctime_nsec = (uint32_t)fst.st_ctim.tv_nsec;
    entry->mtime_sec = (uint32_t)fst.st_mtim.tv_sec;
    entry->mtime_nsec = (uint32_t)fst.st_mtim.tv_nsec;
#endif
    entry->dev = (uint32_t)fst.st_dev;
    entry->ino = (uint32_t)fst.st_ino;
    entry->uid = (uint32_t)fst.st_uid;
    entry->gid = (uint32_t)fst.st_gid;
    entry->size = (uint32_t)file_size;

    index->dirty = true;
    index->sorted = false;
    return FASTGIT_OK;
}

typedef struct {
    const char* path;
    struct stat fst;
    fastgit_hash_t hash;
    fastgit_error_t err;
    size_t file_size;
} bulk_task_t;

static void bulk_hash_fn(void* arg) {
    bulk_task_t* t = (bulk_task_t*)arg;
    int fd = open(t->path, O_RDONLY);
    if (fd < 0) { t->err = FASTGIT_ENOENT; return; }
    if (fstat(fd, &t->fst) != 0) { close(fd); t->err = FASTGIT_EIO; return; }
    t->file_size = (size_t)t->fst.st_size;
    fastgit_error_t he = FASTGIT_OK;
    if (t->file_size == 0) {
        he = fastgit_hash(FASTGIT_HASH_SHA256, "", 0, &t->hash);
    } else {
        void* data = malloc(t->file_size);
        if (!data) { close(fd); t->err = FASTGIT_ENOMEM; return; }
        ssize_t n = read(fd, data, t->file_size);
        close(fd);
        if (n != (ssize_t)t->file_size) { free(data); t->err = FASTGIT_EIO; return; }
        he = fastgit_hash(FASTGIT_HASH_SHA256, data, t->file_size, &t->hash);
        free(data);
        if (he != FASTGIT_OK) { t->err = he; return; }
        t->err = FASTGIT_OK;
        return;
    }
    close(fd);
    t->err = he;
}

fastgit_error_t fastgit_index_add_many(fastgit_index_t* index, const char** paths, size_t count) {
    if (!index || !paths) return FASTGIT_EINVAL;
    if (count == 0) return FASTGIT_OK;
    if (count == 1) return fastgit_index_add(index, paths[0]);

    bulk_task_t* tasks = calloc(count, sizeof(bulk_task_t));
    if (!tasks) return FASTGIT_ENOMEM;
    for (size_t i = 0; i < count; i++) tasks[i].path = paths[i];

    // Try parallel via thread pool, fallback to sequential on failure
    fastgit_thread_pool_t* pool = NULL;
    fastgit_thread_pool_config_t cfg = {0};
    cfg.worker_count = (int)(count < 16 ? count : 16);
    cfg.max_queue_depth = (int)count + 4;
    bool use_pool = (fastgit_thread_pool_new(&cfg, &pool) == FASTGIT_OK);

    if (use_pool) {
        for (size_t i = 0; i < count; i++) {
            fastgit_thread_pool_submit(pool, bulk_hash_fn, &tasks[i]);
        }
        fastgit_thread_pool_wait(pool);
        fastgit_thread_pool_free(pool);
    } else {
        for (size_t i = 0; i < count; i++) bulk_hash_fn(&tasks[i]);
    }

    // Count successes to grow once
    size_t ok = 0;
    for (size_t i = 0; i < count; i++) if (tasks[i].err == FASTGIT_OK) ok++;
    if (ok == 0) {
        fastgit_error_t first = tasks[0].err;
        free(tasks);
        return first;
    }
    if (index->count + ok > index->capacity) {
        size_t need = index->count + ok;
        size_t new_cap = index->capacity ? index->capacity : 1024;
        while (new_cap < need) new_cap *= 2;
        fastgit_index_entry_t* ne = realloc(index->entries, new_cap * sizeof(fastgit_index_entry_t));
        if (!ne) { free(tasks); return FASTGIT_ENOMEM; }
        index->entries = ne;
        index->capacity = new_cap;
    }
    // Bulk insert
    for (size_t i = 0; i < count; i++) {
        if (tasks[i].err != FASTGIT_OK) continue;
        fastgit_oid_t oid;
        oid.algo = FASTGIT_HASH_SHA256;
        oid.len = tasks[i].hash.len;
        memcpy(oid.hash, tasks[i].hash.digest, tasks[i].hash.len);
        uint32_t mode = S_IFREG | 0644;
        if (tasks[i].fst.st_mode & S_IXUSR) mode = S_IFREG | 0755;
        fastgit_index_entry_t* e = &index->entries[index->count++];
        e->oid = oid;
        e->path = strdup(tasks[i].path);
        if (!e->path) { index->count--; continue; }
        e->mode = mode;
        e->stage = FASTGIT_INDEX_STAGE_NORMAL;
        e->flags = (uint16_t)(strlen(tasks[i].path) & 0xFFF);
        e->flags_extended = 0;
        e->ctime_sec = (uint32_t)tasks[i].fst.st_ctime;
#if defined(__APPLE__)
        e->ctime_nsec = 0;
        e->mtime_sec = (uint32_t)tasks[i].fst.st_mtime;
        e->mtime_nsec = 0;
#else
        e->ctime_nsec = (uint32_t)tasks[i].fst.st_ctim.tv_nsec;
        e->mtime_sec = (uint32_t)tasks[i].fst.st_mtim.tv_sec;
        e->mtime_nsec = (uint32_t)tasks[i].fst.st_mtim.tv_nsec;
#endif
        e->dev = (uint32_t)tasks[i].fst.st_dev;
        e->ino = (uint32_t)tasks[i].fst.st_ino;
        e->uid = (uint32_t)tasks[i].fst.st_uid;
        e->gid = (uint32_t)tasks[i].fst.st_gid;
        e->size = (uint32_t)tasks[i].file_size;
    }
    free(tasks);
    index->dirty = true;
    index->sorted = false;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_index_add_from_buffer(fastgit_index_t* index, const char* path, uint32_t mode, const void* data __attribute__((unused)), size_t len __attribute__((unused))) {
    if (!index || !path) return FASTGIT_EINVAL;

    if (index->count >= index->capacity) {
        index->capacity *= 2;
        fastgit_index_entry_t* new_entries = realloc(index->entries, index->capacity * sizeof(fastgit_index_entry_t));
        if (!new_entries) return FASTGIT_ENOMEM;
        index->entries = new_entries;
    }

    fastgit_oid_t oid;
    oid.algo = FASTGIT_HASH_SHA256;
    oid.len = 32;
    memset(oid.hash, 0, 32);

    fastgit_index_entry_t* entry = &index->entries[index->count++];
    entry->oid = oid;
    entry->path = strdup(path);
    entry->mode = mode;
    entry->stage = FASTGIT_INDEX_STAGE_NORMAL;
    entry->flags = (uint16_t)strlen(path);
    entry->flags_extended = 0;

    entry->ctime_sec = 0;
    entry->ctime_nsec = 0;
    entry->mtime_sec = 0;
    entry->mtime_nsec = 0;
    entry->dev = 0;
    entry->ino = 0;
    entry->uid = 0;
    entry->gid = 0;
    entry->size = (uint32_t)len;

    index->dirty = true;
    index->sorted = false;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_index_remove(fastgit_index_t* index, const char* path, uint32_t stage) {
    if (!index || !path) return FASTGIT_EINVAL;
    if (!index->sorted && index->count > 1) {
        qsort(index->entries, index->count, sizeof(fastgit_index_entry_t), index_entry_cmp);
        index->sorted = true;
    }
    if (index->sorted && index->count > 0) {
        fastgit_index_entry_t key;
        memset(&key, 0, sizeof(key));
        key.path = (char*)path;
        key.stage = stage;
        fastgit_index_entry_t* found = bsearch(&key, index->entries, index->count, sizeof(fastgit_index_entry_t), index_entry_cmp);
        if (found) {
            size_t idx = (size_t)(found - index->entries);
            free(found->path);
            memmove(&index->entries[idx], &index->entries[idx + 1], (index->count - idx - 1) * sizeof(fastgit_index_entry_t));
            index->count--;
            index->dirty = true;
            return FASTGIT_OK;
        }
        return FASTGIT_ENOENT;
    }
    for (size_t i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0 && index->entries[i].stage == stage) {
            free(index->entries[i].path);
            memmove(&index->entries[i], &index->entries[i + 1], (index->count - i - 1) * sizeof(fastgit_index_entry_t));
            index->count--;
            index->dirty = true;
            return FASTGIT_OK;
        }
    }
    return FASTGIT_ENOENT;
}

fastgit_error_t fastgit_index_clear(fastgit_index_t* index) {
    if (!index) return FASTGIT_EINVAL;
    for (size_t i = 0; i < index->count; i++) {
        free(index->entries[i].path);
    }
    index->count = 0;
    index->dirty = true;
    index->sorted = true;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_index_find(fastgit_index_t* index, const char* path, uint32_t stage, fastgit_index_entry_t** out) {
    if (!index || !path || !out) return FASTGIT_EINVAL;
    if (!index->sorted && index->count > 1) {
        qsort(index->entries, index->count, sizeof(fastgit_index_entry_t), index_entry_cmp);
        index->sorted = true;
    }
    if (index->sorted && index->count > 0) {
        fastgit_index_entry_t key;
        memset(&key, 0, sizeof(key));
        key.path = (char*)path;
        key.stage = stage;
        fastgit_index_entry_t* found = bsearch(&key, index->entries, index->count, sizeof(fastgit_index_entry_t), index_entry_cmp);
        if (found) {
            *out = found;
            return FASTGIT_OK;
        }
        return FASTGIT_ENOENT;
    }
    for (size_t i = 0; i < index->count; i++) {
        if (index->entries[i].stage == stage && strcmp(index->entries[i].path, path) == 0) {
            *out = &index->entries[i];
            return FASTGIT_OK;
        }
    }
    return FASTGIT_ENOENT;
}

fastgit_error_t fastgit_index_find_by_oid(fastgit_index_t* index, const fastgit_oid_t* oid, fastgit_index_entry_t** out) {
    if (!index || !oid || !out) return FASTGIT_EINVAL;

    for (size_t i = 0; i < index->count; i++) {
        if (fastgit_oid_equal(&index->entries[i].oid, oid)) {
            *out = &index->entries[i];
            return FASTGIT_OK;
        }
    }
    return FASTGIT_ENOENT;
}

size_t fastgit_index_entry_count(fastgit_index_t* index) {
    return index ? index->count : 0;
}

const fastgit_index_entry_t* fastgit_index_entry_by_index(fastgit_index_t* index, size_t n) {
    if (!index || n >= index->count) return NULL;
    return &index->entries[n];
}

fastgit_error_t fastgit_index_conflict_add(fastgit_index_t* index __attribute__((unused)), const char* path __attribute__((unused)),
    const fastgit_oid_t* ancestor __attribute__((unused)), const fastgit_oid_t* ours __attribute__((unused)), const fastgit_oid_t* theirs __attribute__((unused))) {
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_index_conflict_remove(fastgit_index_t* index __attribute__((unused)), const char* path __attribute__((unused))) {
    return FASTGIT_EUNSUPPORTED;
}

bool fastgit_index_has_conflicts(fastgit_index_t* index) {
    if (!index) return false;
    for (size_t i = 0; i < index->count; i++) {
        if (index->entries[i].stage != FASTGIT_INDEX_STAGE_NORMAL) return true;
    }
    return false;
}

fastgit_error_t fastgit_index_read_tree(fastgit_index_t* index __attribute__((unused)), const fastgit_oid_t* tree_oid __attribute__((unused))) {
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_index_write_tree(fastgit_index_t* index __attribute__((unused)), fastgit_oid_t* out __attribute__((unused))) {
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_index_stats(fastgit_index_t* index, fastgit_index_stats_t* out) {
    if (!index || !out) return;
    out->entries = index->count;
    out->size = 0;
    out->mtime = index->mtime;
    out->checksum = index->checksum;
    out->dirty = index->dirty;
}

fastgit_error_t fastgit_index_set_vfs(fastgit_index_t* index, const fastgit_index_vfs_t* vfs) {
    if (!index || !vfs) return FASTGIT_EINVAL;
    index->vfs = *vfs;
    return FASTGIT_OK;
}
