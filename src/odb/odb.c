#include "fastgit/odb.h"
#include "fastgit/object.h"
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
#define snprintf _snprintf
#else
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>
#endif

#define FASTGIT_ODB_LOOSE_DIR_MODE 0755
#define FASTGIT_ODB_COMPRESSION_LEVEL 1
#define FASTGIT_ODB_CACHE_BITS 14
#define FASTGIT_ODB_CACHE_SIZE (1u << FASTGIT_ODB_CACHE_BITS)
#define FASTGIT_ODB_CACHE_MASK (FASTGIT_ODB_CACHE_SIZE - 1)
static size_t obj_size_hint(size_t csize) { return csize * 4 + 128; }

typedef struct {
    fastgit_oid_t oid;
    fastgit_obj_type_t type;
    void* data;
    size_t size;
    uint64_t gen;
    bool occupied;
} odb_cache_entry_t;

struct fastgit_odb {
    char* path;
    uint8_t default_hash_algo;
    fastgit_odb_t** backends;
    size_t backend_count;
    size_t backend_capacity;

    uint64_t stats_reads;
    uint64_t stats_writes;
    uint64_t stats_cache_hits;
    uint64_t stats_cache_misses;

    odb_cache_entry_t* cache;
    uint64_t cache_gen;
    bool algo_dir_ready;
};

static char* odb_object_path(fastgit_odb_t* odb, const fastgit_oid_t* oid) {
    char hex[129];
    fastgit_oid_to_hex(oid, hex, sizeof(hex));
    // git-compatible loose layout: $GITDIR/objects/ab/cdef... (no algo prefix, no double objects)
    size_t need = (size_t)snprintf(NULL, 0, "%s/%.2s/%s", odb->path, hex, hex + 2) + 1;
    char* path = malloc(need);
    if (!path) return NULL;
    snprintf(path, need, "%s/%.2s/%s", odb->path, hex, hex + 2);
    return path;
}

static fastgit_error_t odb_ensure_dir(fastgit_odb_t* odb, const fastgit_oid_t* oid) {
    char hex[129];
    fastgit_oid_to_hex(oid, hex, sizeof(hex));
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/%.2s", odb->path, hex);
#if defined(_WIN32)
    CreateDirectoryA(dir_path, NULL);
    return FASTGIT_OK;
#else
    if (mkdir(dir_path, FASTGIT_ODB_LOOSE_DIR_MODE) == 0 || errno == EEXIST) return FASTGIT_OK;
    return FASTGIT_EIO;
#endif
}

static fastgit_error_t odb_compress(const void* data, size_t len, void** out, size_t* out_len) {
#if defined(FASTGIT_HAVE_ZSTD) && FASTGIT_HAVE_ZSTD
    size_t bound = ZSTD_compressBound(len);
    void* compressed = malloc(bound);
    if (!compressed) return FASTGIT_ENOMEM;

    size_t compressed_size = ZSTD_compress(compressed, bound, data, len, FASTGIT_ODB_COMPRESSION_LEVEL);
    if (ZSTD_isError(compressed_size)) {
        free(compressed);
        return FASTGIT_ERROR;
    }
    *out = compressed;
    *out_len = compressed_size;
    return FASTGIT_OK;
#else
    uLongf bound = compressBound(len);
    void* compressed = malloc(bound);
    if (!compressed) return FASTGIT_ENOMEM;

    int ret = compress2(compressed, &bound, data, len, FASTGIT_ODB_COMPRESSION_LEVEL);
    if (ret != Z_OK) {
        free(compressed);
        return FASTGIT_ERROR;
    }
    *out = compressed;
    *out_len = bound;
    return FASTGIT_OK;
#endif
}

