#include "fastgit/odb.h"
#include <stdlib.h>
#include <string.h>

fastgit_error_t fastgit_odb_loose_new(const char* path, fastgit_odb_t** out) {
    return fastgit_odb_new(path, out);
}
