#include "fastgit/platform.h"
#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__AVX2__)
#include <immintrin.h>

void fastgit_simd_memcpy(void* dst, const void* src, size_t len) {
    const uint8_t* s = (const uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;

    while (len >= 32) {
        __m256i v = _mm256_loadu_si256((const __m256i*)s);
        _mm256_storeu_si256((__m256i*)d, v);
        s += 32;
        d += 32;
        len -= 32;
    }
    while (len >= 16) {
        __m128i v = _mm_loadu_si128((const __m128i*)s);
        _mm_storeu_si128((__m128i*)d, v);
        s += 16;
        d += 16;
        len -= 16;
    }
    while (len--) *d++ = *s++;
}

void fastgit_simd_memset(void* dst, int val, size_t len) {
    uint8_t* d = (uint8_t*)dst;
    __m256i v = _mm256_set1_epi8((char)val);

    while (len >= 32) {
        _mm256_storeu_si256((__m256i*)d, v);
        d += 32;
        len -= 32;
    }
    while (len--) *d++ = val;
}

int fastgit_simd_memcmp(const void* a, const void* b, size_t len) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;

    while (len >= 32) {
        __m256i va = _mm256_loadu_si256((const __m256i*)pa);
        __m256i vb = _mm256_loadu_si256((const __m256i*)pb);
        __m256i cmp = _mm256_cmpeq_epi8(va, vb);
        int mask = _mm256_movemask_epi8(cmp);
        if (mask != 0xFFFFFFFF) {
            for (int i = 0; i < 32; i++) {
                if (pa[i] != pb[i]) return pa[i] - pb[i];
            }
        }
        pa += 32;
        pb += 32;
        len -= 32;
    }
    while (len--) {
        if (*pa != *pb) return *pa - *pb;
        pa++; pb++;
    }
    return 0;
}

uint32_t fastgit_simd_crc32c(uint32_t crc, const void* buf, size_t len) {
#if defined(__SSE4_2__)
    const uint8_t* p = (const uint8_t*)buf;
    while (len >= 8) {
        crc = _mm_crc32_u64(crc, *(const uint64_t*)p);
        p += 8;
        len -= 8;
    }
    while (len--) {
        crc = _mm_crc32_u8(crc, *p++);
    }
    return crc;
#else
    return fastgit_simd_crc32(crc, buf, len);
#endif
}

#else

void fastgit_simd_memcpy(void* dst, const void* src, size_t len) {
    memcpy(dst, src, len);
}

void fastgit_simd_memset(void* dst, int val, size_t len) {
    memset(dst, val, len);
}

int fastgit_simd_memcmp(const void* a, const void* b, size_t len) {
    return memcmp(a, b, len);
}

uint32_t fastgit_simd_crc32(uint32_t crc, const void* buf, size_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc;
}

uint32_t fastgit_simd_crc32c(uint32_t crc, const void* buf, size_t len) {
    return fastgit_simd_crc32(crc, buf, len);
}

#endif

#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>

void fastgit_simd_memcpy(void* dst, const void* src, size_t len) {
    const uint8_t* s = (const uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;

    while (len >= 16) {
        uint8x16_t v = vld1q_u8(s);
        vst1q_u8(d, v);
        s += 16;
        d += 16;
        len -= 16;
    }
    while (len--) *d++ = *s++;
}

void fastgit_simd_memset(void* dst, int val, size_t len) {
    uint8_t* d = (uint8_t*)dst;
    uint8x16_t v = vdupq_n_u8((uint8_t)val);

    while (len >= 16) {
        vst1q_u8(d, v);
        d += 16;
        len -= 16;
    }
    while (len--) *d++ = val;
}

int fastgit_simd_memcmp(const void* a, const void* b, size_t len) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;

    while (len >= 16) {
        uint8x16_t va = vld1q_u8(pa);
        uint8x16_t vb = vld1q_u8(pb);
        uint8x16_t cmp = vceqq_u8(va, vb);
        uint64x2_t cmp64 = vreinterpretq_u64_u8(cmp);
        if (vgetq_lane_u64(cmp64, 0) != UINT64_MAX || vgetq_lane_u64(cmp64, 1) != UINT64_MAX) {
            for (int i = 0; i < 16; i++) {
                if (pa[i] != pb[i]) return pa[i] - pb[i];
            }
        }
        pa += 16;
        pb += 16;
        len -= 16;
    }
    while (len--) {
        if (*pa != *pb) return *pa - *pb;
        pa++; pb++;
    }
    return 0;
}

uint32_t fastgit_simd_crc32(uint32_t crc, const void* buf, size_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc;
}

uint32_t fastgit_simd_crc32c(uint32_t crc, const void* buf, size_t len) {
    return fastgit_simd_crc32(crc, buf, len);
}

#else

void fastgit_simd_memcpy(void* dst, const void* src, size_t len) {
    memcpy(dst, src, len);
}

void fastgit_simd_memset(void* dst, int val, size_t len) {
    memset(dst, val, len);
}

int fastgit_simd_memcmp(const void* a, const void* b, size_t len) {
    return memcmp(a, b, len);
}

uint32_t fastgit_simd_crc32(uint32_t crc, const void* buf, size_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc;
}

uint32_t fastgit_simd_crc32c(uint32_t crc, const void* buf, size_t len) {
    return fastgit_simd_crc32(crc, buf, len);
}

#endif

#include "fastgit/hash.h"

fastgit_error_t fastgit_simd_sha256(const void* data, size_t len, uint8_t* out) {
    if (!data || !out) return FASTGIT_EINVAL;
    fastgit_hash_t h;
    fastgit_error_t err = fastgit_hash(FASTGIT_HASH_SHA256, data, len, &h);
    if (err != FASTGIT_OK) return err;
    memcpy(out, h.digest, 32);
    return FASTGIT_OK;
}

fastgit_error_t fastgit_simd_sha512(const void* data, size_t len, uint8_t* out) {
    if (!data || !out) return FASTGIT_EINVAL;
    fastgit_hash_t h;
    fastgit_error_t err = fastgit_hash(FASTGIT_HASH_SHA3_512, data, len, &h);
    if (err != FASTGIT_OK) {
        err = fastgit_hash(FASTGIT_HASH_SHA384, data, len, &h);
        if (err != FASTGIT_OK) return err;
        memcpy(out, h.digest, 48);
        memset(out + 48, 0, 16);
        return FASTGIT_OK;
    }
    memcpy(out, h.digest, 64);
    return FASTGIT_OK;
}
