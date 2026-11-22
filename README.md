# BloomForge v3

A C++20 Bloom Filter library focused on predictable sizing, compact storage, concurrent access, persistence, and measurable behavior.

## Features

- Optimal Bloom filter sizing from expected insertions and target false-positive probability.
- Packed 64-bit atomic words; concurrent `add` and `possibly_contains` operations.
- Double hashing to derive the bit positions from two 64-bit hashes.
- Versioned binary snapshots with CRC-32 integrity validation.
- Runtime statistics: memory, set-bit utilization, query count, insertion attempts, and estimated FPP.
- CMake build, tests, example, and a simple throughput benchmark.

## Build (Windows / MSYS2 UCRT64)

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/bloomforge_example.exe
./build/bloomforge_benchmark.exe
```

On Linux/macOS, use your normal C++20 compiler and CMake generator.

## Quick start

```cpp
#include <bloomforge/bloom_filter.hpp>

int main() {
    bloomforge::BloomFilter users({1'000'000, 0.01});
    users.add("user:42");

    if (users.possibly_contains("user:42")) {
        // It may exist; verify against the source of truth.
    }

    users.save("users.bloom");
    auto restored = bloomforge::BloomFilter::load("users.bloom");
}
```

## Semantics

- `false` means the value is definitely absent from the values inserted into this filter.
- `true` means the value may be present; false positives are possible.
- A standard Bloom filter does not support deletion. Repeated insertions are safe but increase occupancy.
- The configured FPP is a design target at the expected capacity, not a hard upper bound. Inserting substantially more than the configured capacity increases FPP.
- `insertion_attempts` counts calls, including duplicate values; it is not a unique-element count.

## Thread-safety

Concurrent calls to `add` and `possibly_contains` are safe. Atomic bit updates prevent lost updates. A snapshot taken while writers are active is a valid point-in-time-ish collection of independently loaded words, but it is not a globally linearizable transaction. Quiesce writers before `save` if a consistent application-level snapshot is required.

## Persistence notes

Snapshots are versioned and checksummed. The current implementation writes a temporary sibling file and renames it into place. CRC-32 detects accidental corruption; it is not cryptographic authentication. Treat snapshot files as trusted local data. The binary format currently uses the host's native byte representation, so snapshots are intended for the same architecture/endianness and compatible version of the library.

## Hashing and security

The built-in hashes are non-cryptographic. BloomForge is designed for probabilistic membership checks, not adversarially controlled keys or security decisions. If attackers can deliberately choose inputs, use a keyed hash implementation and manage the key securely.

## Complexity

For `k` hash probes: insertion and lookup are O(k), and storage is O(m) bits. With optimal sizing, `m = -n ln(p) / (ln 2)^2` and `k = (m/n) ln 2`.

## Project layout

- `include/bloomforge/` public API
- `src/` implementation
- `tests/` correctness, persistence, concurrency, and FPP smoke tests
- `benchmarks/` throughput benchmark
- `examples/` usage example
- `docs/` design notes

## Limitations / future extensions

Scalable Bloom filters, deletion via counting filters, memory-mapped storage, keyed hashing, cross-endian serialization, and a formally linearizable snapshot protocol are intentionally outside v1.0.

## Benchmark against `std::unordered_set`

The benchmark accepts an item count and target false-positive probability:

```bash
./build/bloomforge_benchmark 500000 0.01
```

It reports insertion and successful-lookup throughput for both implementations, Bloom negative-lookup throughput, observed false positives on disjoint keys, Bloom bit-storage bytes, and a labelled rough memory estimate for `std::unordered_set`. The hash-set estimate is not an exact allocator measurement. See [docs/BENCHMARKING.md](docs/BENCHMARKING.md) for a repeatable measurement procedure and sanitizer instructions.

## Sanitizer build

With a compatible GCC or Clang toolchain:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DBLOOMFORGE_ENABLE_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```


## v3 engineering upgrades

- Snapshot format v2 uses explicitly little-endian fields rather than native host byte order.
- `save()` takes an exclusive snapshot gate while writers take shared access, so a saved image represents a coherent point in time while concurrent insertions remain mutually concurrent.
- Snapshot loading validates configuration and rejects non-zero padding bits outside the declared bit range.
- Snapshot format versioning intentionally rejects older incompatible v1 snapshots rather than guessing their byte order.
- Thread-safety contract: concurrent `add()` calls are supported; `possibly_contains()` may run concurrently with inserts and can observe an in-progress insertion; callers must treat a positive result as advisory. `save()` waits for active inserts and blocks new inserts during the snapshot copy.

`memory_bytes` reports the atomic bit-array payload only; it excludes object metadata, allocator overhead, and counters.
