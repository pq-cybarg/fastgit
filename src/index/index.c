#include "fastgit/index.h"
#include "fastgit/object.h"
#include "fastgit/hash.h"
#include "fastgit/odb.h"
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

static fastgit_error_t odb_path_from_index(const fastgit_index_t* idx, char* out, size_t outsz);
static int index_entry_cmp(const void* a, const void* b) {
    const fastgit_index_entry_t* ea = (const fastgit_index_entry_t*)a;
    const fastgit_index_entry_t* eb = (const fastgit_index_entry_t*)b;
    int cmp = strcmp(ea->path, eb->path);
    if (cmp != 0) return cmp;
    return (int)ea->stage - (int)eb->stage;
}

static uint8_t index_algo_from_len(size_t len) {
    if (len == 20) return FASTGIT_HASH_SHA1;
    if (len == 32) return FASTGIT_HASH_SHA256;
    if (len == 48) return FASTGIT_HASH_SHA384;
    if (len == 64) return FASTGIT_HASH_SHA3_512;
    return FASTGIT_HASH_SHA256;
}
static void index_entry_from_disk(const fastgit_index_entry_disk_t* disk, fastgit_index_entry_t* entry, uint8_t algo, size_t oid_len) {
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
    // disk->oid is at offset 40; flags follows oid_len
    entry->flags = 0;
    entry->flags_extended = 0;
    entry->stage = 0;
    entry->oid.algo = algo;
    entry->oid.len = oid_len;
    memcpy(entry->oid.hash, disk->oid, oid_len);
    entry->path = NULL;
    // flags will be set by caller after reading variable oid
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
    memcpy(disk->oid, entry->oid.hash, entry->oid.len > 64 ? 64 : entry->oid.len);
    // flags set by caller
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
    index->oid_algo = FASTGIT_HASH_SHA256;
    index->oid_len = 32;
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

        // oid_len agility: detect from file size if ambiguous; default to index->oid_len (32) else infer from remaining
        // For now use stored oid_len (32) but compute fixed size as 40+oid_len+2
        if (index->oid_len == 0) { index->oid_len = 32; index->oid_algo = FASTGIT_HASH_SHA256; }
        size_t fixed_sz = 40 + index->oid_len + 2;
        for (uint32_t i = 0; i < header.count; i++) {
            if (ptr + fixed_sz > end) break;

            fastgit_index_entry_disk_t disk_entry;
            // parse fixed fields manually to support variable oid_len
            uint32_t tmp;
            memcpy(&tmp, ptr, 4); disk_entry.ctime_sec = tmp; ptr += 4;
            memcpy(&tmp, ptr, 4); disk_entry.ctime_nsec = tmp; ptr += 4;
            memcpy(&tmp, ptr, 4); disk_entry.mtime_sec = tmp; ptr += 4;
            memcpy(&tmp, ptr, 4); disk_entry.mtime_nsec = tmp; ptr += 4;
            memcpy(&tmp, ptr, 4); disk_entry.dev = tmp; ptr += 4;
            memcpy(&tmp, ptr, 4); disk_entry.ino = tmp; ptr += 4;
            memcpy(&tmp, ptr, 4); disk_entry.mode = tmp; ptr += 4;
            memcpy(&tmp, ptr, 4); disk_entry.uid = tmp; ptr += 4;
            memcpy(&tmp, ptr, 4); disk_entry.gid = tmp; ptr += 4;
            memcpy(&tmp, ptr, 4); disk_entry.size = tmp; ptr += 4;
            memcpy(disk_entry.oid, ptr, index->oid_len); ptr += index->oid_len;
            uint16_t fl; memcpy(&fl, ptr, 2); disk_entry.flags = fl; ptr += 2;

            fastgit_index_entry_t* entry = &index->entries[index->count++];
            index_entry_from_disk(&disk_entry, entry, index->oid_algo, index->oid_len);
            // flags path len is in entry->flags (bswapped later)
            uint16_t be_flags = __builtin_bswap16(disk_entry.flags);
            entry->flags = be_flags;
            entry->stage = (be_flags >> 12) & 0x3;
            const char* path_start = (const char*)ptr;
            size_t remaining = (size_t)(end - ptr);
            size_t bounded_len = strnlen(path_start, remaining);
            if (bounded_len == remaining) { munmap(mapped, file_size); close(fd); return FASTGIT_ERROR; }
            entry->path = strdup(path_start);
            if (!entry->path) {
                munmap(mapped, file_size);
                close(fd);
                return FASTGIT_ENOMEM;
            }
            size_t path_len_flag = be_flags & 0x0FFF;
            size_t actual;
            if (path_len_flag == 0x0FFF) {
                actual = 0;
                while (ptr + actual < end && ptr[actual]) actual++;
            } else {
                actual = bounded_len;
            }
            size_t entry_len = fixed_sz + actual + 1;
            ptr += actual + 1;
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

    if (index->oid_len == 0) { index->oid_len = 32; index->oid_algo = FASTGIT_HASH_SHA256; }
    size_t fixed_sz = 40 + index->oid_len + 2;
    size_t total_size = sizeof(fastgit_index_header_t);
    for (size_t i = 0; i < index->count; i++) {
        size_t entry_len = fixed_sz + strlen(index->entries[i].path) + 1;
        size_t pad = (8 - (entry_len % 8)) % 8;
        total_size += entry_len + pad;
    }
    total_size += index->oid_len;

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
        fastgit_index_entry_t* e = &index->entries[i];
        // write fixed fields with oid_len agility
        uint32_t be32;
        be32 = __builtin_bswap32(e->ctime_sec); memcpy(ptr, &be32, 4); ptr+=4;
        be32 = __builtin_bswap32(e->ctime_nsec); memcpy(ptr, &be32, 4); ptr+=4;
        be32 = __builtin_bswap32(e->mtime_sec); memcpy(ptr, &be32, 4); ptr+=4;
        be32 = __builtin_bswap32(e->mtime_nsec); memcpy(ptr, &be32, 4); ptr+=4;
        be32 = __builtin_bswap32(e->dev); memcpy(ptr, &be32, 4); ptr+=4;
        be32 = __builtin_bswap32(e->ino); memcpy(ptr, &be32, 4); ptr+=4;
        be32 = __builtin_bswap32(e->mode); memcpy(ptr, &be32, 4); ptr+=4;
        be32 = __builtin_bswap32(e->uid); memcpy(ptr, &be32, 4); ptr+=4;
        be32 = __builtin_bswap32(e->gid); memcpy(ptr, &be32, 4); ptr+=4;
        be32 = __builtin_bswap32(e->size); memcpy(ptr, &be32, 4); ptr+=4;
        memcpy(ptr, e->oid.hash, index->oid_len > e->oid.len ? e->oid.len : index->oid_len);
        if (index->oid_len > e->oid.len) memset(ptr + e->oid.len, 0, index->oid_len - e->oid.len);
        ptr += index->oid_len;
        uint16_t raw = e->flags;
        if (raw == 0) raw = (uint16_t)(strlen(e->path) & 0x0FFF) | ((uint16_t)e->stage << 12);
        uint16_t be16 = __builtin_bswap16(raw);
        memcpy(ptr, &be16, 2); ptr+=2;

        size_t path_len = strlen(e->path);
        memcpy(ptr, e->path, path_len + 1);
        ptr += path_len + 1;

        size_t entry_len = fixed_sz + path_len + 1;
        size_t pad = (8 - (entry_len % 8)) % 8;
        for (size_t p = 0; p < pad; p++) *ptr++ = 0;
    }

    fastgit_hash(index->oid_algo, buf, ptr - (uint8_t*)buf, &index->checksum);
    memcpy(ptr, index->checksum.digest, index->checksum.len ? index->checksum.len : index->oid_len);

    char lock_path[4096];
    snprintf(lock_path, sizeof(lock_path), "%s.lock", path);
    int fd = index->vfs.open(lock_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(buf);
        return FASTGIT_EIO;
    }
    ssize_t written = index->vfs.write(fd, buf, total_size);
#if !defined(_WIN32)
    fsync(fd);
#endif
    index->vfs.close(fd);
    free(buf);
    if (written != (ssize_t)total_size) { index->vfs.unlink(lock_path); return FASTGIT_EIO; }
    if (index->vfs.rename(lock_path, path) != 0) { index->vfs.unlink(lock_path); return FASTGIT_EIO; }
#if !defined(_WIN32)
    /* directory fsync for crash safety */
    {
        char dir[4096]; strncpy(dir, path, sizeof(dir)-1); dir[sizeof(dir)-1]=0;
        char* sl=strrchr(dir,'/'); if(sl){ *sl=0; int df = open(dir, O_DIRECTORY); if(df>=0){ fsync(df); close(df);} }
    }
#endif

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
    fastgit_error_t err = FASTGIT_OK;
    {
        char hdr[32];
        int hdr_len = snprintf(hdr, sizeof(hdr), "blob %zu", file_size);
        hdr[hdr_len++] = '\0';
        fastgit_hash_ctx_t* hctx = fastgit_hash_ctx_new(FASTGIT_HASH_SHA256);
        if (!hctx) { free(data); return FASTGIT_ENOMEM; }
        fastgit_hash_ctx_init(hctx);
        fastgit_hash_ctx_update(hctx, hdr, (size_t)hdr_len);
        if (file_size > 0 && data) fastgit_hash_ctx_update(hctx, data, file_size);
        err = fastgit_hash_ctx_final(hctx, &hash);
        fastgit_hash_ctx_free(hctx);
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

    /* Git-shaped: write blob to ODB before updating cache entry */
    if (index->path) {
        char odb_path[4096];
        if (odb_path_from_index(index, odb_path, sizeof(odb_path)) == FASTGIT_OK) {
            fastgit_odb_t* odb = NULL;
            if (fastgit_odb_open(odb_path, &odb) == FASTGIT_OK) {
                fastgit_oid_t woid;
                /* odb_write re-serializes header; use raw data */
                fastgit_odb_write(odb, FASTGIT_OBJ_BLOB, data ? data : "", file_size, &woid);
                fastgit_odb_free(odb);
            }
        }
    }

    uint32_t mode = S_IFREG | 0644;
    if (fst.st_mode & S_IXUSR) mode = S_IFREG | 0755;

    /* replace by (path, stage) like Git, not append */
    for (size_t i = 0; i < index->count; i++) {
        if (index->entries[i].stage == FASTGIT_INDEX_STAGE_NORMAL && strcmp(index->entries[i].path, path) == 0) {
            /* update in place */
            index->entries[i].oid = oid;
            index->entries[i].mode = mode;
            index->entries[i].size = (uint32_t)file_size;
            index->entries[i].ctime_sec = (uint32_t)fst.st_ctime;
#if defined(__APPLE__)
            index->entries[i].ctime_nsec = 0;
            index->entries[i].mtime_sec = (uint32_t)fst.st_mtime;
            index->entries[i].mtime_nsec = 0;
#else
            index->entries[i].ctime_nsec = (uint32_t)fst.st_ctim.tv_nsec;
            index->entries[i].mtime_sec = (uint32_t)fst.st_mtim.tv_sec;
            index->entries[i].mtime_nsec = (uint32_t)fst.st_mtim.tv_nsec;
#endif
            index->entries[i].dev = (uint32_t)fst.st_dev;
            index->entries[i].ino = (uint32_t)fst.st_ino;
            index->entries[i].uid = (uint32_t)fst.st_uid;
            index->entries[i].gid = (uint32_t)fst.st_gid;
            index->entries[i].flags = (uint16_t)(strlen(path) & 0xFFF);
            free(data);
            index->dirty = true;
            /* keep sorted flag; qsort will re-sort on write */
            return FASTGIT_OK;
        }
    }

    if (index->count >= index->capacity) {
        size_t new_cap = index->capacity ? index->capacity * 2 : 1024;
        fastgit_index_entry_t* new_entries = realloc(index->entries, new_cap * sizeof(fastgit_index_entry_t));
        if (!new_entries) { free(data); return FASTGIT_ENOMEM; }
        index->entries = new_entries;
        index->capacity = new_cap;
    }

    fastgit_index_entry_t* entry = &index->entries[index->count++];
    entry->oid = oid;
    entry->path = strdup(path);
    if (!entry->path) {
        index->count--;
        free(data);
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
    free(data);

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
    void* data = NULL;
    if (t->file_size > 0) {
        data = malloc(t->file_size);
        if (!data) { close(fd); t->err = FASTGIT_ENOMEM; return; }
        ssize_t n = read(fd, data, t->file_size);
        if (n != (ssize_t)t->file_size) { free(data); close(fd); t->err = FASTGIT_EIO; return; }
    }
    close(fd);
    {
        char hdr[32];
        int hdr_len = snprintf(hdr, sizeof(hdr), "blob %zu", t->file_size);
        hdr[hdr_len++] = '\0';
        fastgit_hash_ctx_t* hctx = fastgit_hash_ctx_new(FASTGIT_HASH_SHA256);
        if (!hctx) { free(data); t->err = FASTGIT_ENOMEM; return; }
        fastgit_hash_ctx_init(hctx);
        fastgit_hash_ctx_update(hctx, hdr, (size_t)hdr_len);
        if (t->file_size > 0) fastgit_hash_ctx_update(hctx, data, t->file_size);
        fastgit_error_t he = fastgit_hash_ctx_final(hctx, &t->hash);
        fastgit_hash_ctx_free(hctx);
        free(data);
        t->err = he;
    }
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
    /* Git-shaped: write blobs to ODB before index update */
    if (index->path) {
        char odb_path[4096];
        if (odb_path_from_index(index, odb_path, sizeof(odb_path)) == FASTGIT_OK) {
            fastgit_odb_t* odb = NULL;
            if (fastgit_odb_open(odb_path, &odb) == FASTGIT_OK) {
                for (size_t i = 0; i < count; i++) {
                    if (tasks[i].err != FASTGIT_OK) continue;
                    size_t len = tasks[i].file_size;
                    void* data = NULL;
                    if (len > 0) {
                        data = malloc(len);
                        if (!data) continue;
                        int fd = open(tasks[i].path, O_RDONLY);
                        if (fd >= 0) { ssize_t n = read(fd, data, len); (void)n; close(fd); }
                    }
                    fastgit_oid_t woid;
                    fastgit_odb_write(odb, FASTGIT_OBJ_BLOB, data ? data : "", len, &woid);
                    free(data);
                }
                fastgit_odb_free(odb);
            }
        }
    }
    // Bulk insert with replace-by-(path,stage) dedup
    for (size_t i = 0; i < count; i++) {
        if (tasks[i].err != FASTGIT_OK) continue;
        fastgit_oid_t oid;
        oid.algo = FASTGIT_HASH_SHA256;
        oid.len = tasks[i].hash.len;
        memcpy(oid.hash, tasks[i].hash.digest, tasks[i].hash.len);
        uint32_t mode = S_IFREG | 0644;
        if (tasks[i].fst.st_mode & S_IXUSR) mode = S_IFREG | 0755;
        /* check existing */
        bool replaced = false;
        for (size_t k = 0; k < index->count; k++) {
            if (index->entries[k].stage == FASTGIT_INDEX_STAGE_NORMAL && strcmp(index->entries[k].path, tasks[i].path) == 0) {
                index->entries[k].oid = oid;
                index->entries[k].mode = mode;
                index->entries[k].size = (uint32_t)tasks[i].file_size;
                index->entries[k].ctime_sec = (uint32_t)tasks[i].fst.st_ctime;
#if defined(__APPLE__)
                index->entries[k].ctime_nsec = 0;
                index->entries[k].mtime_sec = (uint32_t)tasks[i].fst.st_mtime;
                index->entries[k].mtime_nsec = 0;
#else
                index->entries[k].ctime_nsec = (uint32_t)tasks[i].fst.st_ctim.tv_nsec;
                index->entries[k].mtime_sec = (uint32_t)tasks[i].fst.st_mtim.tv_sec;
                index->entries[k].mtime_nsec = (uint32_t)tasks[i].fst.st_mtim.tv_nsec;
#endif
                index->entries[k].dev = (uint32_t)tasks[i].fst.st_dev;
                index->entries[k].ino = (uint32_t)tasks[i].fst.st_ino;
                index->entries[k].uid = (uint32_t)tasks[i].fst.st_uid;
                index->entries[k].gid = (uint32_t)tasks[i].fst.st_gid;
                index->entries[k].flags = (uint16_t)(strlen(tasks[i].path) & 0xFFF);
                replaced = true;
                break;
            }
        }
        if (replaced) continue;
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

fastgit_error_t fastgit_index_add_from_buffer(fastgit_index_t* index, const char* path, uint32_t mode, const void* data, size_t len) {
    if (!index || !path) return FASTGIT_EINVAL;

    if (index->count >= index->capacity) {
        size_t new_cap = index->capacity ? index->capacity * 2 : 1024;
        fastgit_index_entry_t* new_entries = realloc(index->entries, new_cap * sizeof(fastgit_index_entry_t));
        if (!new_entries) return FASTGIT_ENOMEM;
        index->entries = new_entries;
        index->capacity = new_cap;
    }

    fastgit_hash_t h;
    {
        char hdr[32];
        int hdr_len = snprintf(hdr, sizeof(hdr), "blob %zu", len);
        hdr[hdr_len++] = '\0';
        fastgit_hash_ctx_t* hctx = fastgit_hash_ctx_new(FASTGIT_HASH_SHA256);
        if (!hctx) return FASTGIT_ENOMEM;
        fastgit_hash_ctx_init(hctx);
        fastgit_hash_ctx_update(hctx, hdr, (size_t)hdr_len);
        if (len > 0 && data) fastgit_hash_ctx_update(hctx, data, len);
        fastgit_error_t he = fastgit_hash_ctx_final(hctx, &h);
        fastgit_hash_ctx_free(hctx);
        if (he != FASTGIT_OK) return he;
    }
    fastgit_oid_t oid;
    oid.algo = FASTGIT_HASH_SHA256;
    oid.len = h.len;
    memcpy(oid.hash, h.digest, h.len);

    if (index->path) {
        char odb_path[4096];
        if (odb_path_from_index(index, odb_path, sizeof(odb_path)) == FASTGIT_OK) {
            fastgit_odb_t* odb = NULL;
            if (fastgit_odb_open(odb_path, &odb) == FASTGIT_OK) {
                fastgit_oid_t woid;
                fastgit_odb_write(odb, FASTGIT_OBJ_BLOB, data ? data : "", len, &woid);
                fastgit_odb_free(odb);
            }
        }
    }

    for (size_t i = 0; i < index->count; i++) {
        if (index->entries[i].stage == FASTGIT_INDEX_STAGE_NORMAL && strcmp(index->entries[i].path, path) == 0) {
            index->entries[i].oid = oid;
            index->entries[i].mode = mode;
            index->entries[i].size = (uint32_t)len;
            index->entries[i].flags = (uint16_t)strlen(path);
            index->dirty = true;
            return FASTGIT_OK;
        }
    }

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

fastgit_error_t fastgit_index_conflict_add(fastgit_index_t* index, const char* path,
    const fastgit_oid_t* ancestor, const fastgit_oid_t* ours, const fastgit_oid_t* theirs) {
    if (!index || !path) return FASTGIT_EINVAL;
    if (!ancestor && !ours && !theirs) return FASTGIT_EINVAL;
    // remove any existing entries for this path (all stages)
    fastgit_index_conflict_remove(index, path);
    // helper to add one stage
    struct { const fastgit_oid_t* oid; uint32_t stage; } stages[3] = {
        { ancestor, FASTGIT_INDEX_STAGE_BASE },
        { ours,     FASTGIT_INDEX_STAGE_OURS },
        { theirs,   FASTGIT_INDEX_STAGE_THEIRS },
    };
    for (int s = 0; s < 3; s++) {
        if (!stages[s].oid) continue;
        if (index->count >= index->capacity) {
            size_t nc = index->capacity ? index->capacity * 2 : 16;
            fastgit_index_entry_t* ne = realloc(index->entries, nc * sizeof(*ne));
            if (!ne) return FASTGIT_ENOMEM;
            index->entries = ne;
            index->capacity = nc;
        }
        fastgit_index_entry_t* e = &index->entries[index->count++];
        memset(e, 0, sizeof(*e));
        e->oid = *stages[s].oid;
        e->path = strdup(path);
        if (!e->path) { index->count--; return FASTGIT_ENOMEM; }
        e->mode = 0100644;
        e->stage = stages[s].stage;
        e->flags = (uint16_t)(strlen(path) & 0xFFF) | (stages[s].stage << 12);
    }
    index->dirty = true;
    index->sorted = false;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_index_conflict_remove(fastgit_index_t* index, const char* path) {
    if (!index || !path) return FASTGIT_EINVAL;
    size_t w = 0;
    for (size_t r = 0; r < index->count; r++) {
        if (strcmp(index->entries[r].path, path) == 0) {
            free(index->entries[r].path);
            continue;
        }
        if (w != r) index->entries[w] = index->entries[r];
        w++;
    }
    bool removed = (w != index->count);
    index->count = w;
    if (removed) { index->dirty = true; }
    return removed ? FASTGIT_OK : FASTGIT_ENOENT;
}

bool fastgit_index_has_conflicts(fastgit_index_t* index) {
    if (!index) return false;
    for (size_t i = 0; i < index->count; i++) {
        if (index->entries[i].stage != FASTGIT_INDEX_STAGE_NORMAL) return true;
    }
    return false;
}

// helpers for write_tree / read_tree
static fastgit_error_t odb_path_from_index(const fastgit_index_t* idx, char* out, size_t outsz) {
    if (!idx || !idx->path || !out) return FASTGIT_EINVAL;
    const char* p = idx->path;
    size_t len = strlen(p);
    // strip /index suffix
    const char* suffix = "/index";
    size_t sl = strlen(suffix);
    if (len < sl || strcmp(p + len - sl, suffix) != 0) return FASTGIT_EINVAL;
    size_t gitdir_len = len - sl;
    if (gitdir_len + 9 >= outsz) return FASTGIT_EINVAL;
    memcpy(out, p, gitdir_len);
    out[gitdir_len] = '\0';
    // gitdir is e.g. /repo/.git  -> objects at /repo/.git/objects
    if (gitdir_len + 9 < outsz) {
        // ensure no double slash
        snprintf(out, outsz, "%.*s/objects", (int)gitdir_len, p);
    }
    return FASTGIT_OK;
}

static fastgit_error_t write_tree_recursive(fastgit_odb_t* odb, fastgit_index_entry_t* entries, size_t count,
                                            const char* prefix, size_t prefix_len, fastgit_oid_t* out_oid);

static fastgit_error_t write_tree_recursive(fastgit_odb_t* odb, fastgit_index_entry_t* entries, size_t count,
                                            const char* prefix, size_t prefix_len, fastgit_oid_t* out_oid) {
    // collect direct files and subdirs under prefix
    // prefix is e.g. "a/b/" or "" for root
    // entries are sorted by path
    fastgit_object_t* tree_obj = NULL;
    fastgit_error_t err = fastgit_tree_create(&tree_obj);
    if (err != FASTGIT_OK) return err;

    // group by next component
    // we need to deduplicate subdirs
    // first pass: handle files directly under prefix and collect unique subdir names
    // use temporary list of subdir names
    char** subdirs = NULL;
    size_t subdir_count = 0, subdir_cap = 0;

    for (size_t i = 0; i < count; i++) {
        const char* path = entries[i].path;
        if (entries[i].stage != FASTGIT_INDEX_STAGE_NORMAL) continue;
        if (prefix_len > 0) {
            if (strncmp(path, prefix, prefix_len) != 0) continue;
            path += prefix_len;
        }
        const char* slash = strchr(path, '/');
        if (!slash) {
            // direct file
            fastgit_tree_entry_t te;
            te.path = (char*)path;
            te.oid = entries[i].oid;
            te.mode = entries[i].mode;
            // tree_add_entry sorts internally; pass copy
            // avoid qsort per insert overhead for bulk - still ok for now
            fastgit_error_t ae = fastgit_tree_add_entry(tree_obj, &te);
            if (ae != FASTGIT_OK) { fastgit_object_free(tree_obj); free(subdirs); return ae; }
        } else {
            size_t dlen = (size_t)(slash - path);
            bool known = false;
            for (size_t k = 0; k < subdir_count; k++) {
                if (strlen(subdirs[k]) == dlen && strncmp(subdirs[k], path, dlen) == 0) { known = true; break; }
            }
            if (!known) {
                if (subdir_count >= subdir_cap) {
                    size_t nc = subdir_cap ? subdir_cap * 2 : 8;
                    char** ne = realloc(subdirs, nc * sizeof(char*));
                    if (!ne) { fastgit_object_free(tree_obj); free(subdirs); return FASTGIT_ENOMEM; }
                    subdirs = ne; subdir_cap = nc;
                }
                char* name = malloc(dlen + 1);
                if (!name) { fastgit_object_free(tree_obj); for(size_t k=0;k<subdir_count;k++) free(subdirs[k]); free(subdirs); return FASTGIT_ENOMEM; }
                memcpy(name, path, dlen); name[dlen] = '\0';
                subdirs[subdir_count++] = name;
            }
        }
    }

    // for each subdir recurse
    for (size_t s = 0; s < subdir_count; s++) {
        char* name = subdirs[s];
        // build new prefix
        size_t new_prefix_len = prefix_len + strlen(name) + 1;
        char* new_prefix = malloc(new_prefix_len + 1);
        if (!new_prefix) { fastgit_object_free(tree_obj); for(size_t k=s;k<subdir_count;k++) free(subdirs[k]); free(subdirs); return FASTGIT_ENOMEM; }
        if (prefix_len > 0) memcpy(new_prefix, prefix, prefix_len);
        memcpy(new_prefix + prefix_len, name, strlen(name));
        new_prefix[prefix_len + strlen(name)] = '/';
        new_prefix[new_prefix_len] = '\0';

        fastgit_oid_t sub_oid;
        fastgit_error_t re = write_tree_recursive(odb, entries, count, new_prefix, new_prefix_len, &sub_oid);
        free(new_prefix);
        if (re != FASTGIT_OK) { fastgit_object_free(tree_obj); for(size_t k=s+1;k<subdir_count;k++) free(subdirs[k]); free(name); free(subdirs); return re; }

        fastgit_tree_entry_t te;
        te.path = name;
        te.oid = sub_oid;
        te.mode = 040000;
        fastgit_error_t ae = fastgit_tree_add_entry(tree_obj, &te);
        if (ae != FASTGIT_OK) { fastgit_object_free(tree_obj); for(size_t k=s+1;k<subdir_count;k++) free(subdirs[k]); free(name); free(subdirs); return ae; }
        free(name);
    }
    free(subdirs);

    // serialize tree content via public iterator (avoid private struct)
    size_t content_len = 0;
    size_t nentries = fastgit_tree_entry_count(tree_obj);
    for (size_t ei = 0; ei < nentries; ei++) {
        const fastgit_tree_entry_t* te = fastgit_tree_entry_by_index(tree_obj, ei);
        char mode_str[16];
        int ml = snprintf(mode_str, sizeof(mode_str), "%o", te->mode);
        content_len += (size_t)ml + 1 + strlen(te->path) + 1 + te->oid.len;
    }
    uint8_t* content = malloc(content_len ? content_len : 1);
    if (!content) { fastgit_object_free(tree_obj); return FASTGIT_ENOMEM; }
    size_t pos = 0;
    for (size_t ei = 0; ei < nentries; ei++) {
        const fastgit_tree_entry_t* te = fastgit_tree_entry_by_index(tree_obj, ei);
        char mode_str[16];
        int ml = snprintf(mode_str, sizeof(mode_str), "%o", te->mode);
        memcpy(content + pos, mode_str, (size_t)ml); pos += (size_t)ml;
        content[pos++] = ' ';
        size_t pl = strlen(te->path);
        memcpy(content + pos, te->path, pl); pos += pl;
        content[pos++] = '\0';
        memcpy(content + pos, te->oid.hash, te->oid.len); pos += te->oid.len;
    }
    // write via odb
    fastgit_oid_t oid;
    fastgit_error_t we = fastgit_odb_write(odb, FASTGIT_OBJ_TREE, content, content_len, &oid);
    free(content);
    fastgit_object_free(tree_obj);
    if (we != FASTGIT_OK) return we;
    if (out_oid) *out_oid = oid;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_index_read_tree(fastgit_index_t* index, const fastgit_oid_t* tree_oid) {
    if (!index || !tree_oid) return FASTGIT_EINVAL;
    char odb_path[4096];
    fastgit_error_t err = odb_path_from_index(index, odb_path, sizeof(odb_path));
    if (err != FASTGIT_OK) return err;
    fastgit_odb_t* odb = NULL;
    err = fastgit_odb_open(odb_path, &odb);
    if (err != FASTGIT_OK) return err;

    // clear existing
    fastgit_index_clear(index);

    // iterative stack for tree expansion
    typedef struct { fastgit_oid_t oid; char* prefix; } stack_item_t;
    stack_item_t* stack = malloc(sizeof(stack_item_t));
    if (!stack) { fastgit_odb_free(odb); return FASTGIT_ENOMEM; }
    stack[0].oid = *tree_oid;
    stack[0].prefix = strdup("");
    size_t stack_size = 1, stack_cap = 1;
    if (!stack[0].prefix) { free(stack); fastgit_odb_free(odb); return FASTGIT_ENOMEM; }

    while (stack_size > 0) {
        stack_item_t cur = stack[--stack_size];
        fastgit_odb_object_t obj;
        err = fastgit_odb_read(odb, &cur.oid, &obj);
        if (err != FASTGIT_OK) { free(cur.prefix); free(stack); fastgit_odb_free(odb); return err; }
        if (obj.type != FASTGIT_OBJ_TREE) { free(obj.data); free(cur.prefix); free(stack); fastgit_odb_free(odb); return FASTGIT_EINVAL; }
        uint8_t* p = (uint8_t*)obj.data;
        uint8_t* end = p + obj.size;
        while (p < end) {
            char* space = memchr(p, ' ', (size_t)(end - p));
            if (!space) break;
            // mode
            char mode_str[16]; size_t mlen = (size_t)(space - (char*)p);
            if (mlen >= sizeof(mode_str)) { free(obj.data); free(cur.prefix); free(stack); fastgit_odb_free(odb); return FASTGIT_ERROR; }
            memcpy(mode_str, p, mlen); mode_str[mlen] = '\0';
            uint32_t mode = (uint32_t)strtoul(mode_str, NULL, 8);
            p = (uint8_t*)space + 1;
            char* nul = memchr(p, '\0', (size_t)(end - p));
            if (!nul) break;
            size_t namelen = (size_t)(nul - (char*)p);
            char* name = malloc(namelen + 1);
            if (!name) { free(obj.data); free(cur.prefix); free(stack); fastgit_odb_free(odb); return FASTGIT_ENOMEM; }
            memcpy(name, p, namelen); name[namelen] = '\0';
            p = (uint8_t*)nul + 1;
            if ((size_t)(end - p) < 32) { free(name); break; }
            fastgit_oid_t entry_oid;
            entry_oid.algo = FASTGIT_HASH_SHA256;
            entry_oid.len = 32;
            memcpy(entry_oid.hash, p, 32);
            p += 32;

            // build full path
            size_t full_len = strlen(cur.prefix) + namelen + 1;
            char* full = malloc(full_len);
            if (!full) { free(name); free(obj.data); free(cur.prefix); free(stack); fastgit_odb_free(odb); return FASTGIT_ENOMEM; }
            strcpy(full, cur.prefix);
            strcat(full, name);
            free(name);

            if (mode == 040000) {
                // push sub-tree onto stack
                if (stack_size >= stack_cap) {
                    size_t nc = stack_cap * 2;
                    stack_item_t* ne = realloc(stack, nc * sizeof(*ne));
                    if (!ne) { free(full); free(obj.data); free(cur.prefix); free(stack); fastgit_odb_free(odb); return FASTGIT_ENOMEM; }
                    stack = ne; stack_cap = nc;
                }
                size_t flen = strlen(full);
                char* new_prefix = malloc(flen + 2);
                if (!new_prefix) { free(full); free(obj.data); free(cur.prefix); free(stack); fastgit_odb_free(odb); return FASTGIT_ENOMEM; }
                memcpy(new_prefix, full, flen);
                new_prefix[flen] = '/';
                new_prefix[flen+1] = '\0';
                free(full);
                stack[stack_size].oid = entry_oid;
                stack[stack_size].prefix = new_prefix;
                stack_size++;
            } else {
                // add index entry
                if (index->count >= index->capacity) {
                    size_t nc = index->capacity ? index->capacity * 2 : 64;
                    fastgit_index_entry_t* ne = realloc(index->entries, nc * sizeof(*ne));
                    if (!ne) { free(full); free(obj.data); free(cur.prefix); free(stack); fastgit_odb_free(odb); return FASTGIT_ENOMEM; }
                    index->entries = ne;
                    index->capacity = nc;
                }
                fastgit_index_entry_t* e = &index->entries[index->count++];
                memset(e, 0, sizeof(*e));
                e->oid = entry_oid;
                e->path = full;
                e->mode = mode;
                e->stage = FASTGIT_INDEX_STAGE_NORMAL;
                e->flags = (uint16_t)(strlen(full) & 0xFFF);
                // stat fields left 0; will be refreshed on checkout/status
            }
        }
        free(obj.data);
        free(cur.prefix);
    }
    free(stack);
    fastgit_odb_free(odb);
    index->dirty = true;
    index->sorted = false;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_index_write_tree(fastgit_index_t* index, fastgit_oid_t* out) {
    if (!index || !out) return FASTGIT_EINVAL;
    if (fastgit_index_has_conflicts(index)) return FASTGIT_EINVAL;
    if (index->count == 0) {
        // empty tree: write empty tree object
        char odb_path[4096];
        fastgit_error_t err = odb_path_from_index(index, odb_path, sizeof(odb_path));
        if (err != FASTGIT_OK) return err;
        fastgit_odb_t* odb = NULL;
        err = fastgit_odb_open(odb_path, &odb);
        if (err != FASTGIT_OK) return err;
        fastgit_oid_t oid;
        err = fastgit_odb_write(odb, FASTGIT_OBJ_TREE, "", 0, &oid);
        fastgit_odb_free(odb);
        if (err == FASTGIT_OK) *out = oid;
        return err;
    }
    // ensure sorted for deterministic write
    if (!index->sorted && index->count > 1) {
        qsort(index->entries, index->count, sizeof(fastgit_index_entry_t), index_entry_cmp);
        index->sorted = true;
    }
    char odb_path[4096];
    fastgit_error_t err = odb_path_from_index(index, odb_path, sizeof(odb_path));
    if (err != FASTGIT_OK) return err;
    fastgit_odb_t* odb = NULL;
    err = fastgit_odb_open(odb_path, &odb);
    if (err != FASTGIT_OK) return err;
    err = write_tree_recursive(odb, index->entries, index->count, "", 0, out);
    fastgit_odb_free(odb);
    return err;
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
