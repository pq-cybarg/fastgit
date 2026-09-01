# fastgit Specification

## Overview

fastgit is a high-performance Git implementation in C targeting baremetal optimization across all platforms (macOS, Windows, Linux, Raspberry Pi, Mobile). Designed to handle 30x GitHub scale with measurable, repeatable performance metrics.

## Performance Targets (Derived from GitHub 2025-2026 Outages)

### Scale Targets
| Metric | Current GitHub | fastgit Target (30x) |
|--------|----------------|---------------------|
| Commits/week | 275M | 8.25B |
| Commits/year | 14B | 420B |
| Actions minutes/week | 21B | 630B |
| Max repo size | ~100GB | Multi-petabyte |
| Max objects/repo | ~1B | 30B+ |
| Concurrent operations | ~100K | 3M+ |

### Latency Targets (p99)
| Operation | Target |
|-----------|--------|
| `git init` | < 5ms |
| `git add` (10K files) | < 500ms |
| `git commit` | < 10ms |
| `git status` (1M files) | < 200ms |
| `git diff` (100K lines) | < 100ms |
| `git log --oneline -100` | < 10ms |
| `git fetch` (1M refs) | < 5s |
| `git push` (1M objects) | < 10s |
| `git clone` (10GB repo) | < 30s |
| Object lookup (cold) | < 1ms |
| Object lookup (hot) | < 10µs |

### Throughput Targets
| Operation | Target |
|-----------|--------|
| Object write (loose) | > 1M ops/sec |
| Object read (loose) | > 5M ops/sec |
| Packfile write | > 500 MB/sec |
| Packfile read | > 1 GB/sec |
| Delta compression | > 200 MB/sec |
| Index write | > 2M entries/sec |
| Network transfer | Line rate (100Gbps+) |

## GitHub Outage Root Causes Addressed

### 1. Capacity Constraints
- **Problem**: Fixed-size databases, connection pools, worker queues
- **Solution**: Lock-free data structures, elastic scaling, backpressure propagation

### 2. Database Overload
- **Problem**: Single MySQL primary for metadata, no read replicas for hot paths
- **Solution**: Embedded KV store (LMDB/rocksdb), multi-tier caching, async replication

### 3. Architectural Coupling
- **Problem**: Monolithic Rails app, synchronous cross-service calls
- **Solution**: Microkernel architecture, async message passing, capability-based security

### 4. Cascade Failures
- **Problem**: No circuit breakers, retry storms amplify load
- **Solution**: Built-in circuit breakers, exponential backoff with jitter, load shedding

### 5. Lack of Load Shedding
- **Problem**: All requests treated equally under overload
- **Solution**: Priority queues, graceful degradation, QoS classes

### 6. Network Saturation
- **Problem**: Unbounded connection acceptance, no flow control
- **Solution**: Token bucket rate limiting, TCP_NODELAY tuning, QUIC support

### 7. Auth Path Bottlenecks
- **Problem**: Centralized auth service, synchronous token validation
- **Solution**: Local JWT validation, distributed capability tokens, async revocation

### 8. Retry Storms
- **Problem**: Clients retry immediately on 5xx, amplifying load 10-100x
- **Solution**: Client-side retry budgets, server-side retry-after headers, jitter

## Hash Algorithm Support

### Required Algorithms (CNSA Suite + Post-Quantum Ready)
| Algorithm | OID | Digest | Use Case |
|-----------|-----|--------|----------|
| SHA-256 | 0x01 | 32 bytes | Default, compatibility |
| SHA-384 | 0x02 | 48 bytes | CNSA Suite, top secret |
| SHA-3-256 | 0x03 | 32 bytes | NIST standard, diversity |
| SHA-3-384 | 0x04 | 48 bytes | CNSA Suite alternative |
| SHA-3-512 | 0x05 | 64 bytes | Maximum security |
| SHAKE128 | 0x06 | Variable | XOF, key derivation |
| SHAKE256 | 0x07 | Variable | XOF, key derivation |

