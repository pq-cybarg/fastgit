#include "fastgit/object.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    printf("Testing object model...\n");

    fastgit_object_t* blob;
    fastgit_error_t err = fastgit_blob_create("Hello, world!", 13, &blob);
    assert(err == FASTGIT_OK);
    assert(fastgit_object_type(blob) == FASTGIT_OBJ_BLOB);
    assert(fastgit_blob_size(blob) == 13);
    assert(memcmp(fastgit_blob_data(blob), "Hello, world!", 13) == 0);

    fastgit_oid_t oid;
    err = fastgit_object_hash(blob, FASTGIT_HASH_SHA256, &oid);
    assert(err == FASTGIT_OK);
    assert(oid.len == 32);
    assert(oid.algo == FASTGIT_HASH_SHA256);

    char hex[65];
    fastgit_oid_to_hex(&oid, hex, sizeof(hex));
    printf("Blob OID: %s\n", hex);

    fastgit_object_free(blob);

    fastgit_object_t* tree;
    err = fastgit_tree_create(&tree);
    assert(err == FASTGIT_OK);
    assert(fastgit_object_type(tree) == FASTGIT_OBJ_TREE);
    assert(fastgit_tree_entry_count(tree) == 0);

    fastgit_tree_entry_t entry = {0};
    entry.oid = oid;
    entry.mode = 0100644;
    entry.path = "test.txt";
    err = fastgit_tree_add_entry(tree, &entry);
    assert(err == FASTGIT_OK);
    assert(fastgit_tree_entry_count(tree) == 1);

    const fastgit_tree_entry_t* found = fastgit_tree_entry_by_name(tree, "test.txt");
    assert(found != NULL);
    assert(strcmp(found->path, "test.txt") == 0);
    assert(fastgit_oid_equal(&found->oid, &oid));

    fastgit_object_free(tree);

    fastgit_signature_t* sig;
    err = fastgit_signature_new("Test User", "test@example.com", 1234567890, 0, &sig);
    assert(err == FASTGIT_OK);
    assert(strcmp(sig->name, "Test User") == 0);
    assert(strcmp(sig->email, "test@example.com") == 0);
    fastgit_signature_free(sig);

    printf("All object tests passed!\n");
    return 0;
}
