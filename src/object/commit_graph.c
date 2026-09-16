#include "fastgit/fastgit.h"
#include "fastgit/odb.h"
#include "fastgit/object.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <zlib.h>

static void write_uint32(uint8_t* buf, uint32_t val) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

static void write_uint64(uint8_t* buf, uint64_t val) {
    for (int i = 7; i >= 0; i--) {
        buf[i] = val & 0xFF;
        val >>= 8;
    }
}

fastgit_error_t fastgit_commit_graph_write(fastgit_repository_t* repo) {
    if (!repo) return FASTGIT_EINVAL;
    
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    if (!odb) return FASTGIT_EIO;
    
    fastgit_odb_iterator_t* it = NULL;
    if (fastgit_odb_iterator_new(odb, &it) != FASTGIT_OK) return FASTGIT_EIO;
    
    fastgit_oid_t* oids = NULL;
    size_t oid_count = 0, oid_cap = 0;
    fastgit_oid_t oid;
    
    while (fastgit_odb_iterator_next(it, &oid) == FASTGIT_OK) {
        fastgit_odb_object_t obj;
        if (fastgit_odb_read(odb, &oid, &obj) != FASTGIT_OK) continue;
        if (obj.type == FASTGIT_OBJ_COMMIT) {
            if (oid_count >= oid_cap) {
                oid_cap = oid_cap ? oid_cap * 2 : 256;
                oids = realloc(oids, oid_cap * sizeof(fastgit_oid_t));
                if (!oids) { fastgit_odb_iterator_free(it); free(obj.data); return FASTGIT_ENOMEM; }
            }
            oids[oid_count++] = oid;
        }
        free(obj.data);
    }
    fastgit_odb_iterator_free(it);
    
    if (oid_count == 0) {
        free(oids);
        return FASTGIT_OK;
    }
    
    // Sort OIDs for binary search
    qsort(oids, oid_count, sizeof(fastgit_oid_t), (int(*)(const void*,const void*))fastgit_oid_cmp);
    
    // Build graph
    uint32_t* parents = calloc(oid_count * 2, sizeof(uint32_t));
    uint32_t* generation = calloc(oid_count, sizeof(uint32_t));
    uint64_t* commit_time = calloc(oid_count, sizeof(uint64_t));
    if (!parents || !generation || !commit_time) {
        free(oids); free(parents); free(generation); free(commit_time);
        return FASTGIT_ENOMEM;
    }
    
    // Map OID to index
    for (size_t i = 0; i < oid_count; i++) {
        fastgit_odb_object_t obj;
        if (fastgit_odb_read(odb, &oids[i], &obj) != FASTGIT_OK) continue;
        if (obj.type != FASTGIT_OBJ_COMMIT) { free(obj.data); continue; }
        
        fastgit_object_t* commit_obj = NULL;
        if (fastgit_object_parse(FASTGIT_OBJ_COMMIT, obj.data, obj.size, &commit_obj) == FASTGIT_OK) {
            const fastgit_commit_t* c = fastgit_commit_parse(commit_obj);
            if (c) {
                for (size_t p = 0; p < c->parent_count && p < 2; p++) {
                    // Find parent index via binary search
                    int lo = 0, hi = oid_count - 1;
                    while (lo <= hi) {
                        int mid = (lo + hi) / 2;
                        int cmp = fastgit_oid_cmp(&c->parents[p], &oids[mid]);
                        if (cmp == 0) {
                            parents[i * 2 + p] = mid;
                            break;
                        } else if (cmp < 0) hi = mid - 1;
                        else lo = mid + 1;
                    }
                }
                commit_time[i] = c->committer ? (uint64_t)c->committer->when : 0;
            }
            fastgit_object_free(commit_obj);
        }
        free(obj.data);
    }
    
    // Topological generation numbers
    for (size_t i = 0; i < oid_count; i++) {
        uint32_t gen = 0;
        for (int p = 0; p < 2; p++) {
            uint32_t pi = parents[i * 2 + p];
            if (pi != UINT32_MAX && generation[pi] + 1 > gen) gen = generation[pi] + 1;
        }
        generation[i] = gen;
    }
    
    // Write commit-graph file
    char graph_path[4096];
    const char* gitdir = fastgit_repository_gitdir(repo);
    if (!gitdir) { free(oids); free(parents); free(generation); free(commit_time); return FASTGIT_EIO; }
    snprintf(graph_path, sizeof(graph_path), "%s/objects/info/commit-graph", gitdir);
    
    FILE* f = fopen(graph_path, "wb");
    if (!f) { free(oids); free(parents); free(generation); free(commit_time); return FASTGIT_EIO; }
    
    // Header: "CGPH" + version(1) + hash_version(1) + num_chunks(4) + padding
    uint8_t header[8] = {'C','G','P','H', 1, 1, 0, 0};
    fwrite(header, 1, 8, f);
    
    // OID Fanout (256 * 4 bytes)
    uint32_t fanout[256] = {0};
    for (size_t i = 0; i < oid_count; i++) {
        fanout[oids[i].hash[0]]++;
    }
    for (int i = 1; i < 256; i++) fanout[i] += fanout[i-1];
    for (int i = 0; i < 256; i++) {
        uint8_t buf[4];
        write_uint32(buf, fanout[i]);
        fwrite(buf, 1, 4, f);
    }
    
    // OID Lookup (oid_count * 32 bytes for SHA256)
    for (size_t i = 0; i < oid_count; i++) {
        fwrite(oids[i].hash, 1, oids[i].len, f);
    }
    
    // Commit Data: generation(4) + time(8) + parents(2*4)
    for (size_t i = 0; i < oid_count; i++) {
        uint8_t buf[20];
        write_uint32(buf, generation[i]);
        write_uint64(buf + 4, commit_time[i]);
        write_uint32(buf + 12, parents[i * 2] == 0 ? 0xFFFFFFFF : parents[i * 2]);
        write_uint32(buf + 16, parents[i * 2 + 1] == 0 ? 0xFFFFFFFF : parents[i * 2 + 1]);
        fwrite(buf, 1, 20, f);
    }
    
    // Trailer: SHA256 of file so far (simplified: write 32 zeros)
    uint8_t trailer[32] = {0};
    fwrite(trailer, 1, 32, f);
    
    fclose(f);
    free(oids);
    free(parents);
    free(generation);
    free(commit_time);
    
    return FASTGIT_OK;
}

