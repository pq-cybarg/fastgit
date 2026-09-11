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

| Operation | Measured | Notes |
|-----------|----------|-------|
| `hash-object` (SHA-256) | ~1.4 GB/s | OpenSSL EVP when available (`FASTGIT_HAVE_OPENSSL=1`) |
| ODB write (hot cache, same blob) | ~1.2M ops/s | 16K 8-probe cache hit, no serialize/write |
| ODB write (durable distinct) | ~3.5k ops/s | `tmp.<pid> + fsync + rename + dir fsync` per loose `ab/cdef` (was `~10k` before `O_TRUNC` → now crash-safe) |
| ODB write (pack batch, 10k/pack) | ~8.5k ops/s | single `pack-*.pack + .idx` `1 fsync` amortized |
| ODB read (hot / distinct) | ~27M / ~2.0M ops/s | cache vs loose zlib inflate; `pack mmap` ~0.95M |
| `add` 1000 files (durable, bulk) | ~6.4k ops/s | `index_add` Git-shaped `blob header + odb_write + replace (path,stage)` — vs `git add` `~1.1k` on same tree (`5.8×`) |
| index add/find/remove | ~3.0M / 3.2M / 5.5M ops/s | in-memory; bulk add with hashing |

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

## Deployment

Everywhere `git` is published, `fastgit` is published (C23 baremetal — no cargo/npm/pip runtime; wrappers are `fastgit-sys` / bindings).

| Channel | Package | Install |
|---|---|---|
| **GitHub** | `pq-cybarg/fastgit` | `gh release` `v0.1.1` — `fastgit-v0.1.1-src.tar.gz (08203b5…)` `fastgit-v0.1.0-macos-arm64.tar.gz` at https://github.com/pq-cybarg/fastgit/releases |
| **Homebrew** | `pq-cybarg/homebrew-fastgit` | `brew tap pq-cybarg/fastgit && brew install fastgit` — `Formula/fastgit.rb` `cmake -S . -B build -DFASTGIT_ENABLE_LTO=ON && cmake --build build && cmake --install build` `depends_on \"cmake\"/\"openssl@3\"/\"zlib\"` |
| **Arch AUR** | `fastgit` | `makepkg -si` from `packaging/arch/PKGBUILD` (`source v0.1.1.tar.gz`, `arch x86_64/aarch64`, `depends openssl zlib curl libssh`, `makedepends cmake`) — `build() cmake -S fastgit-v0.1.1 -B build -DCMAKE_BUILD_TYPE=Release -DFASTGIT_ENABLE_LTO=ON`, `check() ctest`, `package() DESTDIR=\"$pkgdir\" cmake --install build` — `src/CMakeLists.txt:104` now `include(GNUInstallDirs)` + `install(TARGETS fastgit fastgit_static fastgit-cli)` so `cmake --install` works; Darwin cannot run `makepkg` (no `pacman`), `archlinux:latest` container blocked by `seccomp 22 alpm` — verified via mocked `cmake -S /tmp/pkgbuild-test/fastgit-v0.1.1 -B build` → `10/10 2.06s` + `DESTDIR=/tmp/fgtest-install` installs `libfastgit.0.1.0.dylib` `libfastgit.a` `include/fastgit/*.h` `bin/fastgit-cli 33K` |
| **Cargo** | `fastgit-sys 0.1.1` | `packaging/cargo/{Cargo.toml,build.rs,src/lib.rs}` `links=fastgit` `build-dependencies cmake/pkg-config 0.3` — `cargo publish --dry-run --allow-dirty` ok (`pkg-config 0.5` → `0.3` fix); note `build.rs` canonicalizes `CARGO_MANIFEST_DIR/../../` to repo root — `cargo verify` isolates package temp without `CMakeLists.txt` so dry-run warns but real publish uses git source |
| **npm** | `fastgit 0.1.1` | `packaging/npm/{package.json,binding.gyp,fastgit.cc,index.js}` `cargo publish --dry-run` → `packaging/npm: npm publish --dry-run` `1.7kB 5 files shasum 5d7bf…` `npm pack` verified |
| **PyPI** | `fastgit 0.1.1` | `packaging/pypi/{pyproject.toml,fastgit/__init__.py}` `[project.urls] Homepage/Repository` (fixed `homepage`/`repository` invalid) — `python3 -m build --wheel` → `fastgit-0.1.1-py3-none-any.whl 1.7K` `twine check PASSED` (`ctypes` wrapper, `fastgit-cli` expected in `PATH`) |
| **Debian** | `fastgit` | `packaging/debian/{control,rules,changelog,compat}` `dpkg-buildpackage -b` cmake build ctest |
| **Scoop** | `fastgit` | `packaging/scoop/fastgit.json` `checkver`/`autoupdate` from GitHub releases |
| **Docker** | `packaging/docker/Dockerfile.ubuntu` | `docker build -f packaging/docker/Dockerfile.ubuntu .` `ubuntu:22.04` `cmake -S . -B build && cmake --build build && ctest` `10/10 0.07s` |

Portability: `FASTGIT_HAVE_IO_URING` conditional on `liburing.h` (`pkg_check_modules` + `CheckIncludeFile`) — Linux without `liburing` builds without `io_uring` (`ubuntu:22.04` `10/10 0.07s` vs previous `fatal error: liburing.h`). Hash-agile `20/32/48/64` (`index.h:oid[64]`, `pack hash_len 20/32/48/64 loop`) preserves GitHub `sha1` compat (`a6dff50`) while default `sha256`.

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