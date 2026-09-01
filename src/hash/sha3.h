#ifndef FASTGIT_SHA3_H
#define FASTGIT_SHA3_H

#include "fastgit/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

const fastgit_hash_vtable_t* fastgit_sha3_256_vtable(void);
const fastgit_hash_vtable_t* fastgit_sha3_384_vtable(void);
const fastgit_hash_vtable_t* fastgit_sha3_512_vtable(void);
const fastgit_hash_vtable_t* fastgit_shake128_vtable(void);
const fastgit_hash_vtable_t* fastgit_shake256_vtable(void);

#ifdef __cplusplus
}
#endif

#endif
