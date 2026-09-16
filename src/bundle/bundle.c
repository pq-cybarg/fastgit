#include "fastgit/bundle.h"
#include "fastgit/fastgit.h"
#include "fastgit/odb.h"
#include "fastgit/pack.h"
#include "fastgit/hash.h"
#include "fastgit/endian.h"
#include "fastgit/network.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <zlib.h>

static fastgit_error_t unbundle_unpack_pack_to_odb(fastgit_repository_t* repo, const uint8_t* pack_data, size_t pack_len);

struct fastgit_bundle {
    int fd;
    char* path;
};

static fastgit_error_t write_bundle_header(int fd, fastgit_ref_t** refs, size_t count) {
    // Bundle format v2:
    // # v2 git bundle
    // <capabilities>
    // <sha1> refs/heads/master
    // ...
    // 
    // <pack data>
    
    const char* header = "# v2 git bundle\n";
    if (write(fd, header, strlen(header)) != (ssize_t)strlen(header)) return FASTGIT_EIO;
    
    for (size_t i = 0; i < count; i++) {
        char hex[129];
        fastgit_oid_to_hex(&refs[i]->oid, hex, sizeof(hex));
        char line[1024];
        int len = snprintf(line, sizeof(line), "%s %s\n", hex, refs[i]->name);
        if (write(fd, line, len) != len) return FASTGIT_EIO;
    }
    
    // Empty line marks end of header
    if (write(fd, "\n", 1) != 1) return FASTGIT_EIO;
    
    return FASTGIT_OK;
}

