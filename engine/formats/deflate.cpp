#include "formats/deflate.hpp"

#include <algorithm>
#include <array>

namespace openday::formats {
namespace {

constexpr int kWindow = 32768;
constexpr int kMinMatch = 3, kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr int kMaxChain = 48;

constexpr std::array<int, 29> kLengthBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<int, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<int, 30> kDistBase = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<int, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

class BitWriter {
public:
    explicit BitWriter(std::vector<std::uint8_t>& out) : out_(out) {}

    // `count` bits of `value`, least significant first.
    void bits(std::uint32_t value, int count) {
        acc_ |= value << used_;
        used_ += count;
        while (used_ >= 8) {
            out_.push_back(static_cast<std::uint8_t>(acc_));
            acc_ >>= 8;
            used_ -= 8;
        }
    }
    // A Huffman code, most significant bit first.
    void code(std::uint32_t code, int length) {
        std::uint32_t reversed = 0;
        for (int i = 0; i < length; ++i) reversed = (reversed << 1) | ((code >> i) & 1);
        bits(reversed, length);
    }
    void flush() {
        if (used_ > 0) out_.push_back(static_cast<std::uint8_t>(acc_));
        acc_ = 0;
        used_ = 0;
    }

private:
    std::vector<std::uint8_t>& out_;
    std::uint32_t acc_ = 0;
    int used_ = 0;
};

// The fixed literal / length code (RFC 1951 3.2.6).
void symbol(BitWriter& w, int s) {
    if (s < 144) w.code(static_cast<std::uint32_t>(0x30 + s), 8);
    else if (s < 256) w.code(static_cast<std::uint32_t>(0x190 + s - 144), 9);
    else if (s < 280) w.code(static_cast<std::uint32_t>(s - 256), 7);
    else w.code(static_cast<std::uint32_t>(0xC0 + s - 280), 8);
}

void match(BitWriter& w, int length, int distance) {
    int l = 28;
    while (kLengthBase[static_cast<std::size_t>(l)] > length) --l;
    symbol(w, 257 + l);
    w.bits(static_cast<std::uint32_t>(length - kLengthBase[static_cast<std::size_t>(l)]), kLengthExtra[static_cast<std::size_t>(l)]);
    int d = 29;
    while (kDistBase[static_cast<std::size_t>(d)] > distance) --d;
    w.code(static_cast<std::uint32_t>(d), 5);
    w.bits(static_cast<std::uint32_t>(distance - kDistBase[static_cast<std::size_t>(d)]), kDistExtra[static_cast<std::size_t>(d)]);
}

std::uint32_t hash3(const std::uint8_t* p) {
    const std::uint32_t v = static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16;
    return (v * 2654435761u) >> (32 - kHashBits);
}

} // namespace

std::vector<std::uint8_t> deflate(std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> out;
    out.reserve(data.size() / 4 + 64);
    BitWriter w(out);
    w.bits(1, 1); // last block
    w.bits(1, 2); // fixed Huffman codes

    const int n = static_cast<int>(data.size());
    std::vector<int> head(std::size_t{1} << kHashBits, -1);
    std::vector<int> prev(kWindow, -1);
    auto insert = [&](int pos) {
        if (pos + kMinMatch > n) return;
        const std::uint32_t h = hash3(data.data() + pos);
        prev[static_cast<std::size_t>(pos % kWindow)] = head[h];
        head[h] = pos;
    };

    int pos = 0;
    while (pos < n) {
        int bestLength = 0, bestDistance = 0;
        if (pos + kMinMatch <= n) {
            const int limit = std::min(kMaxMatch, n - pos);
            int candidate = head[hash3(data.data() + pos)];
            for (int chain = 0; candidate >= 0 && pos - candidate <= kWindow && chain < kMaxChain; ++chain) {
                int length = 0;
                while (length < limit && data[static_cast<std::size_t>(candidate + length)] == data[static_cast<std::size_t>(pos + length)])
                    ++length;
                if (length > bestLength) {
                    bestLength = length;
                    bestDistance = pos - candidate;
                    if (length == limit) break;
                }
                const int older = prev[static_cast<std::size_t>(candidate % kWindow)];
                if (older >= candidate) break; // the slot was reused by a newer position
                candidate = older;
            }
        }
        if (bestLength >= kMinMatch) {
            match(w, bestLength, bestDistance);
            for (int i = 0; i < bestLength; ++i) insert(pos + i);
            pos += bestLength;
        } else {
            symbol(w, data[static_cast<std::size_t>(pos)]);
            insert(pos);
            ++pos;
        }
    }
    symbol(w, 256); // end of block
    w.flush();
    return out;
}

} // namespace openday::formats
