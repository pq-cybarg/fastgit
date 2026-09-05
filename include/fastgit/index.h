#ifndef FASTGIT_INDEX_H
#define FASTGIT_INDEX_H

#include "fastgit/object.h"
#include "fastgit/hash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FASTGIT_INDEX_VERSION 2
#define FASTGIT_INDEX_SIGNATURE 0x44495243

#pragma pack(push, 1)
typedef struct {
    uint32_t signature;
    uint32_t version;
    uint32_t count;
} fastgit_index_header_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint32_t ctime_sec;
    uint32_t ctime_nsec;
    uint32_t mtime_sec;
    uint32_t mtime_nsec;
    uint32_t dev;
    uint32_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint8_t oid[32];
    uint16_t flags;
} fastgit_index_entry_disk_t;
#pragma pack(pop)

typedef struct fastgit_index fastgit_index_t;

typedef struct {
    fastgit_oid_t oid;
    char* path;
    uint32_t mode;
    uint32_t stage;
    uint32_t ctime_sec;
    uint32_t ctime_nsec;
    uint32_t mtime_sec;
    uint32_t mtime_nsec;
    uint32_t dev;
    uint32_t ino;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint16_t flags;
    uint16_t flags_extended;
} fastgit_index_entry_t;

typedef struct {
    int (*stat)(const char* path, struct stat* st);
    int (*lstat)(const char* path, struct stat* st);
    int (*fstat)(int fd, struct stat* st);
    int (*open)(const char* path, int flags, ...);
    ssize_t (*read)(int fd, void* buf, size_t count);
    ssize_t (*write)(int fd, const void* buf, size_t count);
    int (*close)(int fd);
    int (*mkdir)(const char* path, mode_t mode);
    int (*unlink)(const char* path);
    int (*rename)(const char* oldpath, const char* newpath);
    DIR* (*opendir)(const char* path);
    struct dirent* (*readdir)(DIR* dir);
    int (*closedir)(DIR* dir);
} fastgit_index_vfs_t;

struct fastgit_index {
    char* path;
    fastgit_index_entry_t* entries;
    size_t count;
    size_t capacity;
    bool dirty;
    bool sorted;
    fastgit_index_vfs_t vfs;
    fastgit_hash_t checksum;
    uint64_t mtime;
};

typedef enum {
    FASTGIT_INDEX_STAGE_NORMAL = 0,
    FASTGIT_INDEX_STAGE_BASE = 1,
    FASTGIT_INDEX_STAGE_OURS = 2,
    FASTGIT_INDEX_STAGE_THEIRS = 3,
} fastgit_index_stage_t;

fastgit_error_t fastgit_index_new(fastgit_index_t** out);
fastgit_error_t fastgit_index_open(const char* path, fastgit_index_t** out);
void fastgit_index_free(fastgit_index_t* index);

fastgit_error_t fastgit_index_read(fastgit_index_t* index, const char* path);
fastgit_error_t fastgit_index_write(fastgit_index_t* index);
fastgit_error_t fastgit_index_write_to(fastgit_index_t* index, const char* path);

fastgit_error_t fastgit_index_add(fastgit_index_t* index, const char* path);
fastgit_error_t fastgit_index_add_many(fastgit_index_t* index, const char** paths, size_t count);
fastgit_error_t fastgit_index_add_from_buffer(fastgit_index_t* index, const char* path, uint32_t mode, const void* data, size_t len);
fastgit_error_t fastgit_index_remove(fastgit_index_t* index, const char* path, uint32_t stage);
fastgit_error_t fastgit_index_clear(fastgit_index_t* index);

fastgit_error_t fastgit_index_find(fastgit_index_t* index, const char* path, uint32_t stage, fastgit_index_entry_t** out);
fastgit_error_t fastgit_index_find_by_oid(fastgit_index_t* index, const fastgit_oid_t* oid, fastgit_index_entry_t** out);

size_t fastgit_index_entry_count(fastgit_index_t* index);
const fastgit_index_entry_t* fastgit_index_entry_by_index(fastgit_index_t* index, size_t n);

fastgit_error_t fastgit_index_conflict_add(fastgit_index_t* index, const char* path,
    const fastgit_oid_t* ancestor, const fastgit_oid_t* ours, const fastgit_oid_t* theirs);
fastgit_error_t fastgit_index_conflict_remove(fastgit_index_t* index, const char* path);
bool fastgit_index_has_conflicts(fastgit_index_t* index);

fastgit_error_t fastgit_index_read_tree(fastgit_index_t* index, const fastgit_oid_t* tree_oid);
fastgit_error_t fastgit_index_write_tree(fastgit_index_t* index, fastgit_oid_t* out);

typedef struct {
    uint64_t entries;
    uint64_t size;
    uint64_t mtime;
    fastgit_hash_t checksum;
    bool dirty;
} fastgit_index_stats_t;

void fastgit_index_stats(fastgit_index_t* index, fastgit_index_stats_t* out);

fastgit_error_t fastgit_index_set_vfs(fastgit_index_t* index, const fastgit_index_vfs_t* vfs);

#ifdef __cplusplus
}
#endif

#endif
