#include "bloomforge/bloom_filter.hpp"
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << " " #expr "\n"; ++failures; } } while(false)

template<class F> void check_throws(F&& f) {
    try { f(); CHECK(false); } catch (const std::exception&) {}
}
}

int main() {
    using bloomforge::BloomFilter;
    check_throws([] { BloomFilter bad({0, 0.01}); });
    check_throws([] { BloomFilter bad({100, 1.0}); });

    BloomFilter filter({20'000, 0.01});
    for (int i = 0; i < 10'000; ++i) filter.add("key-" + std::to_string(i));
    for (int i = 0; i < 10'000; ++i) CHECK(filter.possibly_contains("key-" + std::to_string(i)));

    std::uint64_t false_positives = 0;
    for (int i = 20'000; i < 30'000; ++i)
        false_positives += filter.possibly_contains("key-" + std::to_string(i)) ? 1ULL : 0ULL;
    CHECK(static_cast<double>(false_positives) / 10'000.0 < 0.04);

    const auto path = std::filesystem::temp_directory_path() / "bloomforge-test.snapshot";
    filter.save(path);
    auto restored = BloomFilter::load(path);
    for (int i = 0; i < 10'000; ++i) CHECK(restored->possibly_contains("key-" + std::to_string(i)));
    std::filesystem::remove(path);

    // Snapshot format v2 is explicitly little-endian and rejects checksum corruption.
    filter.save(path);
    {
        std::ifstream file(path, std::ios::binary);
        std::vector<unsigned char> header(12);
        file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        CHECK(header.size() == 12);
        CHECK(header[8] == 2 && header[9] == 0 && header[10] == 0 && header[11] == 0);
    }
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekg(20);
        char byte{};
        file.read(&byte, 1);
        byte ^= 0x01;
        file.seekp(20);
        file.write(&byte, 1);
    }
    check_throws([&] { (void)BloomFilter::load(path); });
    std::filesystem::remove(path);

    BloomFilter concurrent({50'000, 0.01});
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) workers.emplace_back([t, &concurrent] {
        for (int i = 0; i < 5'000; ++i) concurrent.add("thread-" + std::to_string(t) + "-" + std::to_string(i));
    });
    for (auto& worker : workers) worker.join();
    for (int t = 0; t < 4; ++t) for (int i = 0; i < 5'000; ++i)
        CHECK(concurrent.possibly_contains("thread-" + std::to_string(t) + "-" + std::to_string(i)));

    const auto stats = filter.stats();
    CHECK(stats.bit_count > 0);
    CHECK(stats.memory_bytes > 0);
    CHECK(stats.insertion_attempts == 10'000);
    if (failures == 0) std::cout << "All BloomForge tests passed.\n";
    return failures == 0 ? 0 : 1;
}
