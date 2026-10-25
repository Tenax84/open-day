#include "formats/hash.hpp"

#include <algorithm>
#include <cstring>

namespace openday::formats {
namespace {

std::uint32_t rotl(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

// The Merkle-Damgard buffering shared by the three hashes.
template <typename Self>
void feed(Self& self, std::array<std::uint8_t, 64>& buf, std::uint64_t& length, std::span<const std::uint8_t> data) {
    std::size_t used = static_cast<std::size_t>(length % 64);
    length += data.size();
    std::size_t i = 0;
    if (used) {
        const std::size_t k = std::min<std::size_t>(64 - used, data.size());
        std::memcpy(buf.data() + used, data.data(), k);
        i = k;
        if (used + k < 64) return;
        self(buf.data());
    }
    for (; i + 64 <= data.size(); i += 64) self(data.data() + i);
    if (i < data.size()) std::memcpy(buf.data(), data.data() + i, data.size() - i);
}

// Appends the 0x80 byte, zeros and the bit length (little- or big-endian).
template <typename Self>
void pad(Self& self, std::array<std::uint8_t, 64>& buf, std::uint64_t length, bool bigEndian) {
    std::size_t used = static_cast<std::size_t>(length % 64);
    buf[used++] = 0x80;
    if (used > 56) {
        std::memset(buf.data() + used, 0, 64 - used);
        self(buf.data());
        used = 0;
    }
    std::memset(buf.data() + used, 0, 56 - used);
    const std::uint64_t bits = length * 8;
    for (int i = 0; i < 8; ++i)
        buf[56 + i] = static_cast<std::uint8_t>(bigEndian ? bits >> (56 - 8 * i) : bits >> (8 * i));
    self(buf.data());
}

constexpr std::uint32_t kMd5K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
constexpr int kMd5S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                           14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                           4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

} // namespace

Md5::Md5() : h_{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476} {}

void Md5::block(const std::uint8_t* p) {
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i) m[i] = le32(p + 4 * i);
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        const std::uint32_t t = d;
        d = c;
        c = b;
        b = b + rotl(a + f + kMd5K[i] + m[g], kMd5S[i]);
        a = t;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
}

void Md5::update(std::span<const std::uint8_t> data) {
    auto self = [this](const std::uint8_t* p) { block(p); };
    feed(self, buf_, length_, data);
}

std::array<std::uint8_t, 16> Md5::finish() {
    auto self = [this](const std::uint8_t* p) { block(p); };
    pad(self, buf_, length_, false);
    std::array<std::uint8_t, 16> out;
    for (int i = 0; i < 16; ++i) out[i] = static_cast<std::uint8_t>(h_[i / 4] >> (8 * (i % 4)));
    return out;
}

Sha1::Sha1() : h_{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0} {}

void Sha1::block(const std::uint8_t* p) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = be32(p + 4 * i);
    for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        const std::uint32_t t = rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = t;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
}

void Sha1::update(std::span<const std::uint8_t> data) {
    auto self = [this](const std::uint8_t* p) { block(p); };
    feed(self, buf_, length_, data);
}

std::array<std::uint8_t, 20> Sha1::finish() {
    auto self = [this](const std::uint8_t* p) { block(p); };
    pad(self, buf_, length_, true);
    std::array<std::uint8_t, 20> out;
    for (int i = 0; i < 20; ++i) out[i] = static_cast<std::uint8_t>(h_[i / 4] >> (24 - 8 * (i % 4)));
    return out;
}

std::string toHex(std::span<const std::uint8_t> bytes) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (const std::uint8_t b : bytes) {
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

} // namespace openday::formats
