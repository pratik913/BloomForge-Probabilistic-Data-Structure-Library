#include "bloomforge/bloom_filter.hpp"
#include <iostream>

int main() {
    bloomforge::BloomFilter filter({50'000, 0.01});
    filter.add("user:1001");
    filter.add("user:1002");

    std::cout << std::boolalpha;
    std::cout << "user:1001 possibly exists: " << filter.possibly_contains("user:1001") << '\n';
    std::cout << "user:9999 possibly exists: " << filter.possibly_contains("user:9999") << '\n';

    const auto s = filter.stats();
    std::cout << "Memory: " << s.memory_bytes << " bytes\n"
              << "Estimated FPP: " << s.estimated_false_positive_probability << '\n';
    filter.save("users.bloom");
    auto restored = bloomforge::BloomFilter::load("users.bloom");
    std::cout << "Restored user:1001: " << restored->possibly_contains("user:1001") << '\n';
}