### PQC Signature Compatibility
- **Not implementing PQC crypto** (use OpenSSL 3.2+/liboqs)
- **Supporting PQC signatures** in commit/tag objects:
  - ML-DSA (Dilithium) signatures
  - SLH-DSA (SPHINCS+) signatures
  - Falcon signatures
  - Hybrid classical+PQC signatures
- **Object format**: Extended header with algorithm identifier
- **Verification**: Delegates to system OpenSSL/liboqs

## Object Model

### Multi-Hash Object ID
```
struct fastgit_oid {
    uint8_t hash[64];  // Max digest size (SHA-3-512/SHAKE256)
    size_t len;        // Actual digest length
    uint8_t algo;      // Algorithm identifier
};
```

### Object Types
1. **Blob**: File content
2. **Tree**: Directory listing (sorted by name)
3. **Commit**: Snapshot + metadata + parents
4. **Tag**: Annotated tag with optional signature

### Object Header
```
<object-type> <space> <size-in-bytes>\0<data>
```
With multi-hash: algorithm encoded in object store, not in header.

## Object Database

### Loose Objects
- Path: `.git/objects/<algo>/<first-2-bytes>/<remaining>`
- Compressed with zstd (level 3) by default
- Supports multiple hash algorithms simultaneously

### Packfiles
- Format: Git packfile v2/v3 compatible
- Delta compression: LZ4 + zstd hybrid
- Multi-pack-index (MIDX) for O(log n) lookups
- Bloom filters for negative lookup optimization
- Reverse index for delta base resolution

### Storage Tiers
1. **Hot**: Recent objects in memory-mapped packfiles
2. **Warm**: Compressed packfiles on SSD
3. **Cold**: Erasure-coded objects on HDD/object store

## Index / Staging Area

### Format
- Memory-mapped binary format (mmap)
- Extension-based for future compatibility
- Parallel write with conflict-free merge
- Entry count: up to 2^32-1

### Operations
- `add`: O(log n) with batch optimization
- `remove`: O(log n)
- `status`: O(changed files) using fsmonitor
- `diff`: SIMD-accelerated Myers diff

## Worktree Operations

### Checkout
- Parallel file writes (worker pool)
- Hardlink/clonefile/reflink where available
- Sparse checkout via cone mode
- Submodule parallel initialization

### Status
- fsmonitor integration (inotify/fsevents/ReadDirectoryChangesW)
- Untracked cache with directory mtimes
- Parallel stat() with thread pool

### Diff
- SIMD-accelerated Myers O(ND) algorithm
- Histogram diff for large files
- Binary diff with delta encoding
- Color output with ANSI optimization

## Network Protocol

### Smart HTTP (Git Protocol v2)
- HTTP/2 + HTTP/3 (QUIC) support
- Server-sent events for progress
- Capability negotiation
- Partial clone support (blobless, treeless)

### SSH
- Native SSH implementation (libssh2 compatible)
- Multiplexed channels
- Agent forwarding support
- PQC key exchange (hybrid)

### Transport Security
- TLS 1.3 mandatory
- Certificate pinning support
- mTLS for server-to-server
- Sigstore verification for artifacts

## Merge / Rebase

