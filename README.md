# fastgit

A high-performance Git implementation in C targeting baremetal optimization across all platforms.

## Features

- **Multi-hash support**: SHA-256, SHA-384 (CNSA), SHA-3/SHAKE, PQC-ready signatures
- **High-performance ODB**: Loose objects + packfiles with delta compression, multi-pack-index
- **Optimized index**: Memory-mapped I/O, parallel operations
- **Worktree operations**: Checkout, status, diff with SIMD acceleration
- **Network protocol**: Smart HTTP/SSH with PQC signature support
- **Merge/rebase**: Optimized algorithms
- **Platform optimizations**: io_uring (Linux), IOCP (Windows), kqueue (macOS/BSD)
- **CLI**: Git-compatible interface

## Spec

See [SPEC.md](SPEC.md) for format, repository layout, and supported commands. This
project implements SPEC v0.2: SHA-256 by default with SHA-384/SHA3/SHAKE available,
`blob <size>\0<data>` zlib loose objects at `$GITDIR/objects/ab/cdef`, and
`.git/{HEAD,config,objects,refs}` layout interoperable with `git cat-file`.

Non-goals: pack/fetch/push and server claims are out of scope for v0.2.

## Performance (measured on Apple M-series, portable build)

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

Other sections (`[performance]`, `[network]`, `[pqc]`) are not implemented in v0.2.

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