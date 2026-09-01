#ifndef FASTGIT_SHA256_H
#define FASTGIT_SHA256_H

#include "fastgit/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

const fastgit_hash_vtable_t* fastgit_sha256_vtable(void);

#ifdef __cplusplus
}
#endif

#endif