static __attribute__((unused)) fastgit_error_t odb_decompress(const void* data, size_t len, void** out, size_t out_len) {
#if defined(FASTGIT_HAVE_ZSTD) && FASTGIT_HAVE_ZSTD
    void* decompressed = malloc(out_len);
    if (!decompressed) return FASTGIT_ENOMEM;

    size_t ret = ZSTD_decompress(decompressed, out_len, data, len);
    if (ZSTD_isError(ret)) {
        free(decompressed);
        return FASTGIT_ERROR;
    }
    *out = decompressed;
    return FASTGIT_OK;
#else
    void* decompressed = malloc(out_len);
    if (!decompressed) return FASTGIT_ENOMEM;

    uLongf dest_len = out_len;
    int ret = uncompress(decompressed, &dest_len, data, len);
    if (ret != Z_OK) {
        free(decompressed);
        return FASTGIT_ERROR;
    }
    *out = decompressed;
    return FASTGIT_OK;
#endif
}

static inline uint32_t odb_hash_oid(const fastgit_oid_t* oid) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < oid->len && i < 8; i++) { h ^= oid->hash[i]; h *= 16777619u; }
    h ^= oid->algo; h *= 16777619u;
    return h;
}

fastgit_error_t fastgit_odb_new(const char* path, fastgit_odb_t** out) {
    if (!path || !out) return FASTGIT_EINVAL;

    fastgit_odb_t* odb = calloc(1, sizeof(fastgit_odb_t));
    if (!odb) return FASTGIT_ENOMEM;

    odb->path = strdup(path);
    if (!odb->path) {
        free(odb);
        return FASTGIT_ENOMEM;
    }

    odb->default_hash_algo = FASTGIT_HASH_SHA256;
    odb->backend_capacity = 4;
    odb->backends = calloc(odb->backend_capacity, sizeof(fastgit_odb_t*));
    if (!odb->backends) {
        free(odb->path);
        free(odb);
        return FASTGIT_ENOMEM;
    }
    odb->cache = calloc(FASTGIT_ODB_CACHE_SIZE, sizeof(odb_cache_entry_t));
    if (!odb->cache) {
        free(odb->backends); free(odb->path); free(odb);
        return FASTGIT_ENOMEM;
    }
    odb->cache_gen = 1;
    odb->algo_dir_ready = false;

#if defined(_WIN32)
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, FASTGIT_ODB_LOOSE_DIR_MODE);
#endif

    *out = odb;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_odb_open(const char* path, fastgit_odb_t** out) {
    return fastgit_odb_new(path, out);
}

void fastgit_odb_free(fastgit_odb_t* odb) {
    if (!odb) return;
    if (odb->cache) {
        for (size_t i = 0; i < FASTGIT_ODB_CACHE_SIZE; i++) if (odb->cache[i].occupied) free(odb->cache[i].data);
        free(odb->cache);
    }
    free(odb->path);
    free(odb->backends);
    free(odb);
}

