#include "fastgit/pack.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "fastgit/endian.h"

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
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

typedef struct { fastgit_oid_t oid; uint8_t pack_idx; uint64_t offset; } midx_entry_t;
static size_t midx_oid_len = 32;
static int midx_entry_cmp(const void* a, const void* b) {
    size_t hl = midx_oid_len;
    if (((const midx_entry_t*)a)->oid.len) hl = ((const midx_entry_t*)a)->oid.len;
    return memcmp(((const midx_entry_t*)a)->oid.hash, ((const midx_entry_t*)b)->oid.hash, hl);
}

fastgit_error_t fastgit_midx_create(const char* midx_file, const char** pack_dirs, size_t dir_count) {
    if (!midx_file || !pack_dirs) return FASTGIT_EINVAL;
    size_t cap = 1024, count = 0;
    midx_entry_t* entries = malloc(cap * sizeof(midx_entry_t));
    if (!entries) return FASTGIT_ENOMEM;
    size_t pack_count = 0;
    for (size_t d = 0; d < dir_count; d++) {
        const char* dir = pack_dirs[d];
        if (!dir) continue;
        pack_count++;
    }
    // collect from each dir
    size_t pack_idx = 0;
    for (size_t d = 0; d < dir_count; d++) {
        const char* dir = pack_dirs[d];
        if (!dir) continue;
        DIR* dp = opendir(dir);
        if (!dp) continue;
        struct dirent* de;
        while ((de = readdir(dp)) != NULL) {
            size_t len = strlen(de->d_name);
            if (len < 4 || strcmp(de->d_name + len - 4, ".idx") != 0) continue;
            char idx_path[4096];
            snprintf(idx_path, sizeof(idx_path), "%s/%s", dir, de->d_name);
            fastgit_pack_index_t* idx = NULL;
            if (fastgit_pack_index_load(idx_path, &idx) != FASTGIT_OK) continue;
            const fastgit_pack_index_data_t* data = fastgit_pack_index_data(idx);
            for (uint32_t i = 0; i < data->count; i++) {
                if (count >= cap) {
                    cap *= 2;
                    midx_entry_t* n = realloc(entries, cap * sizeof(midx_entry_t));
                    if (!n) { fastgit_pack_index_free(idx); closedir(dp); free(entries); return FASTGIT_ENOMEM; }
                    entries = n;
                }
                entries[count].oid = data->oids[i];
                entries[count].pack_idx = (uint8_t)pack_idx;
                entries[count].offset = data->offsets ? data->offsets[i] : 0;
                count++;
            }
            fastgit_pack_index_free(idx);
        }
        closedir(dp);
        pack_idx++;
    }
    if (count == 0) { free(entries); return FASTGIT_ENOENT; }
    size_t oid_len = entries[0].oid.len ? entries[0].oid.len : 32;
    midx_oid_len = oid_len;
    qsort(entries, count, sizeof(midx_entry_t), midx_entry_cmp);
    // fanout cumulative
    uint32_t fanout[256] = {0};
    for (size_t i = 0; i < count; i++) fanout[entries[i].oid.hash[0]]++;
    uint32_t cum = 0;
    for (int i = 0; i < 256; i++) { cum += fanout[i]; fanout[i] = cum; }
    int fd = open(midx_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(entries); return FASTGIT_EIO; }
    uint32_t sig = FASTGIT_BSWAP32(0x4D494458);
    uint32_t ver = FASTGIT_BSWAP32(1);
    uint32_t pc = FASTGIT_BSWAP32((uint32_t)pack_count);
    if (write(fd, &sig, 4) != 4 || write(fd, &ver, 4) != 4 || write(fd, &pc, 4) != 4) { close(fd); free(entries); return FASTGIT_EIO; }
    for (size_t i = 0; i < pack_count; i++) { uint32_t pid = FASTGIT_BSWAP32((uint32_t)i); if (write(fd, &pid, 4) != 4) { close(fd); free(entries); return FASTGIT_EIO; } }
    for (int i = 0; i < 256; i++) { uint32_t v = FASTGIT_BSWAP32(fanout[i]); if (write(fd, &v, 4) != 4) { close(fd); free(entries); return FASTGIT_EIO; } }
    for (size_t i = 0; i < count; i++) if (write(fd, entries[i].oid.hash, (int)oid_len) != (int)oid_len) { close(fd); free(entries); return FASTGIT_EIO; }
    for (size_t i = 0; i < count; i++) { uint8_t p = entries[i].pack_idx; if (write(fd, &p, 1) != 1) { close(fd); free(entries); return FASTGIT_EIO; } }
    for (size_t i = 0; i < count; i++) { uint32_t off = FASTGIT_BSWAP32((uint32_t)entries[i].offset); if (write(fd, &off, 4) != 4) { close(fd); free(entries); return FASTGIT_EIO; } }
    uint8_t checksum[64] = {0};
    if (write(fd, checksum, (int)oid_len) != (int)oid_len) { close(fd); free(entries); return FASTGIT_EIO; }
    close(fd);
    free(entries);
    return FASTGIT_OK;
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

    uint32_t signature = FASTGIT_BSWAP32(*(uint32_t*)ptr);
    ptr += 4;
    if (signature != 0x4D494458) {
        fastgit_midx_free(midx);
        return FASTGIT_ERROR;
    }

    midx->version = FASTGIT_BSWAP32(*(uint32_t*)ptr);
    ptr += 4;
    midx->pack_count = FASTGIT_BSWAP32(*(uint32_t*)ptr);
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
        midx->pack_ids[i] = FASTGIT_BSWAP32(*(uint32_t*)ptr);
        ptr += 4;
    }

    midx->object_count = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t count = FASTGIT_BSWAP32(*(uint32_t*)ptr);
        ptr += 4;
        if (i == 255) midx->object_count = count;
    }

    size_t midx_hash_len = 32;
    {
        size_t hdr = 12 + (size_t)midx->pack_count * 4 + 1024;
        if (midx->mapped_size >= hdr) {
            size_t rem = midx->mapped_size - hdr;
            size_t cnt = midx->object_count;
            if (cnt) {
                size_t cand = (rem > cnt * 5) ? (rem - cnt * 5) / (cnt + 1) : 32;
                if (cand == 20 || cand == 32 || cand == 48 || cand == 64) midx_hash_len = cand;
                else if (cand > 48) midx_hash_len = 64;
                else if (cand > 32) midx_hash_len = 48;
                else if (cand > 20) midx_hash_len = 32;
                else midx_hash_len = 20;
            }
        }
    }
    midx->oids = malloc(midx->object_count * sizeof(fastgit_oid_t));
    if (!midx->oids) {
        fastgit_midx_free(midx);
        return FASTGIT_ENOMEM;
    }
    for (uint32_t i = 0; i < midx->object_count; i++) {
        midx->oids[i].algo = (midx_hash_len == 20) ? FASTGIT_HASH_SHA1 : (midx_hash_len == 48) ? FASTGIT_HASH_SHA384 : (midx_hash_len == 64) ? FASTGIT_HASH_SHA3_512 : FASTGIT_HASH_SHA256;
        midx->oids[i].len = midx_hash_len;
        memcpy(midx->oids[i].hash, ptr, midx_hash_len);
        ptr += midx_hash_len;
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
        uint32_t offset = FASTGIT_BSWAP32(*(uint32_t*)ptr);
        midx->offsets[i] = offset;
        ptr += 4;
    }

    {
        size_t hl = midx->object_count ? midx->oids[0].len : 32;
        if ((size_t)midx->mapped_size >= (size_t)((ptr - (uint8_t*)midx->mapped) + (int)hl)) {
            midx->checksum.algo = (hl == 20) ? FASTGIT_HASH_SHA1 : (hl == 48) ? FASTGIT_HASH_SHA384 : (hl == 64) ? FASTGIT_HASH_SHA3_512 : FASTGIT_HASH_SHA256;
            midx->checksum.len = hl;
            memcpy(midx->checksum.digest, ptr, hl);
        }
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
    if (midx->object_count && oid->len != midx->oids[0].len) return FASTGIT_ENOENT;

    uint32_t first = oid->hash[0];
    uint8_t* fanout = (uint8_t*)midx->mapped + 12 + midx->pack_count * 4;
    uint32_t lo = (first == 0) ? 0 : FASTGIT_BSWAP32(*(uint32_t*)(fanout + (first - 1) * 4));
    uint32_t hi = FASTGIT_BSWAP32(*(uint32_t*)(fanout + first * 4));

    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        size_t hl = midx->oids[mid].len ? midx->oids[mid].len : oid->len;
        int cmp = memcmp(midx->oids[mid].hash, oid->hash, hl);
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
