#include "fastgit/odb.h"
#include "fastgit/object.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    printf("Testing ODB...\n");

    char tmpdir[] = "/tmp/fastgit_test_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != NULL);

    fastgit_odb_t* odb;
    fastgit_error_t err = fastgit_odb_new(dir, &odb);
    assert(err == FASTGIT_OK);

    fastgit_object_t* blob;
    err = fastgit_blob_create("Test content", 12, &blob);
    assert(err == FASTGIT_OK);

    fastgit_oid_t oid;
    err = fastgit_odb_write(odb, FASTGIT_OBJ_BLOB, "Test content", 12, &oid);
    assert(err == FASTGIT_OK);
    printf("Written OID: ");
    char hex[65];
    fastgit_oid_to_hex(&oid, hex, sizeof(hex));
    printf("%s\n", hex);

    bool exists = fastgit_odb_exists(odb, &oid) == FASTGIT_OK;
    assert(exists);

    fastgit_odb_object_t obj;
    err = fastgit_odb_read(odb, &oid, &obj);
    assert(err == FASTGIT_OK);
    assert(obj.type == FASTGIT_OBJ_BLOB);
    assert(obj.size == 12);
    assert(memcmp(obj.data, "Test content", 12) == 0);
    free(obj.data);

    fastgit_oid_t oid2;
    err = fastgit_odb_write(odb, FASTGIT_OBJ_BLOB, "Another content", 15, &oid2);
    assert(err == FASTGIT_OK);

    fastgit_odb_iterator_t* iter;
    err = fastgit_odb_iterator_new(odb, &iter);
    assert(err == FASTGIT_OK);

    size_t count = 0;
    fastgit_oid_t iter_oid;
    while (fastgit_odb_iterator_next(iter, &iter_oid) == FASTGIT_OK) {
        count++;
        char iter_hex[65];
        fastgit_oid_to_hex(&iter_oid, iter_hex, sizeof(iter_hex));
        printf("Iter OID: %s\n", iter_hex);
    }
    assert(count == 2);
    fastgit_odb_iterator_free(iter);

    fastgit_odb_stats_t stats;
    fastgit_odb_stats(odb, &stats);
    printf("Reads: %llu, Writes: %llu\n", stats.reads, stats.writes);

    err = fastgit_odb_delete(odb, &oid);
    assert(err == FASTGIT_OK);
    assert(fastgit_odb_exists(odb, &oid) != FASTGIT_OK);

    fastgit_odb_free(odb);

    system("rm -rf /tmp/fastgit_test_*");

    printf("All ODB tests passed!\n");
    return 0;
}