static fastgit_error_t read_bundle_header(int fd, fastgit_bundle_ref_t*** out_refs, size_t* out_count) {
    *out_refs = NULL;
    *out_count = 0;
    
    char buf[4096];
    size_t buf_len = 0;
    size_t buf_cap = sizeof(buf);
    
    // Read header lines until empty line
    char line[4096];
    fastgit_bundle_ref_t** refs = NULL;
    size_t count = 0, cap = 0;
    
    while (1) {
        ssize_t n = read(fd, line + buf_len, 1);
        if (n <= 0) break;
        buf_len += n;
        if (line[buf_len - 1] == '\n') {
            line[buf_len] = '\0';
            if (buf_len == 1) { // empty line
                break;
            }
            if (buf_len > 5 && memcmp(line, "# v2", 4) == 0) {
                buf_len = 0;
                continue;
            }
            // Parse ref line: <hex> <refname>
            char* sp = strchr(line, ' ');
            if (!sp) { buf_len = 0; continue; }
            *sp = '\0';
            char* hex = line;
            char* refname = sp + 1;
            char* nl = strchr(refname, '\n');
            if (nl) *nl = '\0';
            
            fastgit_oid_t oid;
            if (fastgit_oid_from_hex(hex, &oid) != FASTGIT_OK) { buf_len = 0; continue; }
            
            if (count >= cap) {
                size_t ncap = cap ? cap * 2 : 8;
                fastgit_bundle_ref_t** nb = realloc(refs, ncap * sizeof(fastgit_bundle_ref_t*));
                if (!nb) {
                    for (size_t i = 0; i < count; i++) free(refs[i]);
                    free(refs);
                    return FASTGIT_ENOMEM;
                }
                refs = nb; cap = ncap;
            }
            
            fastgit_bundle_ref_t* r = calloc(1, sizeof(*r));
            if (!r) {
                for (size_t i = 0; i < count; i++) free(refs[i]);
                free(refs);
                return FASTGIT_ENOMEM;
            }
            r->name = strdup(refname);
            r->oid = oid;
            refs[count++] = r;
            
            buf_len = 0;
        }
    }
    
    *out_refs = refs;
    *out_count = count;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_bundle_create(fastgit_repository_t* repo, const char* path, const char* refspec, fastgit_bundle_t** out) {
    (void)refspec;
    if (!repo || !path) return FASTGIT_EINVAL;
    (void)out;
    
    // Collect refs to bundle
    char** rlist = NULL; size_t rcnt = 0;
    fastgit_error_t err = fastgit_reference_list(repo, "refs/heads/", &rlist, &rcnt);
    if (err != FASTGIT_OK) return err;
    
    fastgit_ref_t** refs = calloc(rcnt, sizeof(fastgit_ref_t*));
    if (!refs) { fastgit_reference_list_free(rlist, rcnt); return FASTGIT_ENOMEM; }
    
    size_t count = 0;
    for (size_t i = 0; i < rcnt; i++) {
        fastgit_oid_t oid;
        if (fastgit_reference_lookup(repo, rlist[i], &oid) == FASTGIT_OK) {
            refs[count] = calloc(1, sizeof(fastgit_ref_t));
            if (!refs[count]) { fastgit_reference_list_free(rlist, rcnt); for (size_t j = 0; j < count; j++) free(refs[j]); free(refs); return FASTGIT_ENOMEM; }
            refs[count]->name = strdup(rlist[i]);
            refs[count]->oid = oid;
            count++;
        }
    }
    fastgit_reference_list_free(rlist, rcnt);
    
    // Create pack file with all objects reachable from these refs
    char pack_tpl[] = "/tmp/fastgit-bundle-XXXXXX";
    int pfd = mkstemp(pack_tpl);
    if (pfd < 0) {
        for (size_t i = 0; i < count; i++) { free(refs[i]->name); free(refs[i]); }
        free(refs);
        return FASTGIT_EIO;
    }
    close(pfd);
    char pack_file[1024]; snprintf(pack_file, sizeof(pack_file), "%s.pack", pack_tpl);
    char idx_file[1024]; snprintf(idx_file, sizeof(idx_file), "%s.idx", pack_tpl);
    
    fastgit_pack_t* pack = NULL;
    if (fastgit_pack_create(pack_file, idx_file, &pack) != FASTGIT_OK) {
        for (size_t i = 0; i < count; i++) { free(refs[i]->name); free(refs[i]); }
        free(refs);
        unlink(pack_tpl);
        return FASTGIT_EIO;
    }
    
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    fastgit_odb_iterator_t* it = NULL;
    if (fastgit_odb_iterator_new(odb, &it) == FASTGIT_OK) {
        fastgit_oid_t oid;
        while (fastgit_odb_iterator_next(it, &oid) == FASTGIT_OK) {
            fastgit_odb_object_t obj;
            if (fastgit_odb_read(odb, &oid, &obj) != FASTGIT_OK) continue;
            fastgit_oid_t dummy;
            fastgit_pack_add_object(pack, obj.type, obj.data, obj.size, &dummy);
            free(obj.data);
        }
        fastgit_odb_iterator_free(it);
    }
    fastgit_pack_close(pack);
    
    // Read pack file and append to bundle
    int bfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (bfd < 0) {
        for (size_t i = 0; i < count; i++) { free(refs[i]->name); free(refs[i]); }
        free(refs);
        unlink(pack_file); unlink(idx_file);
        return FASTGIT_EIO;
    }
    
    // Write bundle header
    if (write_bundle_header(bfd, refs, count) != FASTGIT_OK) {
        close(bfd);
        for (size_t i = 0; i < count; i++) { free(refs[i]->name); free(refs[i]); }
        free(refs);
        unlink(pack_file); unlink(idx_file);
        return FASTGIT_EIO;
    }
    
    // Append pack data
    int pfd2 = open(pack_file, O_RDONLY);
    if (pfd2 >= 0) {
        char buf[8192];
        ssize_t n;
        while ((n = read(pfd2, buf, sizeof(buf))) > 0) {
            if (write(bfd, buf, n) != n) break;
        }
        close(pfd2);
    }
    close(bfd);
    
    // Cleanup
    for (size_t i = 0; i < count; i++) { free(refs[i]->name); free(refs[i]); }
    free(refs);
    unlink(pack_file); unlink(idx_file);
    
    if (out) *out = NULL;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_bundle_verify(const char* path, fastgit_bundle_ref_t*** out_refs, size_t* out_count) {
    if (!path || !out_refs || !out_count) return FASTGIT_EINVAL;
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) return FASTGIT_ENOENT;
    
    fastgit_error_t err = read_bundle_header(fd, out_refs, out_count);
    close(fd);
    return err;
}

fastgit_error_t fastgit_bundle_unbundle(fastgit_repository_t* repo, const char* path, const char* refspec) {
    (void)refspec;
    if (!repo || !path) return FASTGIT_EINVAL;
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) return FASTGIT_ENOENT;
    
    fastgit_bundle_ref_t** refs = NULL;
    size_t count = 0;
    fastgit_error_t err = read_bundle_header(fd, &refs, &count);
    if (err != FASTGIT_OK) { close(fd); return err; }
    
    // Find pack data start
    off_t pack_start = lseek(fd, 0, SEEK_CUR);
    
    // Read pack data
    off_t file_size = lseek(fd, 0, SEEK_END);
    size_t pack_len = file_size - pack_start;
    lseek(fd, pack_start, SEEK_SET);
    
    uint8_t* pack_data = malloc(pack_len);
    if (!pack_data) { fastgit_bundle_refs_free(refs, count); close(fd); return FASTGIT_ENOMEM; }
    if (read(fd, pack_data, pack_len) != (ssize_t)pack_len) { free(pack_data); fastgit_bundle_refs_free(refs, count); close(fd); return FASTGIT_EIO; }
    close(fd);
    
    // Unpack to ODB
    err = unbundle_unpack_pack_to_odb(repo, pack_data, pack_len);
    free(pack_data);
    
    // Update refs
    if (err == FASTGIT_OK) {
        for (size_t i = 0; i < count; i++) {
            fastgit_reference_update(repo, refs[i]->name, &refs[i]->oid, "unbundle");
        }
    }
    
    fastgit_bundle_refs_free(refs, count);
    return err;
}

