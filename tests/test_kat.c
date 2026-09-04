#include "fastgit/hash.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void check(uint8_t algo, const char* msg, const char* expect_hex) {
    fastgit_hash_t h;
    fastgit_error_t err = fastgit_hash(algo, msg, strlen(msg), &h);
    assert(err == FASTGIT_OK);
    char hex[129];
    fastgit_hash_to_hex(&h, hex, sizeof(hex));
    if (strcmp(hex, expect_hex) != 0) {
        fprintf(stderr, "KAT FAIL algo=0x%02x msg='%s'\n got  %s\n want %s\n", algo, msg, hex, expect_hex);
        assert(0);
    }
}

int main(void) {
    // SHA-256 NIST
    check(FASTGIT_HASH_SHA256, "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check(FASTGIT_HASH_SHA256, "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check(FASTGIT_HASH_SHA256, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // SHA-384 (NIST)
    check(FASTGIT_HASH_SHA384, "", "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b");
    check(FASTGIT_HASH_SHA384, "abc", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
    // SHA3-256
    check(FASTGIT_HASH_SHA3_256, "", "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a");
    check(FASTGIT_HASH_SHA3_256, "abc", "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532");
    // SHA3-512 empty (NIST, verified via openssl & hashlib)
    check(FASTGIT_HASH_SHA3_512, "", "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26");
    printf("KAT OK: SHA-256/384/SHA3-256/512 vectors passed\n");
    return 0;
}
