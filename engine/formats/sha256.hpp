// SHA-256 (FIPS 180-4), used to verify downloaded update packages.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace openday::formats {

class Sha256 {
public:
    Sha256();
    void update(std::span<const std::uint8_t> data);
    std::array<std::uint8_t, 32> finish();
    // finish() as 64 lower-case hex digits.
    std::string finishHex();

private:
    void block(const std::uint8_t* p);

    std::array<std::uint32_t, 8> h_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t used_ = 0;
    std::uint64_t length_ = 0;
};

std::string sha256Hex(std::span<const std::uint8_t> data);

} // namespace openday::formats