fastgit_error_t fastgit_odb_read(fastgit_odb_t* odb, const fastgit_oid_t* oid, fastgit_odb_object_t* out) {
    if (!odb || !oid || !out) return FASTGIT_EINVAL;
    odb->stats_reads++;
    if (odb->cache) {
        uint32_t h = odb_hash_oid(oid) & FASTGIT_ODB_CACHE_MASK;
        for (size_t probe = 0; probe < 8; probe++) {
            size_t idx = (h + probe) & FASTGIT_ODB_CACHE_MASK;
            odb_cache_entry_t* e = &odb->cache[idx];
            if (!e->occupied) break;
            if (e->oid.len == oid->len && e->oid.algo == oid->algo && memcmp(e->oid.hash, oid->hash, oid->len) == 0) {
                void* cpy = malloc(e->size ? e->size : 1);
                if (!cpy) return FASTGIT_ENOMEM;
                if (e->size) memcpy(cpy, e->data, e->size);
                out->oid = *oid; out->type = e->type; out->size = e->size; out->data = cpy; out->source = FASTGIT_ODB_LOOSE;
                odb->stats_cache_hits++; return FASTGIT_OK;
            }
        }
    }
    for (size_t i = 0; i < odb->backend_count; i++) {
        fastgit_error_t err = fastgit_odb_read(odb->backends[i], oid, out);
        if (err == FASTGIT_OK) { odb->stats_cache_hits++; return FASTGIT_OK; }
    }

    char* path = odb_object_path(odb, oid);
    if (!path) return FASTGIT_ENOMEM;

#if defined(_WIN32)
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        free(path);
        return FASTGIT_ENOENT;
    }

    LARGE_INTEGER size;
    GetFileSizeEx(hFile, &size);
    size_t file_size = (size_t)size.QuadPart;

    void* compressed = malloc(file_size);
    if (!compressed) {
        CloseHandle(hFile);
        free(path);
        return FASTGIT_ENOMEM;
    }

    DWORD bytes_read;
    if (!ReadFile(hFile, compressed, (DWORD)file_size, &bytes_read, NULL)) {
        free(compressed);
        CloseHandle(hFile);
        free(path);
        return FASTGIT_EIO;
    }
    CloseHandle(hFile);
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        free(path);
        return FASTGIT_ENOENT;
    }

    struct stat st;
    fstat(fd, &st);
    size_t file_size = st.st_size;

    void* compressed = malloc(file_size);
    if (!compressed) {
        close(fd);
        free(path);
        return FASTGIT_ENOMEM;
    }

    ssize_t n = read(fd, compressed, file_size);
    close(fd);

    if (n != (ssize_t)file_size) {
        free(compressed);
        free(path);
        return FASTGIT_EIO;
    }
#endif

    size_t guess = obj_size_hint(file_size);
    void* decompressed = NULL;
    size_t decompressed_len = guess;
    fastgit_error_t derr = FASTGIT_ERROR;
    for (int attempt = 0; attempt < 8; attempt++) {
        decompressed = malloc(decompressed_len);
        if (!decompressed) { free(compressed); free(path); return FASTGIT_ENOMEM; }
        uLongf dl = (uLongf)decompressed_len;
        int zr = uncompress(decompressed, &dl, compressed, file_size);
        if (zr == Z_OK) { decompressed_len = dl; derr = FASTGIT_OK; break; }
        free(decompressed); decompressed = NULL;
        if (zr == Z_BUF_ERROR) { decompressed_len *= 4; continue; }
        free(compressed); free(path); return FASTGIT_ERROR;
    }
    free(compressed);
    if (derr != FASTGIT_OK || !decompressed) { free(path); return FASTGIT_ERROR; }
    size_t header_end = 0;
    while (header_end < decompressed_len && ((uint8_t*)decompressed)[header_end] != '\0') header_end++;
    if (header_end >= decompressed_len) { free(decompressed); free(path); return FASTGIT_ERROR; }
    char* header_str = malloc(header_end + 1);
    if (!header_str) { free(decompressed); free(path); return FASTGIT_ENOMEM; }
    memcpy(header_str, decompressed, header_end);
    header_str[header_end] = '\0';
    char type_str[16];
    size_t obj_size;
    if (sscanf(header_str, "%15s %zu", type_str, &obj_size) != 2) { free(header_str); free(decompressed); free(path); return FASTGIT_ERROR; }
    free(header_str);
    fastgit_obj_type_t type = fastgit_obj_type_from_name(type_str);
    size_t payload_off = header_end + 1;
    if (payload_off + obj_size > decompressed_len) { free(decompressed); free(path); return FASTGIT_ERROR; }
    void* data = malloc(obj_size ? obj_size : 1);
    if (!data) { free(decompressed); free(path); return FASTGIT_ENOMEM; }
    if (obj_size) memcpy(data, (uint8_t*)decompressed + payload_off, obj_size);
    free(decompressed);
    free(path);
    (void)derr;

    out->oid = *oid;
    out->type = type;
    out->size = obj_size;
    out->data = data;
    out->source = FASTGIT_ODB_LOOSE;
    odb->stats_cache_misses++;
    if (odb->cache) {
        uint32_t h = odb_hash_oid(oid) & FASTGIT_ODB_CACHE_MASK;
        for (size_t probe = 0; probe < 8; probe++) {
            size_t idx = (h + probe) & FASTGIT_ODB_CACHE_MASK;
            odb_cache_entry_t* e = &odb->cache[idx];
            if (!e->occupied) {
                void* cpy = malloc(obj_size ? obj_size : 1);
                if (cpy) { if (obj_size) memcpy(cpy, data, obj_size); e->oid=*oid; e->type=type; e->size=obj_size; e->data=cpy; e->gen=odb->cache_gen++; e->occupied=true; }
                break;
            }
            if (e->oid.len==oid->len && e->oid.algo==oid->algo && memcmp(e->oid.hash,oid->hash,oid->len)==0) break;
        }
    }
    return FASTGIT_OK;
}

