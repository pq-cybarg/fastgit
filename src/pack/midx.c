#include "fastgit/pack.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

struct fastgit_midx {
    char* file;
    int fd;
    void* mapped;
    size_t mapped_size;
    bool own_mapping;

    uint32_t version;
    uint32_t pack_count;
    uint32_t object_count;
    uint32_t* pack_ids;
    fastgit_oid_t* oids;
    uint32_t* pack_indices;
    uint64_t* offsets;
    fastgit_hash_t checksum;
};

fastgit_error_t fastgit_midx_create(const char* midx_file __attribute__((unused)), const char** pack_dirs __attribute__((unused)), size_t dir_count __attribute__((unused))) {
    return FASTGIT_EUNSUPPORTED;
}

fastgit_error_t fastgit_midx_open(const char* midx_file, fastgit_midx_t** out) {
    if (!midx_file || !out) return FASTGIT_EINVAL;

    fastgit_midx_t* midx = calloc(1, sizeof(fastgit_midx_t));
    if (!midx) return FASTGIT_ENOMEM;

    midx->file = strdup(midx_file);
    if (!midx->file) {
        free(midx);
        return FASTGIT_ENOMEM;
    }

#if defined(_WIN32)
    HANDLE hFile = CreateFileA(midx_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        free(midx->file);
        free(midx);
        return FASTGIT_ENOENT;
    }
    midx->fd = _open_osfhandle((intptr_t)hFile, _O_RDONLY);
#else
    midx->fd = open(midx_file, O_RDONLY);
    if (midx->fd < 0) {
        free(midx->file);
        free(midx);
        return FASTGIT_ENOENT;
    }
#endif

    struct stat st;
    fstat(midx->fd, &st);
    midx->mapped_size = st.st_size;

#if defined(_WIN32)
    HANDLE hMap = CreateFileMapping((HANDLE)_get_osfhandle(midx->fd), NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        close(midx->fd);
        free(midx->file);
        free(midx);
        return FASTGIT_EIO;
    }
    midx->mapped = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap);
#else
    midx->mapped = mmap(NULL, midx->mapped_size, PROT_READ, MAP_PRIVATE, midx->fd, 0);
    if (midx->mapped == MAP_FAILED) {
        close(midx->fd);
        free(midx->file);
        free(midx);
        return FASTGIT_EIO;
    }
#endif
    midx->own_mapping = true;

    uint8_t* ptr = (uint8_t*)midx->mapped;
    if (midx->mapped_size < 12) {
        fastgit_midx_free(midx);
        return FASTGIT_ERROR;
    }

    uint32_t signature = __builtin_bswap32(*(uint32_t*)ptr);
    ptr += 4;
    if (signature != 0x4D494458) {
        fastgit_midx_free(midx);
        return FASTGIT_ERROR;
    }

    midx->version = __builtin_bswap32(*(uint32_t*)ptr);
    ptr += 4;
    midx->pack_count = __builtin_bswap32(*(uint32_t*)ptr);
    ptr += 4;

    if (midx->mapped_size < 12 + midx->pack_count * 4) {
        fastgit_midx_free(midx);
        return FASTGIT_ERROR;
    }

    midx->pack_ids = malloc(midx->pack_count * sizeof(uint32_t));
    if (!midx->pack_ids) {
        fastgit_midx_free(midx);
        return FASTGIT_ENOMEM;
    }
    for (uint32_t i = 0; i < midx->pack_count; i++) {
        midx->pack_ids[i] = __builtin_bswap32(*(uint32_t*)ptr);
        ptr += 4;
    }

    midx->object_count = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t count = __builtin_bswap32(*(uint32_t*)ptr);
        ptr += 4;
        if (i == 255) midx->object_count = count;
    }

    midx->oids = malloc(midx->object_count * sizeof(fastgit_oid_t));
    if (!midx->oids) {
        fastgit_midx_free(midx);
        return FASTGIT_ENOMEM;
    }
    for (uint32_t i = 0; i < midx->object_count; i++) {
        midx->oids[i].algo = FASTGIT_HASH_SHA256;
        midx->oids[i].len = 32;
        memcpy(midx->oids[i].hash, ptr, 32);
        ptr += 32;
    }

    midx->pack_indices = malloc(midx->object_count * sizeof(uint32_t));
    if (!midx->pack_indices) {
        fastgit_midx_free(midx);
        return FASTGIT_ENOMEM;
    }
    for (uint32_t i = 0; i < midx->object_count; i++) {
        midx->pack_indices[i] = *ptr++;
    }

    midx->offsets = malloc(midx->object_count * sizeof(uint64_t));
    if (!midx->offsets) {
        fastgit_midx_free(midx);
        return FASTGIT_ENOMEM;
    }
    for (uint32_t i = 0; i < midx->object_count; i++) {
        uint32_t offset = __builtin_bswap32(*(uint32_t*)ptr);
        midx->offsets[i] = offset;
        ptr += 4;
    }

    if ((size_t)midx->mapped_size >= (size_t)((ptr - (uint8_t*)midx->mapped) + 32)) {
        midx->checksum.algo = FASTGIT_HASH_SHA256;
        midx->checksum.len = 32;
        memcpy(midx->checksum.digest, ptr, 32);
    }

    *out = midx;
    return FASTGIT_OK;
}

void fastgit_midx_free(fastgit_midx_t* midx) {
    if (!midx) return;
    if (midx->own_mapping && midx->mapped) {
#if defined(_WIN32)
        UnmapViewOfFile(midx->mapped);
#else
        munmap(midx->mapped, midx->mapped_size);
#endif
    }
    if (midx->fd >= 0) close(midx->fd);
    free(midx->file);
    free(midx->pack_ids);
    free(midx->oids);
    free(midx->pack_indices);
    free(midx->offsets);
    free(midx);
}

fastgit_error_t fastgit_midx_find(fastgit_midx_t* midx, const fastgit_oid_t* oid, fastgit_pack_t** pack_out, uint32_t* index_out) {
    if (!midx || !oid || !pack_out || !index_out) return FASTGIT_EINVAL;
    if (oid->len != 32) return FASTGIT_ENOENT;

    uint32_t first = oid->hash[0];
    uint8_t* fanout = (uint8_t*)midx->mapped + 12 + midx->pack_count * 4;
    uint32_t lo = (first == 0) ? 0 : __builtin_bswap32(*(uint32_t*)(fanout + (first - 1) * 4));
    uint32_t hi = __builtin_bswap32(*(uint32_t*)(fanout + first * 4));

    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int cmp = memcmp(midx->oids[mid].hash, oid->hash, 32);
        if (cmp < 0) lo = mid + 1;
        else if (cmp > 0) hi = mid;
        else {
            *index_out = mid;
            *pack_out = NULL;
            return FASTGIT_OK;
        }
    }
    return FASTGIT_ENOENT;
}

void fastgit_midx_stats(fastgit_midx_t* midx, fastgit_midx_stats_t* out) {
    if (!midx || !out) return;
    out->pack_count = midx->pack_count;
    out->object_count = midx->object_count;
    out->total_size = 0;
    out->midx_checksum = midx->checksum;
}
