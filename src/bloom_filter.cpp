#include "bloomforge/bloom_filter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <bit>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace bloomforge {
namespace {
constexpr std::uint64_t kMagic = 0x424C4F4F4D464F52ULL; // BLOOMFOR
constexpr std::uint32_t kFormatVersion = 2;
constexpr std::uint64_t kMaxBits = 8ULL * 1024ULL * 1024ULL * 1024ULL;

std::uint64_t mix64(std::uint64_t x) noexcept {
    x ^= x >> 30U; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27U; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31U);
}

std::uint32_t crc32(std::span<const std::byte> data) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : data) {
        crc ^= static_cast<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

template <typename T>
void append_le(std::vector<std::byte>& out, T value) {
    static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
    using Bits = std::conditional_t<std::is_floating_point_v<T>,
        std::conditional_t<sizeof(T) == 8, std::uint64_t, std::uint32_t>, T>;
    const Bits bits = [&]() {
        if constexpr (std::is_floating_point_v<T>) return std::bit_cast<Bits>(value);
        else return static_cast<Bits>(value);
    }();
    using Unsigned = std::make_unsigned_t<Bits>;
    const auto raw = static_cast<Unsigned>(bits);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        out.push_back(static_cast<std::byte>((raw >> (i * 8U)) & 0xFFU));
}

template <typename T>
T read_le(std::span<const std::byte> bytes, std::size_t& offset) {
    static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T))
        throw std::runtime_error("BloomForge: truncated snapshot");
    using Bits = std::conditional_t<std::is_floating_point_v<T>,
        std::conditional_t<sizeof(T) == 8, std::uint64_t, std::uint32_t>, T>;
    using Unsigned = std::make_unsigned_t<Bits>;
    Unsigned raw{};
    for (std::size_t i = 0; i < sizeof(T); ++i)
        raw |= static_cast<Unsigned>(static_cast<std::uint8_t>(bytes[offset++])) << (i * 8U);
    const Bits bits = static_cast<Bits>(raw);
    if constexpr (std::is_floating_point_v<T>) return std::bit_cast<T>(bits);
    else return static_cast<T>(bits);
}
} // namespace

BloomFilter::BloomFilter(BloomFilterConfig config)
    : BloomFilter(config, optimal_bit_count(config.expected_insertions, config.false_positive_probability),
                  optimal_hash_count(optimal_bit_count(config.expected_insertions, config.false_positive_probability),
                                      config.expected_insertions), RawTag{}) {}

BloomFilter::BloomFilter(BloomFilterConfig config, std::uint64_t bit_count,
                         std::uint64_t hash_count, RawTag)
    : config_(config), bit_count_(bit_count), hash_count_(hash_count),
      word_count_(static_cast<std::size_t>((bit_count + 63ULL) / 64ULL)),
      words_(std::make_unique<std::atomic<std::uint64_t>[]>(word_count_)) {
    if (config_.expected_insertions == 0 || !std::isfinite(config_.false_positive_probability) ||
        config_.false_positive_probability <= 0.0 || config_.false_positive_probability >= 1.0)
        throw std::invalid_argument("BloomForge: expected_insertions must be > 0 and FPP must be in (0, 1)");
    if (bit_count_ == 0 || bit_count_ > kMaxBits || hash_count_ == 0 || hash_count_ > 64)
        throw std::invalid_argument("BloomForge: invalid or excessive filter dimensions");
    for (std::size_t i = 0; i < word_count_; ++i) words_[i].store(0, std::memory_order_relaxed);
}

std::uint64_t BloomFilter::optimal_bit_count(std::uint64_t n, double p) {
    if (n == 0 || !std::isfinite(p) || p <= 0.0 || p >= 1.0)
        throw std::invalid_argument("BloomForge: invalid configuration");
    const long double bits = -static_cast<long double>(n) * std::log(static_cast<long double>(p)) /
                             (std::log(2.0L) * std::log(2.0L));
    if (!std::isfinite(static_cast<double>(bits)) || bits > static_cast<long double>(kMaxBits))
        throw std::length_error("BloomForge: requested filter exceeds maximum supported size");
    return std::max<std::uint64_t>(64, static_cast<std::uint64_t>(std::ceil(bits)));
}

