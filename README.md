# fastgit

A high-performance Git implementation in C targeting baremetal optimization across all platforms.

## Features

- **Multi-hash support**: SHA-256, SHA-384 (CNSA), SHA-3/SHAKE, PQC-ready signatures
- **High-performance ODB** (v0.2 + v0.3 pack read): loose objects + pack v2 read (SHA-256) with OFS/REF delta and idx fanout; pack write/MIDX _planned_ (SPEC §9)
- **Optimized index** (v0.2): mmap + parallel bulk add; status currently sequential
- **Worktree** (v0.2): status/diff via index-vs-worktree; checkout pending
- **Network** _planned_: smart HTTP/SSH + PQC — not yet surpassing `git fetch`/`push`
- **Merge/rebase** _planned_: not yet surpassing `git merge`/`rebase`
- **Platform** _planned_: io_uring/IOCP/kqueue wiring pending
- **CLI**: Git-compatible interface

## Spec

See [SPEC.md](SPEC.md) for format, repository layout, and supported commands. This
project implements SPEC v0.2: SHA-256 by default with SHA-384/SHA3/SHAKE available,
`blob <size>\0<data>` zlib loose objects at `$GITDIR/objects/ab/cdef`, and
`.git/{HEAD,config,objects,refs}` layout interoperable with `git cat-file`.

Scope: nothing is out of scope. v0.2 ships loose ODB + index + hash (see §9); pack read/write, smart HTTP/SSH, io_uring and QUIC are on the roadmap to beat `git verify-pack`/`fetch`/`push` (SPEC §9, then §10).

## Performance (measured on Apple M-series, portable build — §9, not chromium on bandwidth)

| Operation | Measured |
|-----------|----------|
| `hash-object` (SHA-256) | ~1700 MB/s |
| ODB write / read (hot) | ~1.1M / ~28M ops/s |
| index add (bulk, with hashing) | ~11M entries/s |

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Options

```bash
cmake -DFASTGIT_USE_OPENSSL=ON \
      -DFASTGIT_USE_SIMD=ON \
      -DFASTGIT_ENABLE_LTO=ON \
      -DFASTGIT_BUILD_TESTS=ON \
      -DFASTGIT_BUILD_BENCHMARKS=ON ..
```

### Dependencies

- C23 compiler (GCC 12+, Clang 15+, MSVC 19.35+)
- CMake 3.20+
- OpenSSL 3.0+ (optional, for hardware-accelerated crypto)
- liburing (Linux, optional)
- liboqs (optional, for PQC signatures)

## Running Tests

```bash
cd build
ctest --output-on-failure
```

## Running Benchmarks

```bash
cd build
./benchmarks/bench_hash
./benchmarks/bench_odb
./benchmarks/bench_index
./benchmarks/bench_diff
./benchmarks/bench_pack
./benchmarks/bench_full
```

## Usage

```bash
# Initialize repository
fastgit init

# Add files
fastgit add .

# Commit
fastgit commit -m "Initial commit"

# Status
fastgit status

# Diff
fastgit diff

# Log
fastgit log --oneline

# Hash object
fastgit hash-object file.txt

# Benchmark
fastgit benchmark

# Stats
fastgit stats

# Migrate hash algorithm
fastgit migrate sha384
```

## Configuration

Supported in v0.2 is the git-interop `.git/config`:

```ini
[core]
	repositoryformatversion = 1

[extensions]
	objectFormat = sha256
```

Other sections (`[performance]`, `[network]`, `[pqc]`) are roadmap (SPEC §9, then §10), not out of scope.

## Architecture

```
fastgit/
├── src/
│   ├── hash/          # Hash abstraction + implementations
│   ├── object/        # Git object model (blob, tree, commit, tag)
│   ├── odb/           # Object database (loose + pack)
│   ├── pack/          # Packfile format, delta, MIDX
│   ├── index/         # Staging area (mmap-based)
│   ├── worktree/      # Checkout, status, diff
│   ├── network/       # HTTP/SSH transport, PQC
│   ├── merge/         # Merge/rebase algorithms
│   ├── platform/      # io_uring, IOCP, kqueue, SIMD, thread pool
│   └── cli/           # Command-line interface
├── include/fastgit/   # Public headers
├── tests/             # Unit tests
├── benchmarks/        # Performance benchmarks
└── docs/              # Documentation
```

## License

MIT License