#include "fastgit/hash.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    printf("Testing hash functions...\n");

    const char* test_data = "Hello, fastgit!";
    size_t test_len = strlen(test_data);

    fastgit_hash_t hash;

    fastgit_error_t err = fastgit_hash(FASTGIT_HASH_SHA256, test_data, test_len, &hash);
    assert(err == FASTGIT_OK);
    assert(hash.algo == FASTGIT_HASH_SHA256);
    assert(hash.len == 32);

    char hex[129];
    char hex_sha256[65];
    fastgit_hash_to_hex(&hash, hex, sizeof(hex));
    printf("SHA-256: %s\n", hex);
    assert(strlen(hex) == 64);
    strcpy(hex_sha256, hex);
    fastgit_hash_t hash_sha256 = hash;

    err = fastgit_hash(FASTGIT_HASH_SHA384, test_data, test_len, &hash);
    assert(err == FASTGIT_OK);
    assert(hash.algo == FASTGIT_HASH_SHA384);
    assert(hash.len == 48);
    fastgit_hash_to_hex(&hash, hex, sizeof(hex));
    printf("SHA-384: %s\n", hex);
    assert(strlen(hex) == 96);

    err = fastgit_hash(FASTGIT_HASH_SHA3_256, test_data, test_len, &hash);
    assert(err == FASTGIT_OK);
    assert(hash.algo == FASTGIT_HASH_SHA3_256);
    assert(hash.len == 32);
    fastgit_hash_to_hex(&hash, hex, sizeof(hex));
    printf("SHA3-256: %s\n", hex);
    assert(strlen(hex) == 64);

    err = fastgit_hash(FASTGIT_HASH_SHA3_384, test_data, test_len, &hash);
    assert(err == FASTGIT_OK);
    assert(hash.algo == FASTGIT_HASH_SHA3_384);
    assert(hash.len == 48);
    fastgit_hash_to_hex(&hash, hex, sizeof(hex));
    printf("SHA3-384: %s\n", hex);
    assert(strlen(hex) == 96);

    err = fastgit_hash(FASTGIT_HASH_SHA3_512, test_data, test_len, &hash);
    assert(err == FASTGIT_OK);
    assert(hash.algo == FASTGIT_HASH_SHA3_512);
    assert(hash.len == 64);
    fastgit_hash_to_hex(&hash, hex, sizeof(hex));
    printf("SHA3-512: %s\n", hex);
    assert(strlen(hex) == 128);

    err = fastgit_hash(FASTGIT_HASH_SHAKE128, test_data, test_len, &hash);
    assert(err == FASTGIT_OK);
    assert(hash.algo == FASTGIT_HASH_SHAKE128);
    fastgit_hash_to_hex(&hash, hex, sizeof(hex));
    printf("SHAKE128: %s\n", hex);

    err = fastgit_hash(FASTGIT_HASH_SHAKE256, test_data, test_len, &hash);
    assert(err == FASTGIT_OK);
    assert(hash.algo == FASTGIT_HASH_SHAKE256);
    fastgit_hash_to_hex(&hash, hex, sizeof(hex));
    printf("SHAKE256: %s\n", hex);

    fastgit_hash_t hash2;
    err = fastgit_hash(FASTGIT_HASH_SHA256, test_data, test_len, &hash2);
    assert(err == FASTGIT_OK);
    assert(fastgit_hash_equal(&hash_sha256, &hash2));
    assert(fastgit_hash_compare(&hash_sha256, &hash2) == 0);

    err = fastgit_hash(FASTGIT_HASH_SHA256, "different", 9, &hash2);
    assert(err == FASTGIT_OK);
    assert(!fastgit_hash_equal(&hash_sha256, &hash2));

    err = fastgit_hash_from_hex(FASTGIT_HASH_SHA256, hex_sha256, &hash2);
    assert(err == FASTGIT_OK);
    assert(fastgit_hash_equal(&hash_sha256, &hash2));

    printf("All hash tests passed!\n");
    return 0;
}
