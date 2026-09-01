#include "fastgit/index.h"
#include <stdlib.h>
#include <string.h>

fastgit_error_t fastgit_index_entry_new(const char* path, uint32_t mode, const fastgit_oid_t* oid, fastgit_index_entry_t** out) {
    if (!path || !oid || !out) return FASTGIT_EINVAL;

    fastgit_index_entry_t* entry = calloc(1, sizeof(fastgit_index_entry_t));
    if (!entry) return FASTGIT_ENOMEM;

    entry->path = strdup(path);
    if (!entry->path) {
        free(entry);
        return FASTGIT_ENOMEM;
    }

    entry->oid = *oid;
    entry->mode = mode;
    entry->stage = FASTGIT_INDEX_STAGE_NORMAL;
    entry->flags = (uint16_t)strlen(path);
    entry->flags_extended = 0;

    *out = entry;
    return FASTGIT_OK;
}

void fastgit_index_entry_free(fastgit_index_entry_t* entry) {
    if (!entry) return;
    free(entry->path);
    free(entry);
}

int fastgit_index_entry_cmp(const fastgit_index_entry_t* a, const fastgit_index_entry_t* b) {
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    int cmp = strcmp(a->path, b->path);
    if (cmp != 0) return cmp;
    return (int)a->stage - (int)b->stage;
}

bool fastgit_index_entry_is_conflict(const fastgit_index_entry_t* entry) {
    return entry && entry->stage != FASTGIT_INDEX_STAGE_NORMAL;
}

bool fastgit_index_entry_is_valid(const fastgit_index_entry_t* entry) {
    return entry && entry->path && entry->path[0] != '\0';
}
