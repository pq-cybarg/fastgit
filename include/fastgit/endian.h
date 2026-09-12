#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#include <stdlib.h>
#define FASTGIT_BSWAP32(x) _byteswap_ulong((uint32_t)(x))
#define FASTGIT_BSWAP64(x) _byteswap_uint64((uint64_t)(x))
#else
#define FASTGIT_BSWAP32(x) __builtin_bswap32((uint32_t)(x))
#define FASTGIT_BSWAP64(x) __builtin_bswap64((uint64_t)(x))
#endif
