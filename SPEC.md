# fastgit Specification v0.2

Baremetal C implementation of Git, portable across macOS/Linux/Windows/ARM.

## 1. Goals
- Drop-in git compatibility for loose objects and CLI verbs
- Multi-hash OIDs (SHA-256 default) with CNSA SHA-384/SHA-3/SHAKE
- PQC signature verification delegated to OpenSSL/liboqs (not custom crypto)
- Repeatable benchmarks, no `arch=native` by default

## 2. Hash Algorithms
| Algo | ID | Digest | Status |
|------|----|--------|--------|
| SHA-256 | 0x01 | 32 | default, NIST KAT verified |
| SHA-384 | 0x02 | 48 | CNSA, OpenSSL EVP |
| SHA3-256 | 0x03 | 32 | Keccak, NIST KAT |
| SHA3-384 | 0x04 | 48 |  |
| SHA3-512 | 0x05 | 64 | |
| SHAKE128/256 | 0x06/0x07 | variable | XOF |

`fastgit_hash(algo, data, len, out)` vtable; OpenSSL fast path when `FASTGIT_HAVE_OPENSSL`.

## 3. Object Model
`struct fastgit_oid { uint8_t hash[64]; size_t len; uint8_t algo; }`
Types: blob/tree/commit/tag. Header `<type> <size>\0<data>` hashed per git spec.
Serialize via `fastgit_object_serialize`, hash via `fastgit_object_hash`.

## 4. Object Database (git-interop)
- Loose: `$GITDIR/objects/ab/cdef...` where `ab = hex[0:2]` (2-char fanout), zlib-compressed header+data. No algo prefix. Verified by `git cat-file -p <oid>` when `extensions.objectFormat=sha256`.
- `repositoryformatversion=1` with `objectFormat=sha256` on `init`.
- Reads support both 40-hex (SHA1 legacy) and 64-hex.
- Hot cache: 16K direct-mapped, 8-probe, `FASTGIT_ODB_CACHE_SIZE=16384`.

## 5. Repository Layout (`fastgit_repository_init`)
Creates `.git/{HEAD,config,description,info/exclude,packed-refs,objects/{,pack,info},refs/{heads,tags},hooks,branches,info}`. HEAD `ref: refs/heads/main`. Config core+extensions.

## 6. CLI
Verbs: init, hash-object [-t <type>] [-w] [--stdin], cat-file [-p|-t|-s], status, add, benchmark, stats, version. `hash-object -w` writes via ODB and prints hex equal to `git hash-object` for blobs.

## 7. Build
CMake 3.20+ C23. Options `FASTGIT_NATIVE_OPT` (OFF default, enables `-march=native`), `FASTGIT_ENABLE_WERROR` (OFF), `FASTGIT_ENABLE_LTO` (ON, via IPO check), `FASTGIT_USE_OPENSSL/SIMD`. No hardcoded `-arch arm64` without opt-in.

## 8. Benchmarks & Correctness
- `ctest` 9 suites; `bench_hash/odb/index/pack/diff/full`.
- Targets: ODB write >1M/s read >5M/s, index add >2M/s, hash ~1.6GB/s SHA-256.
- KATs: `tests/test_kat` NIST vectors for SHA-256/384/SHA3 (abc, empty, long). Must pass on CI.

## 9. Non-goals (former SPEC bloat removed)
GitHub capacity/autoscaling/MySQL/Rails incident postmortems are company concerns, not git format. Deferred: pack v2/v3 writer, smart HTTP, QUIC, io_uring wiring, distributed store.

## 10. Verification
```
cmake -S . -B build && cmake --build build && ctest --test-dir build
./build/src/fastgit hash-object -w file && git --git-dir=.git cat-file -p <oid>
./build/tests/test_kat
```
