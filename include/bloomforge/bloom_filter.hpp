#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace bloomforge {

struct BloomFilterConfig {
    std::uint64_t expected_insertions{100'000};
    double false_positive_probability{0.01};
};

struct BloomFilterStats {
    std::uint64_t insertion_attempts{};
    std::uint64_t query_count{};
    std::uint64_t bit_count{};
    std::uint64_t hash_function_count{};
    std::uint64_t set_bit_count{};
    double configured_false_positive_probability{};
    double estimated_false_positive_probability{};
    double bit_utilization{};
    std::size_t memory_bytes{};
};

class BloomFilter final {
public:
    explicit BloomFilter(BloomFilterConfig config);
    ~BloomFilter() = default;

    BloomFilter(const BloomFilter&) = delete;
    BloomFilter& operator=(const BloomFilter&) = delete;
    BloomFilter(BloomFilter&&) = delete;
    BloomFilter& operator=(BloomFilter&&) = delete;

    // The byte sequence is hashed as-is. For strings, use add(string_view).
    void add(std::span<const std::byte> value);
    [[nodiscard]] bool possibly_contains(std::span<const std::byte> value) const;
    void add(std::string_view value);
    [[nodiscard]] bool possibly_contains(std::string_view value) const;

    [[nodiscard]] BloomFilterStats stats() const noexcept;
    [[nodiscard]] BloomFilterConfig config() const noexcept { return config_; }

    // Saves a versioned, checksummed snapshot. Writes to a temporary file first.
    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static std::unique_ptr<BloomFilter> load(const std::filesystem::path& path);

private:
    struct RawTag {};
    BloomFilter(BloomFilterConfig config, std::uint64_t bit_count,
                std::uint64_t hash_count, RawTag);

    [[nodiscard]] static std::uint64_t optimal_bit_count(std::uint64_t n, double p);
    [[nodiscard]] static std::uint64_t optimal_hash_count(std::uint64_t m, std::uint64_t n);
    [[nodiscard]] static std::uint64_t hash1(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] static std::uint64_t hash2(std::span<const std::byte> bytes) noexcept;

    BloomFilterConfig config_;
    std::uint64_t bit_count_{};
    std::uint64_t hash_count_{};
    std::size_t word_count_{};
    std::unique_ptr<std::atomic<std::uint64_t>[]> words_;
    // Shared by writers; save() takes exclusive ownership for a coherent snapshot.
    mutable std::shared_mutex snapshot_mutex_;
    mutable std::atomic<std::uint64_t> insertion_attempts_{0};
    mutable std::atomic<std::uint64_t> query_count_{0};
};

} // namespace bloomforge