fastgit_error_t fastgit_commit_graph_read(fastgit_repository_t* repo, fastgit_commit_graph_t** out) {
    if (!repo || !out) return FASTGIT_EINVAL;
    *out = NULL;
    
    const char* gitdir = fastgit_repository_gitdir(repo);
    if (!gitdir) return FASTGIT_ENOENT;
    
    char graph_path[4096];
    snprintf(graph_path, sizeof(graph_path), "%s/objects/info/commit-graph", gitdir);
    
    FILE* f = fopen(graph_path, "rb");
    if (!f) return FASTGIT_ENOENT;
    
    // Read header
    uint8_t header[8];
    if (fread(header, 1, 8, f) != 8 || memcmp(header, "CGPH", 4) != 0) {
        fclose(f);
        return FASTGIT_EINVAL;
    }
    
    // Skip fanout (1024 bytes)
    fseek(f, 1024, SEEK_CUR);
    
    // Read OID count from fanout[255]
    fseek(f, -4, SEEK_CUR);
    uint32_t count;
    fread(&count, 1, 4, f);
    count = __builtin_bswap32(count);
    fseek(f, 8 + 1024, SEEK_SET);
    
    fastgit_commit_graph_t* cg = calloc(1, sizeof(fastgit_commit_graph_t));
    if (!cg) { fclose(f); return FASTGIT_ENOMEM; }
    cg->count = count;
    cg->oids = calloc(count, sizeof(fastgit_oid_t));
    cg->parents = calloc(count * 2, sizeof(uint32_t));
    cg->generation = calloc(count, sizeof(uint32_t));
    cg->commit_time = calloc(count, sizeof(uint64_t));
    
    // Read OIDs
    for (size_t i = 0; i < count; i++) {
        cg->oids[i].algo = FASTGIT_HASH_SHA256;
        cg->oids[i].len = 32;
        fread(cg->oids[i].hash, 1, 32, f);
    }
    
    // Read commit data
    for (size_t i = 0; i < count; i++) {
        uint8_t buf[20];
        fread(buf, 1, 20, f);
        cg->generation[i] = __builtin_bswap32(*(uint32_t*)buf);
        cg->commit_time[i] = __builtin_bswap64(*(uint64_t*)(buf + 4));
        cg->parents[i * 2] = __builtin_bswap32(*(uint32_t*)(buf + 12));
        cg->parents[i * 2 + 1] = __builtin_bswap32(*(uint32_t*)(buf + 16));
    }
    
    fclose(f);
    *out = cg;
    return FASTGIT_OK;
}

void fastgit_commit_graph_free(fastgit_commit_graph_t* cg) {
    if (!cg) return;
    free(cg->oids);
    free(cg->parents);
    free(cg->generation);
    free(cg->commit_time);
    free(cg);
}