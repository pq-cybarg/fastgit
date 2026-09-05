#include "fastgit/pack.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define FASTGIT_DELTA_WINDOW 1024
#define FASTGIT_DELTA_MAX_MATCH 65535

typedef struct {
    const uint8_t* data;
    size_t len;
    size_t pos;
} fastgit_delta_window_t;

static __attribute__((unused)) void delta_window_init(fastgit_delta_window_t* w, const uint8_t* data, size_t len) {
    w->data = data;
    w->len = len;
    w->pos = 0;
}

static __attribute__((unused)) void delta_window_advance(fastgit_delta_window_t* w, size_t n) {
    w->pos += n;
    if (w->pos > w->len) w->pos = w->len;
}

static __attribute__((unused)) size_t delta_window_find_match(const fastgit_delta_window_t* w, size_t max_len) {
    if (w->pos >= w->len) return 0;

    size_t max_match = w->len - w->pos;
    if (max_match > max_len) max_match = max_len;
    if (max_match < 4) return 0;

    size_t window_start = (w->pos > FASTGIT_DELTA_WINDOW) ? w->pos - FASTGIT_DELTA_WINDOW : 0;
    size_t best_len = 0;

    for (size_t i = window_start; i < w->pos; i++) {
        size_t max_possible = w->pos - i;
        if (max_possible <= best_len) continue;

        size_t match_len = 0;
        while (match_len < max_match &&
               w->data[i + match_len] == w->data[w->pos + match_len]) {
            match_len++;
        }

        if (match_len > best_len) {
            best_len = match_len;
            if (best_len >= max_len) break;
        }
    }

    return (best_len >= 4) ? best_len : 0;
}

fastgit_error_t fastgit_delta_compress(const void* base, size_t base_len, const void* target, size_t target_len, void** out, size_t* out_len) {
    (void)base; (void)base_len;
    if (!target || !out || !out_len) return FASTGIT_EINVAL;
    uint8_t hdr[20];
    size_t n1 = fastgit_encode_varint(0, hdr);
    size_t n2 = fastgit_encode_varint(target_len, hdr + n1);
    // encode as single literal insert: cmd &0x80 path in apply
    size_t need = n1 + n2 + target_len + (target_len / 127 + 2);
    uint8_t* delta = (uint8_t*)malloc(need);
    if (!delta) return FASTGIT_ENOMEM;
    size_t pos=0;
    memcpy(delta+pos, hdr, n1); pos+=n1;
    memcpy(delta+pos, hdr+n1, n2); pos+=n2;
    // git delta: split literal into 127-byte chunks with MSB 0
    size_t off = 0;
    while (off < target_len) {
        size_t chunk = target_len - off;
        if (chunk > 127) chunk = 127;
        delta[pos++] = (uint8_t)chunk;
        memcpy(delta+pos, (const uint8_t*)target + off, chunk); pos+=chunk;
        off += chunk;
    }
    *out = delta;
    *out_len = pos;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_delta_apply(const void* base, size_t base_len, const void* delta, size_t delta_len, void** out, size_t* out_len) {
    if (!delta || !out || !out_len) return FASTGIT_EINVAL;
    const uint8_t* d = (const uint8_t*)delta;
    const uint8_t* dend = d + delta_len;
    uint64_t src_size = 0;
    size_t n = fastgit_decode_varint(d, dend - d, &src_size);
    if (!n) return FASTGIT_ERROR;
    d += n;
    uint64_t dst_size = 0;
    n = fastgit_decode_varint(d, dend - d, &dst_size);
    if (!n) return FASTGIT_ERROR;
    d += n;
    *out_len = dst_size;
    *out = malloc(dst_size);
    if (!*out) return FASTGIT_ENOMEM;
    uint8_t* out_ptr = (uint8_t*)*out;
    uint8_t* out_end = out_ptr + dst_size;
    while (d < dend && out_ptr < out_end) {
        uint8_t cmd = *d++;
        if (cmd & 0x80) {
            // copy from base: bits 0x01..0x08 offset, 0x10..0x40 size
            uint32_t off = 0;
            uint32_t sz = 0;
            if (cmd & 0x01) { if (d >= dend) return FASTGIT_ERROR; off |= (uint32_t)*d++; }
            if (cmd & 0x02) { if (d >= dend) return FASTGIT_ERROR; off |= (uint32_t)*d++ << 8; }
            if (cmd & 0x04) { if (d >= dend) return FASTGIT_ERROR; off |= (uint32_t)*d++ << 16; }
            if (cmd & 0x08) { if (d >= dend) return FASTGIT_ERROR; off |= (uint32_t)*d++ << 24; }
            if (cmd & 0x10) { if (d >= dend) return FASTGIT_ERROR; sz |= (uint32_t)*d++; }
            if (cmd & 0x20) { if (d >= dend) return FASTGIT_ERROR; sz |= (uint32_t)*d++ << 8; }
            if (cmd & 0x40) { if (d >= dend) return FASTGIT_ERROR; sz |= (uint32_t)*d++ << 16; }
            if (sz == 0) sz = 0x10000;
            if (!base || off + sz > base_len) return FASTGIT_ERROR;
            if (out_ptr + sz > out_end) return FASTGIT_ERROR;
            memcpy(out_ptr, (const uint8_t*)base + off, sz);
            out_ptr += sz;
        } else {
            size_t copy_len = cmd & 0x7F;
            if (copy_len == 0) return FASTGIT_ERROR;
            if (d + copy_len > dend) return FASTGIT_ERROR;
            if (out_ptr + copy_len > out_end) return FASTGIT_ERROR;
            memcpy(out_ptr, d, copy_len);
            out_ptr += copy_len;
            d += copy_len;
        }
    }
    return FASTGIT_OK;
}
