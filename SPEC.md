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
- `ctest` 10 suites; `bench_hash/odb/index/pack/diff/full`.
- Targets: ODB write >1M/s read >5M/s, index add >2M/s, hash ~1.6GB/s SHA-256.
- KATs: `tests/test_kat` NIST vectors for SHA-256/384/SHA3 (abc, empty, long). Must pass on CI.

## 9. Roadmap to beat git (§10 honest status)
| Feature | v0.2 → v0.3 status | Next |
|---------|-------------|------|
| hash/ODB loose/index/cli | shipped, git-interop verified (git cat-file, git ls-files, git status parity) | polish |
| pack v2 read (SHA-256) | shipped — mmap PACK 0x5041434b ver2 varint OFS ((off+1)<<7)/REF32 inflate+delta idx v2 magic ff744f63 fanout 32B, verified on git-generated packs (557B/668B packs cross-read) | survive `git verify-pack` on macOS requires non-Apple Git (Apple Git 2.50 SHA256 experimental reports wrong index v2 size) |
| pack write / idx v2 / MIDX / delta | shipped — pack write w_oids/w_crcs/w_offsets varint hdr SHA256 trailer idx v2 sorted fanout, MIDX 20→32 OID, delta 127-chunk git-compatible, verified pack_smoke 72B+1176 idx + git-generated cross-read + midx/delta e2e | beat `git repack` throughput on 1M-object bench |
| smart HTTP (pkt-line) / SSH | shipped — libcurl smart_http Retry-After/JWT/circuit/budget + pkt-line parse (40/64-char, 0000/0001, cap truncation) wired to remote_ls/fetch/push; libssh ssh://+scp with cred_cb channel + retry loop branching is_http/is_ssh | surpass `git fetch/push` latency on 10K-ref bench |
| io_uring / QUIC / distributed store | io_uring shipped fast path via liburing on Linux (preadd/pwrite/fsync via io_uring) with fallback portable loop on macOS; QUIC/distributed store | remaining |

No feature is out of scope. The project aims to beat git on every front; v0.3 retains honesty: Apple Git SHA256 verify-pack remains experimental, but fastgit packs now interoperate with git-generated SHA256 packs.

## 10. Verification
```
cmake -S . -B build && cmake --build build && ctest --test-dir build  # 10/10 pass 2.6s
./build/src/fastgit-cli hash-object -w file && git --git-dir=.git cat-file -p <oid>  # 64-hex SHA256 interop
./build/tests/test_kat                                              # NIST KATs
/tmp/pack_smoke  # 72B pack + 1176 idx + delta variant (both custom & git-generated cross-read pass)
```
