#ifndef FASTGIT_SUBMODULE_H
#define FASTGIT_SUBMODULE_H

#include "fastgit/fastgit.h"
#include "fastgit/object.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fastgit_submodule fastgit_submodule_t;



fastgit_error_t fastgit_submodule_add(fastgit_repository_t* repo, const char* url, const char* path, const char* branch);
fastgit_error_t fastgit_submodule_init(fastgit_repository_t* repo, const char* name);
fastgit_error_t fastgit_submodule_update(fastgit_repository_t* repo, const char* name, bool init);
fastgit_error_t fastgit_submodule_foreach(fastgit_repository_t* repo, int (*callback)(fastgit_submodule_info_t* info, void* payload), void* payload);
fastgit_error_t fastgit_submodule_status(fastgit_repository_t* repo, fastgit_submodule_info_t*** out_info, size_t* count);
void fastgit_submodule_info_free(fastgit_submodule_info_t** info, size_t count);
fastgit_error_t fastgit_submodule_parse_gitmodules(fastgit_repository_t* repo, fastgit_submodule_info_t*** out_info, size_t* count);

#ifdef __cplusplus
}
#endif

#endif