// sha256.h - SHA-256 (FIPS 180-4), used to check end to end that a file arrived intact.
#pragma once
#include <cstdint>
#include <string>

namespace edc {
std::string sha256Hex(const std::string &data);
class Sha256 {
public:
    Sha256();
    void update(const void *data, size_t len);
    std::string hex();

private:
    void block(const uint8_t *p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t bufLen_ = 0;
    uint64_t total_ = 0;
};
}  // namespace edc
