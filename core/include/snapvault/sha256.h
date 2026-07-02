#ifndef SNAPVAULT_SHA256_H
#define SNAPVAULT_SHA256_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace snapvault {

// Compact from-scratch SHA-256 (FIPS 180-4). No external dependencies.
class Sha256 {
public:
    Sha256();
    void update(const uint8_t* data, size_t len);
    void update(const std::string& data);
    // Finalizes the digest and returns the lowercase hex string (64 chars).
    std::string hexdigest();

    // Convenience: hash a whole buffer to a hex string.
    static std::string hex(const uint8_t* data, size_t len);
    static std::string hex(const std::string& data);

private:
    void transform(const uint8_t* chunk);

    uint32_t state_[8];
    uint64_t bitlen_;
    uint8_t buffer_[64];
    size_t bufferlen_;
};

}  // namespace snapvault

#endif  // SNAPVAULT_SHA256_H
