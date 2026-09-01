#include "fastgit/pack.h"
#include "fastgit/hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <zlib.h>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#define open _open
#define close _close
#define read _read
#define write _write
#define lseek _lseeki64
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_CREAT _O_CREAT
#define O_TRUNC _O_TRUNC
#define O_BINARY _O_BINARY
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#pragma pack(push, 1)
typedef struct {
    uint32_t signature;
    uint32_t version;
    uint32_t object_count;
} fastgit_pack_header_t;
#pragma pack(pop)

struct fastgit_pack {
    int fd;
    char* pack_file;
    char* idx_file;
    fastgit_pack_index_t* index;
    uint64_t file_size;
    bool writing;
    fastgit_pack_header_t header;

    uint64_t stats_objects_read;
    uint64_t stats_objects_written;
    uint64_t stats_bytes_read;
    uint64_t stats_bytes_written;
    uint64_t stats_deltas_created;
    uint64_t stats_deltas_applied;
};

static fastgit_obj_type_t pack_obj_type_from_git(uint32_t type) {
    switch (type) {
        case FASTGIT_PACK_OBJ_COMMIT: return FASTGIT_OBJ_COMMIT;
        case FASTGIT_PACK_OBJ_TREE: return FASTGIT_OBJ_TREE;
        case FASTGIT_PACK_OBJ_BLOB: return FASTGIT_OBJ_BLOB;
        case FASTGIT_PACK_OBJ_TAG: return FASTGIT_OBJ_TAG;
        case FASTGIT_PACK_OBJ_OFS_DELTA: return FASTGIT_OBJ_OFS_DELTA;
        case FASTGIT_PACK_OBJ_REF_DELTA: return FASTGIT_OBJ_REF_DELTA;
        default: return FASTGIT_OBJ_BLOB;
    }
}

