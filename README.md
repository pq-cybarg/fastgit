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

## Performance Targets (30x GitHub Scale)

| Operation | Target |
|-----------|--------|
| `git init` | < 5ms |
| `git add` (10K files) | < 500ms |
| `git commit` | < 10ms |
| `git status` (1M files) | < 200ms |
| `git diff` (100K lines) | < 100ms |
| Object lookup (cold) | < 1ms |
| Object lookup (hot) | < 10µs |
| Packfile write | > 500 MB/sec |
| Packfile read | > 1 GB/sec |
| Network transfer | Line rate (100Gbps+) |

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

Create `.git/fastgit.conf` or `~/.config/fastgit/config`:

```ini
[core]
    hashAlgorithm = sha256
    compression = zstd
    compressionLevel = 3
    packThreads = auto
    indexThreads = auto
    fsmonitor = true

[performance]
    workerThreads = auto
    ioBackend = io_uring
    mmapThreshold = 256MB
    cacheSize = 512MB
    loadShedding = true

[network]
    protocol = http2
    tlsVersion = 1.3
    rateLimit = 100000

[pqc]
    enabled = true
    algorithms = ml-dsa-65,slh-dsa-sha2-128f
    hybrid = true
```

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