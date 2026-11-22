# BloomForge v2 scope

This iteration focuses on evidence-driven performance evaluation rather than adding multiple data structures before the core is measured.

## Delivered

- Side-by-side insertion and positive lookup benchmark against `std::unordered_set<std::string>`.
- Negative-query workload over a disjoint key range and observed false-positive calculation.
- Configurable item count and target false-positive probability.
- Explicit distinction between exact Bloom storage and rough hash-set memory estimation.
- Benchmark methodology and sanitizer build documentation.
- CMake option for AddressSanitizer + UndefinedBehaviorSanitizer on compatible GCC/Clang toolchains.

## Known limitations to resolve before production adoption

- The current snapshot encoding writes native representations; a portable, explicitly endian-defined wire format should be implemented before exchanging snapshots across architectures or compiler environments.
- Saving while writers are active does not provide a globally consistent point-in-time snapshot. Quiesce writers or introduce snapshot coordination before relying on persistence for recovery.
- The built-in non-cryptographic hash functions are not designed to resist adversarially selected keys. Use a keyed hash for hostile-input environments.
- `std::atomic<uint64_t>` lock-freedom is platform-dependent; do not assume lock-free behavior on every target.
- The benchmark is a microbenchmark. It is not a substitute for application-level profiling or allocator-level memory measurement.

## Next engineering milestones

1. Portable serialization format with explicit byte order and stronger format validation.
2. Snapshot consistency contract and crash-durability design.
3. Configurable keyed hashing strategy.
4. ThreadSanitizer runs and stress tests on supported platforms.
5. Cache-aware and SIMD experiments, each guarded by correctness tests and benchmark evidence.
6. Scalable and counting variants only after the base implementation has stable correctness and performance baselines.
