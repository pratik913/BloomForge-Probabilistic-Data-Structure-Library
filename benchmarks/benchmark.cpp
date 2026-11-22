#include "bloomforge/bloom_filter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Measurement {
    double milliseconds{};
    double operations_per_second{};
};

template <typename Function>
Measurement measure(std::uint64_t operations, Function&& function) {
    const auto start = Clock::now();
    function();
    const auto finish = Clock::now();
    const auto ms = std::chrono::duration<double, std::milli>(finish - start).count();
    return {ms, ms == 0.0 ? 0.0 : static_cast<double>(operations) * 1000.0 / ms};
}

std::string key_for(std::uint64_t i) { return "bf-key-" + std::to_string(i); }

void run(std::uint64_t count, double target_fpp) {
    std::vector<std::string> inserted;
    inserted.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) inserted.push_back(key_for(i));

    bloomforge::BloomFilter bloom({count, target_fpp});
    std::unordered_set<std::string> exact;
    exact.reserve(static_cast<std::size_t>(count));

    const auto bloom_insert = measure(count, [&] {
        for (const auto& key : inserted) bloom.add(key);
    });
    const auto exact_insert = measure(count, [&] {
        for (const auto& key : inserted) exact.insert(key);
    });

    std::uint64_t bloom_hits = 0;
    const auto bloom_lookup = measure(count, [&] {
        for (const auto& key : inserted) bloom_hits += bloom.possibly_contains(key) ? 1ULL : 0ULL;
    });
    std::uint64_t exact_hits = 0;
    const auto exact_lookup = measure(count, [&] {
        for (const auto& key : inserted) exact_hits += exact.contains(key) ? 1ULL : 0ULL;
    });

    // Use a disjoint key range to measure observed false positives.
    std::uint64_t false_positives = 0;
    const auto negative_queries = std::max<std::uint64_t>(count, 10'000);
    const auto negative_lookup = measure(negative_queries, [&] {
        for (std::uint64_t i = 0; i < negative_queries; ++i)
            false_positives += bloom.possibly_contains(key_for(count + i)) ? 1ULL : 0ULL;
    });

    const auto stats = bloom.stats();
    const auto exact_memory_estimate = exact.size() * (sizeof(std::string) + sizeof(void*) * 2U + 32U);
    const double observed_fpp = static_cast<double>(false_positives) / static_cast<double>(negative_queries);

    std::cout << "\n=== BloomForge benchmark ===\n"
              << "C++ language mode: " << __cplusplus << "\n"
              << "Inserted keys: " << count << "\n"
              << "Target FPP: " << target_fpp << "\n"
              << "Bloom bits: " << stats.bit_count << " | hashes: " << stats.hash_function_count << "\n"
              << "Bloom storage: " << stats.memory_bytes << " bytes\n"
              << "unordered_set rough estimate: " << exact_memory_estimate << " bytes (not allocator-measured)\n"
              << std::fixed << std::setprecision(3)
              << "\nInsert throughput (ops/s)\n"
              << "  BloomForge:       " << bloom_insert.operations_per_second << " (" << bloom_insert.milliseconds << " ms)\n"
              << "  unordered_set:    " << exact_insert.operations_per_second << " (" << exact_insert.milliseconds << " ms)\n"
              << "\nPositive lookup throughput (ops/s)\n"
              << "  BloomForge:       " << bloom_lookup.operations_per_second << " (" << bloom_lookup.milliseconds << " ms), hits=" << bloom_hits << "\n"
              << "  unordered_set:    " << exact_lookup.operations_per_second << " (" << exact_lookup.milliseconds << " ms), hits=" << exact_hits << "\n"
              << "\nNegative Bloom lookups: " << negative_queries << "\n"
              << "  False positives:  " << false_positives << "\n"
              << "  Observed FPP:     " << observed_fpp << "\n"
              << "  Estimated FPP:    " << stats.estimated_false_positive_probability << "\n"
              << "  Negative query throughput: " << negative_lookup.operations_per_second << " ops/s\n"
              << "\nNote: results depend on compiler, CPU, allocator, key distribution and build flags.\n"
              << "Run multiple times and compare Release builds on the same machine.\n";
}
} // namespace

int main(int argc, char** argv) {
    const std::uint64_t count = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 500'000ULL;
    const double fpp = argc > 2 ? std::strtod(argv[2], nullptr) : 0.01;
    if (count == 0 || fpp <= 0.0 || fpp >= 1.0) {
        std::cerr << "Usage: bloomforge_benchmark [item_count>0] [false_positive_probability in (0,1)]\n";
        return EXIT_FAILURE;
    }
    run(count, fpp);
}