std::uint64_t BloomFilter::optimal_hash_count(std::uint64_t m, std::uint64_t n) {
    const auto k = static_cast<std::uint64_t>(std::llround((static_cast<long double>(m) / n) * std::log(2.0L)));
    return std::clamp<std::uint64_t>(k, 1, 64);
}

std::uint64_t BloomFilter::hash1(std::span<const std::byte> bytes) noexcept {
    std::uint64_t h = 14695981039346656037ULL;
    for (const auto b : bytes) { h ^= static_cast<std::uint8_t>(b); h *= 1099511628211ULL; }
    return mix64(h ^ static_cast<std::uint64_t>(bytes.size()));
}

std::uint64_t BloomFilter::hash2(std::span<const std::byte> bytes) noexcept {
    std::uint64_t h = 0x9e3779b97f4a7c15ULL ^ static_cast<std::uint64_t>(bytes.size());
    for (const auto b : bytes) h = mix64(h ^ static_cast<std::uint8_t>(b));
    return mix64(h) | 1ULL; // odd step avoids a zero increment
}

void BloomFilter::add(std::span<const std::byte> value) {
    std::shared_lock snapshot_guard(snapshot_mutex_);
    insertion_attempts_.fetch_add(1, std::memory_order_relaxed);
    const auto h1 = hash1(value), h2 = hash2(value);
    for (std::uint64_t i = 0; i < hash_count_; ++i) {
        const auto bit = (h1 + i * h2) % bit_count_;
        const auto mask = 1ULL << (bit & 63ULL);
        words_[static_cast<std::size_t>(bit >> 6U)].fetch_or(mask, std::memory_order_relaxed);
    }
}

bool BloomFilter::possibly_contains(std::span<const std::byte> value) const {
    query_count_.fetch_add(1, std::memory_order_relaxed);
    const auto h1 = hash1(value), h2 = hash2(value);
    for (std::uint64_t i = 0; i < hash_count_; ++i) {
        const auto bit = (h1 + i * h2) % bit_count_;
        const auto mask = 1ULL << (bit & 63ULL);
        if ((words_[static_cast<std::size_t>(bit >> 6U)].load(std::memory_order_relaxed) & mask) == 0)
            return false;
    }
    return true;
}

void BloomFilter::add(std::string_view value) {
    add(std::as_bytes(std::span(value.data(), value.size())));
}
bool BloomFilter::possibly_contains(std::string_view value) const {
    return possibly_contains(std::as_bytes(std::span(value.data(), value.size())));
}

BloomFilterStats BloomFilter::stats() const noexcept {
    std::uint64_t set_bits = 0;
    for (std::size_t i = 0; i < word_count_; ++i)
        set_bits += static_cast<std::uint64_t>(std::popcount(words_[i].load(std::memory_order_relaxed)));
    const double utilization = bit_count_ == 0 ? 0.0 : static_cast<double>(set_bits) / static_cast<double>(bit_count_);
    const double estimated = std::pow(utilization, static_cast<double>(hash_count_));
    return {insertion_attempts_.load(std::memory_order_relaxed), query_count_.load(std::memory_order_relaxed),
            bit_count_, hash_count_, set_bits, config_.false_positive_probability, estimated,
            utilization, word_count_ * sizeof(std::uint64_t)};
}