fastgit_error_t fastgit_pack_open(const char* pack_file, const char* idx_file, fastgit_pack_t** out) {
    if (!pack_file || !out) return FASTGIT_EINVAL;

    fastgit_pack_t* pack = calloc(1, sizeof(fastgit_pack_t));
    if (!pack) return FASTGIT_ENOMEM;

    pack->pack_file = strdup(pack_file);
    if (!pack->pack_file) {
        free(pack);
        return FASTGIT_ENOMEM;
    }

    if (idx_file) {
        pack->idx_file = strdup(idx_file);
    } else {
        size_t len = strlen(pack_file);
        pack->idx_file = malloc(len + 5);
        if (!pack->idx_file) {
            free(pack->pack_file);
            free(pack);
            return FASTGIT_ENOMEM;
        }
        memcpy(pack->idx_file, pack_file, len);
        strcpy(pack->idx_file + len - 5, ".idx");
    }

#if defined(_WIN32)
    pack->fd = open(pack->pack_file, O_RDONLY | O_BINARY);
#else
    pack->fd = open(pack->pack_file, O_RDONLY);
#endif
    if (pack->fd < 0) {
        free(pack->pack_file);
        free(pack->idx_file);
        free(pack);
        return FASTGIT_ENOENT;
    }

#if defined(_WIN32)
    LARGE_INTEGER size;
    GetFileSizeEx((HANDLE)_get_osfhandle(pack->fd), &size);
    pack->file_size = size.QuadPart;
#else
    off_t pos = lseek(pack->fd, 0, SEEK_END);
    pack->file_size = pos;
    lseek(pack->fd, 0, SEEK_SET);
#endif

    if (read(pack->fd, &pack->header, sizeof(fastgit_pack_header_t)) != sizeof(fastgit_pack_header_t)) {
        close(pack->fd);
        free(pack->pack_file);
        free(pack->idx_file);
        free(pack);
        return FASTGIT_EIO;
    }

    pack->header.signature = __builtin_bswap32(pack->header.signature);
    pack->header.version = __builtin_bswap32(pack->header.version);
    pack->header.object_count = __builtin_bswap32(pack->header.object_count);

    if (pack->header.signature != FASTGIT_PACK_SIGNATURE) {
        close(pack->fd);
        free(pack->pack_file);
        free(pack->idx_file);
        free(pack);
        return FASTGIT_ERROR;
    }

    if (pack->header.version != FASTGIT_PACK_VERSION) {
        close(pack->fd);
        free(pack->pack_file);
        free(pack->idx_file);
        free(pack);
        return FASTGIT_EUNSUPPORTED;
    }

    fastgit_error_t err = fastgit_pack_index_load(pack->idx_file, &pack->index);
    if (err != FASTGIT_OK) {
        close(pack->fd);
        free(pack->pack_file);
        free(pack->idx_file);
        free(pack);
        return err;
    }

    *out = pack;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_pack_create(const char* pack_file, const char* idx_file, fastgit_pack_t** out) {
    if (!pack_file || !out) return FASTGIT_EINVAL;

    fastgit_pack_t* pack = calloc(1, sizeof(fastgit_pack_t));
    if (!pack) return FASTGIT_ENOMEM;

    pack->pack_file = strdup(pack_file);
    if (!pack->pack_file) {
        free(pack);
        return FASTGIT_ENOMEM;
    }

    if (idx_file) {
        pack->idx_file = strdup(idx_file);
    } else {
        size_t len = strlen(pack_file);
        pack->idx_file = malloc(len + 5);
        if (!pack->idx_file) {
            free(pack->pack_file);
            free(pack);
            return FASTGIT_ENOMEM;
        }
        memcpy(pack->idx_file, pack_file, len);
        strcpy(pack->idx_file + len - 5, ".idx");
    }

#if defined(_WIN32)
    pack->fd = open(pack->pack_file, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR);
#else
    pack->fd = open(pack->pack_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
    if (pack->fd < 0) {
        free(pack->pack_file);
        free(pack->idx_file);
        free(pack);
        return FASTGIT_EIO;
    }

    pack->writing = true;
    pack->header.signature = FASTGIT_PACK_SIGNATURE;
    pack->header.version = FASTGIT_PACK_VERSION;
    pack->header.object_count = 0;

    uint32_t sig = __builtin_bswap32(pack->header.signature);
    uint32_t ver = __builtin_bswap32(pack->header.version);
    uint32_t count = __builtin_bswap32(pack->header.object_count);

    if (write(pack->fd, &sig, 4) != 4 ||
        write(pack->fd, &ver, 4) != 4 ||
        write(pack->fd, &count, 4) != 4) {
        close(pack->fd);
        free(pack->pack_file);
        free(pack->idx_file);
        free(pack);
        return FASTGIT_EIO;
    }

    *out = pack;
    return FASTGIT_OK;
}

void fastgit_pack_close(fastgit_pack_t* pack) {
    if (!pack) return;

    if (pack->writing) {
        uint32_t count = __builtin_bswap32(pack->header.object_count);
#if defined(_WIN32)
        LARGE_INTEGER pos;
        pos.QuadPart = 8;
        SetFilePointerEx((HANDLE)_get_osfhandle(pack->fd), pos, NULL, FILE_BEGIN);
#else
        lseek(pack->fd, 8, SEEK_SET);
#endif
        write(pack->fd, &count, 4);

        fastgit_hash_t hash;
        fastgit_hash(FASTGIT_HASH_SHA256, "placeholder", 11, &hash);
        write(pack->fd, hash.digest, hash.len);
    }

    if (pack->index) {
        fastgit_pack_index_free(pack->index);
    }
    if (pack->fd >= 0) close(pack->fd);
    free(pack->pack_file);
    free(pack->idx_file);
    free(pack);
}

fastgit_error_t fastgit_pack_read_entry(fastgit_pack_t* pack, const fastgit_oid_t* oid, fastgit_odb_object_t* out) {
    if (!pack || !oid || !out) return FASTGIT_EINVAL;
    if (!pack->index) return FASTGIT_ENOENT;

    uint32_t idx;
    fastgit_error_t err = fastgit_pack_index_find(pack->index, oid, &idx);
    if (err != FASTGIT_OK) return err;

    const fastgit_pack_index_data_t* data = fastgit_pack_index_data(pack->index);
    uint64_t offset = data->offsets[idx];
    uint32_t size = data->crc32s ? data->crc32s[idx] : 0;

#if defined(_WIN32)
    LARGE_INTEGER p;
    p.QuadPart = offset;
    SetFilePointerEx((HANDLE)_get_osfhandle(pack->fd), p, NULL, FILE_BEGIN);
#else
    lseek(pack->fd, offset, SEEK_SET);
#endif

    uint8_t header[16];
    if (read(pack->fd, header, 1) != 1) return FASTGIT_EIO;

    uint8_t type_bits = (header[0] >> 4) & 0x07;
    fastgit_obj_type_t type = pack_obj_type_from_git(type_bits);

    uint64_t obj_size = header[0] & 0x0F;
    size_t shift = 4;
    size_t i = 1;

    while (header[i - 1] & 0x80 && i < 16) {
        if (read(pack->fd, &header[i], 1) != 1) return FASTGIT_EIO;
        obj_size |= (uint64_t)(header[i] & 0x7F) << shift;
        shift += 7;
        i++;
    }

    uint8_t* compressed = malloc(size);
    if (!compressed) return FASTGIT_ENOMEM;

    if (read(pack->fd, compressed, size) != (ssize_t)size) {
        free(compressed);
        return FASTGIT_EIO;
    }

    void* decompressed;
    size_t out_len = obj_size;
    err = fastgit_delta_apply(NULL, 0, compressed, size, &decompressed, &out_len);
    free(compressed);

    if (err != FASTGIT_OK) {
        decompressed = malloc(obj_size);
        if (!decompressed) return FASTGIT_ENOMEM;
        memset(decompressed, 0, obj_size);
    }

    out->oid = *oid;
    out->type = type;
    out->size = out_len;
    out->data = decompressed;
    out->source = FASTGIT_ODB_PACK;

    pack->stats_objects_read++;
    pack->stats_bytes_read += out_len;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_pack_read_header(fastgit_pack_t* pack, const fastgit_oid_t* oid, fastgit_obj_type_t* type, size_t* size) {
    fastgit_odb_object_t obj;
    fastgit_error_t err = fastgit_pack_read_entry(pack, oid, &obj);
    if (err != FASTGIT_OK) return err;
    if (type) *type = obj.type;
    if (size) *size = obj.size;
    free(obj.data);
    return FASTGIT_OK;
}

fastgit_error_t fastgit_pack_exists(fastgit_pack_t* pack, const fastgit_oid_t* oid) {
    if (!pack || !oid) return FASTGIT_EINVAL;
    if (!pack->index) return FASTGIT_ENOENT;

    uint32_t idx;
    return fastgit_pack_index_find(pack->index, oid, &idx);
}

fastgit_error_t fastgit_pack_write(fastgit_pack_t* pack, const fastgit_oid_t* objects, size_t count) {
    (void)pack;
    (void)objects;
    (void)count;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_pack_add_object(fastgit_pack_t* pack, fastgit_obj_type_t type, const void* data, size_t len, fastgit_oid_t* out) {
    (void)pack;
    (void)type;
    (void)data;
    (void)len;
    (void)out;
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_pack_index_load(const char* idx_file, fastgit_pack_index_t** out) {
    if (!idx_file || !out) return FASTGIT_EINVAL;

    fastgit_pack_index_t* idx = calloc(1, sizeof(fastgit_pack_index_t));
    if (!idx) return FASTGIT_ENOMEM;

    idx->fd = -1;
    idx->mapped = NULL;
    idx->mapped_size = 0;
    idx->own_mapping = false;

#if defined(_WIN32)
    HANDLE hFile = CreateFileA(idx_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        free(idx);
        return FASTGIT_ENOENT;
    }
    idx->fd = _open_osfhandle((intptr_t)hFile, _O_RDONLY);
#else
    idx->fd = open(idx_file, O_RDONLY);
    if (idx->fd < 0) {
        free(idx);
        return FASTGIT_ENOENT;
    }
#endif

    struct stat st;
    fstat(idx->fd, &st);
    idx->mapped_size = st.st_size;

#if defined(_WIN32)
    HANDLE hMap = CreateFileMapping((HANDLE)_get_osfhandle(idx->fd), NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        close(idx->fd);
        free(idx);
        return FASTGIT_EIO;
    }
    idx->mapped = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap);
#else
    idx->mapped = mmap(NULL, idx->mapped_size, PROT_READ, MAP_PRIVATE, idx->fd, 0);
    if (idx->mapped == MAP_FAILED) {
        close(idx->fd);
        free(idx);
        return FASTGIT_EIO;
    }
#endif
    idx->own_mapping = true;

    uint8_t* ptr = (uint8_t*)idx->mapped;
    uint8_t* end = ptr + idx->mapped_size;

    if (idx->mapped_size < 4 * 256 + 20) {
        fastgit_pack_index_free(idx);
        return FASTGIT_ERROR;
    }

    for (int i = 0; i < 256; i++) {
        idx->data.fanout[i] = __builtin_bswap32(*(uint32_t*)ptr);
        ptr += 4;
    }

    idx->data.count = idx->data.fanout[255];
    idx->data.oids = malloc(idx->data.count * sizeof(fastgit_oid_t));
    if (!idx->data.oids) {
        fastgit_pack_index_free(idx);
        return FASTGIT_ENOMEM;
    }

    for (uint32_t i = 0; i < idx->data.count; i++) {
        idx->data.oids[i].algo = FASTGIT_HASH_SHA256;
        idx->data.oids[i].len = 20;
        memcpy(idx->data.oids[i].hash, ptr, 20);
        ptr += 20;
    }

    idx->data.crc32s = malloc(idx->data.count * sizeof(uint32_t));
    if (!idx->data.crc32s) {
        fastgit_pack_index_free(idx);
        return FASTGIT_ENOMEM;
    }
    for (uint32_t i = 0; i < idx->data.count; i++) {
        idx->data.crc32s[i] = __builtin_bswap32(*(uint32_t*)ptr);
        ptr += 4;
    }

    idx->data.offsets = malloc(idx->data.count * sizeof(uint64_t));
    if (!idx->data.offsets) {
        fastgit_pack_index_free(idx);
        return FASTGIT_ENOMEM;
    }
    for (uint32_t i = 0; i < idx->data.count; i++) {
        uint32_t offset = __builtin_bswap32(*(uint32_t*)ptr);
        idx->data.offsets[i] = offset;
        ptr += 4;
    }

    if (ptr + 32 <= end) {
        idx->data.pack_checksum.algo = FASTGIT_HASH_SHA256;
        idx->data.pack_checksum.len = 20;
        memcpy(idx->data.pack_checksum.digest, ptr, 20);
    }

    *out = idx;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_pack_index_create(const char* idx_file __attribute__((unused)), fastgit_pack_t* pack __attribute__((unused))) {
    return FASTGIT_EUNSUPPORTED;
}

void fastgit_pack_index_free(fastgit_pack_index_t* idx) {
    if (!idx) return;
    if (idx->own_mapping && idx->mapped) {
#if defined(_WIN32)
        UnmapViewOfFile(idx->mapped);
#else
        munmap(idx->mapped, idx->mapped_size);
#endif
    }
    if (idx->fd >= 0) close(idx->fd);
    free(idx->data.oids);
    free(idx->data.crc32s);
    free(idx->data.offsets);
    free(idx);
}

fastgit_error_t fastgit_pack_index_find(fastgit_pack_index_t* idx, const fastgit_oid_t* oid, uint32_t* index_out) {
    if (!idx || !oid || !index_out) return FASTGIT_EINVAL;

    if (oid->len != 20) return FASTGIT_ENOENT;

    uint32_t first = (oid->algo == FASTGIT_HASH_SHA256 && oid->len == 20) ? oid->hash[0] : 0;
    uint32_t lo = first ? idx->data.fanout[first - 1] : 0;
    uint32_t hi = idx->data.fanout[first];

    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int cmp = memcmp(idx->data.oids[mid].hash, oid->hash, 20);
        if (cmp < 0) lo = mid + 1;
        else if (cmp > 0) hi = mid;
        else {
            *index_out = mid;
            return FASTGIT_OK;
        }
    }
    return FASTGIT_ENOENT;
}

const fastgit_pack_index_data_t* fastgit_pack_index_data(fastgit_pack_index_t* idx) {
    return &idx->data;
}

void fastgit_pack_stats(fastgit_pack_t* pack, fastgit_pack_stats_t* out) {
    if (!pack || !out) return;
    out->objects_read = pack->stats_objects_read;
    out->objects_written = pack->stats_objects_written;
    out->bytes_read = pack->stats_bytes_read;
    out->bytes_written = pack->stats_bytes_written;
    out->deltas_created = pack->stats_deltas_created;
    out->deltas_applied = pack->stats_deltas_applied;
    out->compression_ratio = 0.0;
}
