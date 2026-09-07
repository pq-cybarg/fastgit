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
    if (!target || !out || !out_len) return FASTGIT_EINVAL;
    uint8_t hdr[20];
    size_t n1 = fastgit_encode_varint(base_len, hdr);
    size_t n2 = fastgit_encode_varint(target_len, hdr + n1);
    size_t need = n1 + n2 + target_len * 2 + 16;
    uint8_t* delta = (uint8_t*)malloc(need);
    if (!delta) return FASTGIT_ENOMEM;
    size_t pos = 0;
    memcpy(delta + pos, hdr, n1); pos += n1;
    memcpy(delta + pos, hdr + n1, n2); pos += n2;
    const uint8_t* t = (const uint8_t*)target;
    const uint8_t* b = (const uint8_t*)base;
    size_t tpos = 0;
    uint8_t lit_buf[127];
    size_t lit_len = 0;
    while (tpos < target_len) {
        size_t best_len = 0;
        size_t best_off = 0;
        if (b && base_len >= 4 && target_len - tpos >= 4) {
            for (size_t boff = 0; boff + 4 <= base_len; boff++) {
                if (b[boff] != t[tpos]) continue;
                size_t ml = 0;
                size_t max_ml = base_len - boff;
                size_t remain = target_len - tpos;
                if (max_ml > remain) max_ml = remain;
                if (max_ml > 0x10000) max_ml = 0x10000;
                while (ml < max_ml && b[boff + ml] == t[tpos + ml]) ml++;
                if (ml > best_len) { best_len = ml; best_off = boff; if (ml >= remain) break; }
            }
        }
        if (best_len >= 4) {
            if (lit_len) { delta[pos++] = (uint8_t)lit_len; memcpy(delta + pos, lit_buf, lit_len); pos += lit_len; lit_len = 0; }
            uint8_t cmd = 0x80;
            if (best_off & 0xFF) cmd |= 0x01;
            if (best_off & 0xFF00) cmd |= 0x02;
            if (best_off & 0xFF0000) cmd |= 0x04;
            if (best_off & 0xFF000000) cmd |= 0x08;
            if (best_len & 0xFF) cmd |= 0x10;
            if (best_len & 0xFF00) cmd |= 0x20;
            if (best_len & 0xFF0000) cmd |= 0x40;
            if (best_len == 0x10000) best_len = 0;
            delta[pos++] = cmd;
            if (cmd & 0x01) delta[pos++] = best_off & 0xFF;
            if (cmd & 0x02) delta[pos++] = (best_off >> 8) & 0xFF;
            if (cmd & 0x04) delta[pos++] = (best_off >> 16) & 0xFF;
            if (cmd & 0x08) delta[pos++] = (best_off >> 24) & 0xFF;
            if (cmd & 0x10) delta[pos++] = best_len & 0xFF;
            if (cmd & 0x20) delta[pos++] = (best_len >> 8) & 0xFF;
            if (cmd & 0x40) delta[pos++] = (best_len >> 16) & 0xFF;
            if (best_len == 0) tpos += 0x10000; else tpos += best_len;
        } else {
            lit_buf[lit_len++] = t[tpos++];
            if (lit_len == 127) { delta[pos++] = (uint8_t)lit_len; memcpy(delta + pos, lit_buf, lit_len); pos += lit_len; lit_len = 0; }
        }
    }
    if (lit_len) { delta[pos++] = (uint8_t)lit_len; memcpy(delta + pos, lit_buf, lit_len); pos += lit_len; }
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