fastgit_error_t fastgit_odb_read_header(fastgit_odb_t* odb, const fastgit_oid_t* oid, fastgit_obj_type_t* type, size_t* size) {
    fastgit_odb_object_t obj;
    fastgit_error_t err = fastgit_odb_read(odb, oid, &obj);
    if (err != FASTGIT_OK) return err;
    if (type) *type = obj.type;
    if (size) *size = obj.size;
    free(obj.data);
    return FASTGIT_OK;
}

fastgit_error_t fastgit_odb_write(fastgit_odb_t* odb, fastgit_obj_type_t type, const void* data, size_t len, fastgit_oid_t* out) {
    if (!odb || !data || !out) return FASTGIT_EINVAL;
    odb->stats_writes++;
    fastgit_object_t temp_obj;
    temp_obj.type = type; temp_obj.size = len; temp_obj.data = (void*)data; temp_obj.free_data = NULL;
    fastgit_error_t err = fastgit_object_hash(&temp_obj, odb->default_hash_algo, out);
    if (err != FASTGIT_OK) return err;
    if (odb->cache) {
        uint32_t h = odb_hash_oid(out) & FASTGIT_ODB_CACHE_MASK;
        for (size_t probe = 0; probe < 8; probe++) {
            size_t idx = (h + probe) & FASTGIT_ODB_CACHE_MASK;
            odb_cache_entry_t* e = &odb->cache[idx];
            if (e->occupied && e->oid.len==out->len && e->oid.algo==out->algo && memcmp(e->oid.hash,out->hash,out->len)==0) {
                odb->stats_cache_hits++; return FASTGIT_OK;
            }
            if (!e->occupied) break;
        }
    }

    fastgit_buf_t buf;
    err = fastgit_object_serialize(&temp_obj, odb->default_hash_algo, &buf);
    if (err != FASTGIT_OK) return err;

    void* compressed;
    size_t compressed_len;
    err = odb_compress(buf.data, buf.len, &compressed, &compressed_len);
    free(buf.data);
    if (err != FASTGIT_OK) return err;

    err = odb_ensure_dir(odb, out);
    if (err != FASTGIT_OK) {
        free(compressed);
        return err;
    }

    char* path = odb_object_path(odb, out);
    if (!path) {
        free(compressed);
        return FASTGIT_ENOMEM;
    }

#if defined(_WIN32)
    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        free(compressed);
        free(path);
        return FASTGIT_EIO;
    }
    DWORD bytes_written;
    if (!WriteFile(hFile, compressed, (DWORD)compressed_len, &bytes_written, NULL) || bytes_written != compressed_len) {
        free(compressed);
        CloseHandle(hFile);
        free(path);
        return FASTGIT_EIO;
    }
    CloseHandle(hFile);
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(compressed);
        free(path);
        return FASTGIT_EIO;
    }
    ssize_t n = write(fd, compressed, compressed_len);
    close(fd);
    if (n != (ssize_t)compressed_len) {
        free(compressed);
        free(path);
        return FASTGIT_EIO;
    }