static fastgit_error_t unbundle_unpack_pack_to_odb(fastgit_repository_t* repo, const uint8_t* pack_data, size_t pack_len) {
    if (!repo || !pack_data || pack_len < 12) return FASTGIT_EINVAL;
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    if (!odb) return FASTGIT_EIO;
    
    size_t off = 12;
    uint32_t obj_count = 0;
    if (pack_len >= 12) obj_count = FASTGIT_BSWAP32(*(uint32_t*)(pack_data + 8));
    
    typedef struct { uint64_t off; void* data; size_t len; fastgit_obj_type_t type; } cache_ent_t;
    cache_ent_t* cache = calloc(obj_count ? obj_count : 16, sizeof(cache_ent_t));
    size_t cache_n = 0;
    
    for (uint32_t i = 0; i < obj_count && off < pack_len - 32; i++) {
        if (off >= pack_len) break;
        uint8_t c = pack_data[off];
        uint32_t ptype = (c >> 4) & 0x07;
        uint64_t psize = c & 0x0F;
        size_t shift = 4; size_t hlen = 1;
        while (c & 0x80) {
            if (off + hlen >= pack_len) break;
            c = pack_data[off + hlen];
            psize |= (uint64_t)(c & 0x7F) << shift;
            shift += 7; hlen++;
            if (hlen > 10) break;
        }
        uint64_t cur = off + hlen;
        fastgit_obj_type_t ftype = FASTGIT_OBJ_BLOB;
        if (ptype == 1) ftype = FASTGIT_OBJ_COMMIT;
        else if (ptype == 2) ftype = FASTGIT_OBJ_TREE;
        else if (ptype == 3) ftype = FASTGIT_OBJ_BLOB;
        else if (ptype == 4) ftype = FASTGIT_OBJ_TAG;
        
        if (ptype == 6 || ptype == 7) {
            uint64_t base_off = 0;
            fastgit_oid_t base_oid; bool have_base_oid = false;
            if (ptype == 6) {
                if (cur >= pack_len) break;
                uint64_t ofs = pack_data[cur] & 0x7F; size_t olen = 1;
                while (pack_data[cur + olen - 1] & 0x80) {
                    if (cur + olen >= pack_len) break;
                    ofs = ((ofs + 1) << 7) | (pack_data[cur + olen] & 0x7F);
                    olen++;
                }
                cur += olen;
                base_off = off - ofs;
            } else {
                if (cur + 32 > pack_len) break;
                base_oid.len = 32; base_oid.algo = FASTGIT_HASH_SHA256; memcpy(base_oid.hash, pack_data + cur, 32);
                have_base_oid = true; cur += 32;
            }
            
            uint8_t* delta_buf = malloc(psize); if (!delta_buf) break;
            size_t avail = pack_len - cur - 32;
            z_stream zs = {0};
            if (inflateInit(&zs) != Z_OK) { free(delta_buf); break; }
            zs.next_in = (Bytef*)(pack_data + cur); zs.avail_in = avail;
            zs.next_out = delta_buf; zs.avail_out = psize;
            int zr = inflate(&zs, Z_FINISH);
            size_t consumed = zs.total_in;
            inflateEnd(&zs);
            if (zr != Z_STREAM_END) { free(delta_buf); break; }
            cur += consumed;
            
            void* base_data = NULL; size_t base_len = 0; fastgit_obj_type_t base_type = FASTGIT_OBJ_BLOB;
            bool found = false;
            if (have_base_oid) {
                fastgit_odb_t* odb = fastgit_repository_odb(repo);
                fastgit_odb_object_t bo; if (odb && fastgit_odb_read(odb, &base_oid, &bo) == FASTGIT_OK) { base_data = bo.data; base_len = bo.size; base_type = bo.type; found = true; }
                else {
                    for (size_t k = 0; k < cache_n; k++) if (cache[k].off == base_off) { base_data = cache[k].data; base_len = cache[k].len; base_type = cache[k].type; found = true; break; }
                }
            } else {
                for (size_t k = 0; k < cache_n; k++) if (cache[k].off == base_off) { base_data = cache[k].data; base_len = cache[k].len; base_type = cache[k].type; found = true; break; }
                if (!found) {
                }
            }
            if (!found) { free(delta_buf); off = cur; continue; }
            
            void* out = NULL; size_t out_len = 0;
            const uint8_t* d = delta_buf; const uint8_t* dend = d + psize;
            uint64_t src_sz = 0; size_t n = 0; uint64_t v = 0; size_t s = 0;
            v = 0; s = 0; for (n = 0; d + n < dend; n++) { v |= (uint64_t)(d[n] & 0x7F) << s; if ((d[n] & 0x80) == 0) { n++; break; } s += 7; }
            src_sz = v; d += n;
            uint64_t dst_sz = 0; v = 0; s = 0; for (n = 0; d + n < dend; n++) { v |= (uint64_t)(d[n] & 0x7F) << s; if ((d[n] & 0x80) == 0) { n++; break; } s += 7; }
            dst_sz = v; d += n;
            uint8_t* res = malloc(dst_sz ? dst_sz : 1);
            uint8_t* o = res;
            while (d < dend) {
                uint8_t cmd = *d++;
                if (cmd & 0x80) {
                    uint32_t off2 = 0, sz2 = 0;
                    if (cmd & 0x01) off2 |= *d++;
                    if (cmd & 0x02) off2 |= *d++ << 8;
                    if (cmd & 0x04) off2 |= *d++ << 16;
                    if (cmd & 0x08) off2 |= *d++ << 24;
                    if (cmd & 0x10) sz2 |= *d++;
                    if (cmd & 0x20) sz2 |= *d++ << 8;
                    if (cmd & 0x40) sz2 |= *d++ << 16;
                    if (sz2 == 0) sz2 = 0x10000;
                    if (off2 + sz2 > base_len || (size_t)(o - res) + sz2 > dst_sz) break;
                    memcpy(o, (uint8_t*)base_data + off2, sz2); o += sz2;
                } else {
                    if (cmd == 0) break;
                    size_t ins = cmd & 0x7F;
                    if (d + ins > dend || (size_t)(o - res) + ins > dst_sz) break;
                    memcpy(o, d, ins); o += ins; d += ins;
                }
            }
            free(delta_buf);
            if ((size_t)(o - res) != dst_sz) { free(res); off = cur; continue; }
            fastgit_oid_t out_oid; fastgit_odb_write(odb, base_type, res, dst_sz, &out_oid);
            if (cache_n < obj_count) { cache[cache_n].off = off; cache[cache_n].data = res; cache[cache_n].len = dst_sz; cache[cache_n].type = base_type; cache_n++; } else free(res);
            off = cur;
        } else {
            uint8_t* out = malloc(psize ? psize : 1); if (!out) break;
            size_t avail = pack_len - cur - 32;
            z_stream zs = {0};
            if (inflateInit(&zs) != Z_OK) { free(out); break; }
            zs.next_in = (Bytef*)(pack_data + cur); zs.avail_in = avail;
            zs.next_out = out; zs.avail_out = psize;
            int zr = inflate(&zs, Z_FINISH);
            size_t consumed = zs.total_in;
            inflateEnd(&zs);
            if (zr != Z_STREAM_END) { free(out); break; }
            cur += consumed;
            fastgit_oid_t out_oid; fastgit_odb_write(odb, ftype, out, psize, &out_oid);
            if (cache_n < obj_count) { cache[cache_n].off = off; cache[cache_n].data = out; cache[cache_n].len = psize; cache[cache_n].type = ftype; cache_n++; } else free(out);
            off = cur;
        }
    }
    for (size_t i = 0; i < cache_n; i++) free(cache[i].data);
    free(cache);
    return FASTGIT_OK;
}

void fastgit_bundle_free(fastgit_bundle_t* bundle) {
    if (!bundle) return;
    if (bundle->fd >= 0) close(bundle->fd);
    free(bundle->path);
    free(bundle);
}

void fastgit_bundle_refs_free(fastgit_bundle_ref_t** refs, size_t count) {
    if (!refs) return;
    for (size_t i = 0; i < count; i++) {
        free(refs[i]->name);
        free(refs[i]);
    }
    free(refs);
}