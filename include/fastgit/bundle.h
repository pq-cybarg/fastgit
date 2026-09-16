#ifndef FASTGIT_BUNDLE_H
#define FASTGIT_BUNDLE_H

#include "fastgit/object.h"
#include "fastgit/hash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fastgit_bundle fastgit_bundle_t;

typedef struct {
    char* name;
    fastgit_oid_t oid;
} fastgit_bundle_ref_t;

fastgit_error_t fastgit_bundle_create(fastgit_repository_t* repo, const char* path, const char* refspec, fastgit_bundle_t** out);
fastgit_error_t fastgit_bundle_verify(const char* path, fastgit_bundle_ref_t*** out_refs, size_t* out_count);
fastgit_error_t fastgit_bundle_unbundle(fastgit_repository_t* repo, const char* path, const char* refspec);
void fastgit_bundle_free(fastgit_bundle_t* bundle);
void fastgit_bundle_refs_free(fastgit_bundle_ref_t** refs, size_t count);

#ifdef __cplusplus
}
#endif

#endif