void BloomFilter::save(const std::filesystem::path& path) const {
    // Exclude writers while collecting all words, yielding a coherent point-in-time snapshot.
    std::unique_lock snapshot_guard(snapshot_mutex_);
    std::vector<std::byte> bytes;
    bytes.reserve(48 + word_count_ * sizeof(std::uint64_t));
    append_le(bytes, kMagic); append_le(bytes, kFormatVersion);
    append_le(bytes, config_.expected_insertions); append_le(bytes, config_.false_positive_probability);
    append_le(bytes, bit_count_); append_le(bytes, hash_count_);
    for (std::size_t i = 0; i < word_count_; ++i) append_le(bytes, words_[i].load(std::memory_order_relaxed));
    append_le(bytes, crc32(bytes));

    auto temp = path; temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("BloomForge: cannot open temporary snapshot for writing");
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) { out.close(); std::filesystem::remove(temp); throw std::runtime_error("BloomForge: snapshot write failed"); }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec); ec.clear();
        std::filesystem::rename(temp, path, ec);
        if (ec) { std::filesystem::remove(temp); throw std::runtime_error("BloomForge: cannot publish snapshot: " + ec.message()); }
    }
}

std::unique_ptr<BloomFilter> BloomFilter::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("BloomForge: cannot open snapshot");
    const auto end = in.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > (kMaxBits / 8ULL + 128ULL))
        throw std::runtime_error("BloomForge: invalid snapshot size");
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    in.seekg(0); in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in || bytes.size() < 52) throw std::runtime_error("BloomForge: truncated snapshot");

    std::size_t offset = 0;
    const auto magic = read_le<std::uint64_t>(bytes, offset);
    const auto version = read_le<std::uint32_t>(bytes, offset);
    if (magic != kMagic || version != kFormatVersion) throw std::runtime_error("BloomForge: unsupported snapshot format");
    BloomFilterConfig config;
    config.expected_insertions = read_le<std::uint64_t>(bytes, offset);
    config.false_positive_probability = read_le<double>(bytes, offset);
    const auto bit_count = read_le<std::uint64_t>(bytes, offset);
    const auto hash_count = read_le<std::uint64_t>(bytes, offset);
    if (bit_count == 0 || bit_count > kMaxBits || hash_count == 0 || hash_count > 64)
        throw std::runtime_error("BloomForge: invalid dimensions in snapshot");
    const auto expected_words = static_cast<std::size_t>((bit_count + 63ULL) / 64ULL);
    const auto expected_size = offset + expected_words * sizeof(std::uint64_t) + sizeof(std::uint32_t);
    if (bytes.size() != expected_size) throw std::runtime_error("BloomForge: snapshot length does not match dimensions");
    std::size_t crc_offset = bytes.size() - sizeof(std::uint32_t);
    std::size_t crc_read_offset = crc_offset;
    const auto stored_crc = read_le<std::uint32_t>(bytes, crc_read_offset);
    if (crc32(std::span<const std::byte>(bytes.data(), crc_offset)) != stored_crc)
        throw std::runtime_error("BloomForge: snapshot checksum mismatch");

    if (config.expected_insertions == 0 || !std::isfinite(config.false_positive_probability) ||
        config.false_positive_probability <= 0.0 || config.false_positive_probability >= 1.0)
        throw std::runtime_error("BloomForge: invalid configuration in snapshot");

    auto filter = std::unique_ptr<BloomFilter>(new BloomFilter(config, bit_count, hash_count, RawTag{}));
    for (std::size_t i = 0; i < expected_words; ++i)
        filter->words_[i].store(read_le<std::uint64_t>(bytes, offset), std::memory_order_relaxed);

    // Reject non-canonical snapshots with set bits beyond the declared bit count.
    const auto remainder = bit_count % 64ULL;
    if (remainder != 0) {
        const auto valid_mask = (1ULL << remainder) - 1ULL;
        const auto last_word = filter->words_[expected_words - 1].load(std::memory_order_relaxed);
        if ((last_word & ~valid_mask) != 0)
            throw std::runtime_error("BloomForge: non-zero padding bits in snapshot");
    }
    return filter;
}

} // namespace bloomforge