### Merge Strategies
- **ort** (Ostensibly Recursive's Twin): Default, O(n log n)
- **recursive**: Legacy compatibility
- **octopus**: Multi-head merges
- **ours/theirs**: Trivial strategies

### Rebase
- Interactive rebase with conflict resolution
- --update-refs for stacked branches
- Autosquash/fixup automation
- Parallel patch application

## Platform Optimizations

### Linux
- io_uring for async I/O (SQPOLL for submission)
- fanotify for fsmonitor
- Huge pages (2MB/1GB) for packfile mapping
- CPU affinity for worker threads
- eBPF for observability

### Windows
- IOCP for async I/O
- ReadDirectoryChangesW for fsmonitor
- ReFS block cloning for checkout
- Win32 priority classes for QoS
- ETW for tracing

### macOS
- kqueue for async I/O
- FSEvents for fsmonitor
- APFS clonefile for checkout
- Grand Central Dispatch for threading
- os_signpost for tracing

### ARM/Raspberry Pi/Mobile
- NEON SIMD for hash/diff
- Reduced memory footprint mode
- Battery-aware scheduling
- Cross-compilation support

### SIMD Acceleration
| Algorithm | x86_64 | ARM64 |
|-----------|--------|-------|
| SHA-256 | AVX2/SHA-NI | NEON SHA |
| SHA-384/512 | AVX2 | NEON SHA |
| SHA-3/KECCAK | AVX2/AVX512 | NEON |
| LZ4 | AVX2 | NEON |
| zstd | AVX2 | NEON |
| Myers diff | AVX2 | NEON |

## CLI Interface

### Command Compatibility
- Full `git` CLI compatibility
- `fastgit` binary with identical flags
- Alias support via config
- Completion scripts (bash/zsh/fish/powershell)

### Extended Commands
- `fastgit benchmark` - Built-in benchmarking
- `fastgit stats` - Runtime statistics
- `fastgit doctor` - Repository health check
- `fastgit migrate` - Hash algorithm migration
- `fastgit verify` - Cryptographic verification

## Configuration

### File: `.git/fastgit.conf` / `~/.config/fastgit/config`
```ini
[core]
    hashAlgorithm = sha256|sha384|sha3-256|sha3-384|sha3-512
    compression = zstd|lz4|zlib|none
    compressionLevel = 3
    packThreads = auto
    indexThreads = auto
    fsmonitor = true

[performance]
    workerThreads = auto
    ioBackend = io_uring|iocp|kqueue|epoll
    mmapThreshold = 256MB
    cacheSize = 512MB
    loadShedding = true
    priorityClasses = interactive,background,batch

[network]
    protocol = http2|http3|ssh
    tlsVersion = 1.3
    rateLimit = 100000
    windowSize = 65535

[pqc]
    enabled = true
    algorithms = ml-dsa-65,slh-dsa-sha2-128f
    hybrid = true
```

## Benchmarks & Testing

### Required Benchmarks (run on every CI)
1. **Microbenchmarks** (per operation, 1000 iterations)
   - Hash throughput (all algorithms)
   - Object create/lookup/delete
   - Index operations
   - Delta compression

2. **Macrobenchmarks** (realistic workloads)
   - Linux kernel repo (1M+ files, 10M+ commits)
   - Chromium repo (3M+ files)
   - Synthetic monorepo (10M files, 100M commits)
   - AI agent workload (bursty, parallel, no sleep)

3. **Stress Tests**
   - 24-hour soak test
   - OOM handling
   - Network partition simulation
   - Concurrent clone/fetch/push

### Metrics Collection
- Prometheus-compatible `/metrics` endpoint
- OpenTelemetry tracing
- Structured JSON logging
- Per-operation histograms (latency, throughput, errors)

### Repeatability
- Fixed seed for randomized tests
- Docker images for benchmark environment
- Hardware inventory recorded
- CI runs on dedicated hardware (not shared runners)

## Security

### Supply Chain
- Reproducible builds
- SBOM generation (CycloneDX/SPDX)
- Sigstore signing
- SLSA Level 3 compliance

### Runtime
- Capability-based sandboxing
- seccomp-BPF filters (Linux)
- AppContainer (Windows)
- Seatbelt (macOS)

### Cryptography
- Constant-time implementations
- Side-channel resistance
- FIPS 140-3 mode (optional)
- Key zeroization on free

## Future Extensions

### Planned
- Partial clone with promisor remotes
- Commit graph generation
- Changed-path Bloom filters
- FSMonitor daemon
- Distributed object store (S3/GCS/Azure)
- Git LFS integration
- GPG/SSH/Sigstore signing
- Worktree add/remove/prune
- Submodule improvements

### Research
- CRDT-based conflict-free merges
- Learned index structures
- Hardware acceleration (FPGA/ASIC)
- Quantum-resistant object format

## Version History

| Version | Date | Notes |
|---------|------|-------|
| 0.1.0 | 2026-01 | Initial specification |