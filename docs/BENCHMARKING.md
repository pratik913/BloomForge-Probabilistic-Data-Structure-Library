# BloomForge Benchmarking Guide

## What is measured

The benchmark compares BloomForge with `std::unordered_set<std::string>` using the same inserted key set:

- insertion throughput;
- successful lookup throughput;
- negative lookup throughput;
- observed false-positive rate on a disjoint key range;
- Bloom filter storage bytes;
- a clearly labelled rough `unordered_set` memory estimate.

The `unordered_set` estimate is not an allocator measurement. Node layout, bucket policy, string capacity, allocator metadata and standard-library implementation affect its real footprint. Do not present this estimate as exact memory usage.

## Reproducible procedure

Build in Release mode and run on an otherwise idle machine:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBLOOMFORGE_BUILD_TESTS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
./build-release/bloomforge_benchmark 500000 0.01
./build-release/bloomforge_benchmark 1000000 0.01
```

On Windows multi-configuration generators, add `--config Release` to build and test commands and run the executable from `build-release/Release/` if generated there.

Run each case at least five times, report the median, and record CPU, OS, compiler/version, standard library, build flags, item count and target FPP. Compare implementations on the same machine and with the same key corpus. Avoid comparing Debug results with Release results.

## Interpretation

A Bloom filter trades exact membership and deletion support for compact storage and false positives. It can answer “definitely absent” or “possibly present”; a positive must be checked against the authoritative data store. It is not expected to beat a hash set on every workload: hash cost, number of probes, cache behavior, allocator, and workload shape matter.

## Sanitizers

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DBLOOMFORGE_ENABLE_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

AddressSanitizer and UndefinedBehaviorSanitizer are supported with compatible GCC/Clang toolchains. ThreadSanitizer should be run in a separate build because it is generally incompatible with AddressSanitizer; configure manually with `-fsanitize=thread` using a compatible compiler.