#endif

    free(compressed);
    free(path);
    if (odb->cache) {
        uint32_t h = odb_hash_oid(out) & FASTGIT_ODB_CACHE_MASK;
        for (size_t probe = 0; probe < 8; probe++) {
            size_t idx = (h + probe) & FASTGIT_ODB_CACHE_MASK;
            odb_cache_entry_t* e = &odb->cache[idx];
            if (!e->occupied) {
                void* cpy = malloc(len ? len : 1);
                if (cpy) { if (len) memcpy(cpy, data, len); e->oid=*out; e->type=type; e->size=len; e->data=cpy; e->gen=odb->cache_gen++; e->occupied=true; }
                break;
            }
            if (e->oid.len==out->len && e->oid.algo==out->algo && memcmp(e->oid.hash,out->hash,out->len)==0) {
                free(e->data); void* cpy=malloc(len?len:1); if(cpy){ if(len) memcpy(cpy,data,len); e->data=cpy; e->size=len; e->type=type; e->gen=odb->cache_gen++; }
                break;
            }
        }
    }
    return FASTGIT_OK;
}

fastgit_error_t fastgit_odb_exists(fastgit_odb_t* odb, const fastgit_oid_t* oid) {
    if (!odb || !oid) return FASTGIT_EINVAL;

    for (size_t i = 0; i < odb->backend_count; i++) {
        if (fastgit_odb_exists(odb->backends[i], oid) == FASTGIT_OK) {
            return FASTGIT_OK;
        }
    }

    char* path = odb_object_path(odb, oid);
    if (!path) return FASTGIT_ENOMEM;

#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    free(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) ? FASTGIT_OK : FASTGIT_ENOENT;
#else
    int ret = access(path, F_OK);
    free(path);
    return (ret == 0) ? FASTGIT_OK : FASTGIT_ENOENT;
#endif
}

fastgit_error_t fastgit_odb_delete(fastgit_odb_t* odb, const fastgit_oid_t* oid) {
    if (!odb || !oid) return FASTGIT_EINVAL;

    char* path = odb_object_path(odb, oid);
    if (!path) return FASTGIT_ENOMEM;

#if defined(_WIN32)
    BOOL ret = DeleteFileA(path);
    free(path);
    return ret ? FASTGIT_OK : FASTGIT_EIO;
#else
    int ret = unlink(path);
    free(path);
    return (ret == 0) ? FASTGIT_OK : FASTGIT_EIO;
#endif
}

struct fastgit_odb_iterator {
    fastgit_odb_t* odb;
    fastgit_oid_t* oids;
    size_t count;
    size_t pos;
};

fastgit_error_t fastgit_odb_iterator_new(fastgit_odb_t* odb, fastgit_odb_iterator_t** out) {
    if (!odb || !out) return FASTGIT_EINVAL;
    fastgit_odb_iterator_t* iter = calloc(1, sizeof(*iter));
    if (!iter) return FASTGIT_ENOMEM;
    iter->odb = odb;
    // git-compatible 2-level layout: objects/ab/cdef...
    char objects_dir[1024];
    snprintf(objects_dir, sizeof(objects_dir), "%s", odb->path);
    DIR* d1 = opendir(objects_dir);
    if (!d1) { iter->oids=NULL; iter->count=0; *out=iter; return FASTGIT_OK; }
    size_t cap=64; iter->oids=malloc(cap*sizeof(fastgit_oid_t));
    if (!iter->oids) { closedir(d1); free(iter); return FASTGIT_ENOMEM; }
    struct dirent* e1;
    while ((e1=readdir(d1))!=NULL) {
        if (e1->d_name[0]=='.') continue;
        if (strlen(e1->d_name)!=2) continue; // git loose Fan-out is 2 hex chars
        char p1[1024]; snprintf(p1,sizeof(p1),"%s/%s",odb->path,e1->d_name);
        DIR* d2=opendir(p1); if(!d2) continue;
        struct dirent* e2; while((e2=readdir(d2))!=NULL){
            if(e2->d_name[0]=='.') continue;
            char full[256]; snprintf(full,sizeof(full),"%s%s",e1->d_name,e2->d_name);
            fastgit_oid_t oid; if(fastgit_oid_from_hex(full,&oid)!=FASTGIT_OK) continue;
            if(iter->count>=cap){ cap*=2; fastgit_oid_t* n=realloc(iter->oids,cap*sizeof(*n)); if(!n) break; iter->oids=n; }
            iter->oids[iter->count++]=oid;
        }
        closedir(d2);
    }
    closedir(d1);
    *out=iter; return FASTGIT_OK;
}

