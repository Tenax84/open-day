// MD5 and SHA-1 (RFC 1321, FIPS 180-4): checksums Inno Setup installers
// keep for every stored file (docs/formats/INNO.md). SHA-256 is in
// formats/sha256.hpp.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace openday::formats {

class Md5 {
public:
    Md5();
    void update(std::span<const std::uint8_t> data);
    std::array<std::uint8_t, 16> finish();

private:
    void block(const std::uint8_t* p);
    std::array<std::uint32_t, 4> h_;
    std::array<std::uint8_t, 64> buf_{};
    std::uint64_t length_ = 0;
};

class Sha1 {
public:
    Sha1();
    void update(std::span<const std::uint8_t> data);
    std::array<std::uint8_t, 20> finish();

private:
    void block(const std::uint8_t* p);
    std::array<std::uint32_t, 5> h_;
    std::array<std::uint8_t, 64> buf_{};
    std::uint64_t length_ = 0;
};

// Lower-case hexadecimal.
std::string toHex(std::span<const std::uint8_t> bytes);

} // namespace openday::formats
