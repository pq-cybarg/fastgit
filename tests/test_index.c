#include "fastgit/index.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

int main(void) {
    printf("Testing index...\n");

    fastgit_index_t* index;
    fastgit_error_t err = fastgit_index_new(&index);
    assert(err == FASTGIT_OK);

    fastgit_oid_t oid;
    fastgit_hash_t hash;
    fastgit_hash(FASTGIT_HASH_SHA256, "test", 4, &hash);
    fastgit_oid_from_hex("d4735e3a265e16eee03f59718b9b5d03019c07d8b6c51f90da3a666eec13ab35", &oid);

    err = fastgit_index_add_from_buffer(index, "test.txt", 0100644, "test", 4);
    assert(err == FASTGIT_OK);
    assert(fastgit_index_entry_count(index) == 1);

    fastgit_index_entry_t* entry;
    err = fastgit_index_find(index, "test.txt", FASTGIT_INDEX_STAGE_NORMAL, &entry);
    assert(err == FASTGIT_OK);
    assert(strcmp(entry->path, "test.txt") == 0);
    assert(entry->mode == 0100644);
    assert(entry->stage == FASTGIT_INDEX_STAGE_NORMAL);

    err = fastgit_index_remove(index, "test.txt", FASTGIT_INDEX_STAGE_NORMAL);
    assert(err == FASTGIT_OK);
    assert(fastgit_index_entry_count(index) == 0);

    err = fastgit_index_add_from_buffer(index, "a.txt", 0100644, "a", 1);
    assert(err == FASTGIT_OK);
    err = fastgit_index_add_from_buffer(index, "b.txt", 0100644, "b", 1);
    assert(err == FASTGIT_OK);
    err = fastgit_index_add_from_buffer(index, "c.txt", 0100644, "c", 1);
    assert(err == FASTGIT_OK);
    assert(fastgit_index_entry_count(index) == 3);

    const fastgit_index_entry_t* e0 = fastgit_index_entry_by_index(index, 0);
    const fastgit_index_entry_t* e1 = fastgit_index_entry_by_index(index, 1);
    const fastgit_index_entry_t* e2 = fastgit_index_entry_by_index(index, 2);
    assert(strcmp(e0->path, "a.txt") == 0);
    assert(strcmp(e1->path, "b.txt") == 0);
    assert(strcmp(e2->path, "c.txt") == 0);

    fastgit_index_clear(index);
    assert(fastgit_index_entry_count(index) == 0);

    fastgit_index_free(index);

    printf("All index tests passed!\n");
    return 0;
}