fastgit_error_t fastgit_odb_iterator_next(fastgit_odb_iterator_t* iter, fastgit_oid_t* out) {
    if (!iter || !out) return FASTGIT_EINVAL;
    if (iter->pos >= iter->count) return FASTGIT_ENOENT;
    *out = iter->oids[iter->pos++];
    return FASTGIT_OK;
}

void fastgit_odb_iterator_free(fastgit_odb_iterator_t* iter) {
    if (!iter) return;
    free(iter->oids);
    free(iter);
}

fastgit_error_t fastgit_odb_add_backend(fastgit_odb_t* odb, fastgit_odb_t* backend, int priority) {
    if (!odb || !backend) return FASTGIT_EINVAL;

    if (odb->backend_count >= odb->backend_capacity) {
        size_t new_cap = odb->backend_capacity * 2;
        fastgit_odb_t** new_backends = realloc(odb->backends, new_cap * sizeof(fastgit_odb_t*));
        if (!new_backends) return FASTGIT_ENOMEM;
        odb->backends = new_backends;
        odb->backend_capacity = new_cap;
    }

    if (priority >= 0 && (size_t)priority < odb->backend_count) {
        memmove(&odb->backends[priority + 1], &odb->backends[priority],
                (odb->backend_count - priority) * sizeof(fastgit_odb_t*));
        odb->backends[priority] = backend;
    } else {
        odb->backends[odb->backend_count] = backend;
    }
    odb->backend_count++;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_odb_read_prefix(fastgit_odb_t* odb, const uint8_t* prefix, size_t prefix_len, fastgit_odb_object_t* out) {
    if (!odb || !prefix || !out) return FASTGIT_EINVAL;

    fastgit_odb_iterator_t* iter;
    fastgit_error_t err = fastgit_odb_iterator_new(odb, &iter);
    if (err != FASTGIT_OK) return err;

    fastgit_oid_t oid;
    while (fastgit_odb_iterator_next(iter, &oid) == FASTGIT_OK) {
        if (oid.len >= prefix_len && memcmp(oid.hash, prefix, prefix_len) == 0) {
            err = fastgit_odb_read(odb, &oid, out);
            fastgit_odb_iterator_free(iter);
            return err;
        }
    }

    fastgit_odb_iterator_free(iter);
    return FASTGIT_ENOENT;
}

void fastgit_odb_stats(fastgit_odb_t* odb, fastgit_odb_stats_t* out) {
    if (!odb || !out) return;
    out->loose_count = 0;
    out->pack_count = 0;
    out->loose_size = 0;
    out->pack_size = 0;
    out->reads = odb->stats_reads;
    out->writes = odb->stats_writes;
    out->cache_hits = odb->stats_cache_hits;
    out->cache_misses = odb->stats_cache_misses;
}

void fastgit_odb_stats_reset(fastgit_odb_t* odb) {
    if (!odb) return;
    odb->stats_reads = 0;
    odb->stats_writes = 0;
    odb->stats_cache_hits = 0;
    odb->stats_cache_misses = 0;
}

fastgit_error_t fastgit_odb_pack(fastgit_odb_t* odb __attribute__((unused)), const fastgit_oid_t* objects __attribute__((unused)), size_t count __attribute__((unused))) {
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_odb_gc(fastgit_odb_t* odb __attribute__((unused))) {
    return FASTGIT_EUNSUPPORTED;
}
