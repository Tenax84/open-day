#include "formats/inflate.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace openday::formats {
namespace {

constexpr int kMaxBits = 15;
constexpr int kMaxLitLen = 288;
constexpr int kMaxDist = 30;

constexpr int kFastBits = 9;

// Canonical Huffman code as counts per length plus symbols in code order.
// Codes of up to kFastBits bits are decoded with one table lookup (`fast`:
// symbol | length << 12, indexed by the next bits of the stream); longer
// codes one bit at a time.
struct Huffman {
    std::array<std::uint16_t, kMaxBits + 1> count{};
    std::array<std::uint16_t, kMaxLitLen> symbol{};
    std::array<std::uint16_t, 1 << kFastBits> fast{};

    // Builds the code from code lengths. Incomplete codes are allowed (the
    // format permits them for single-symbol distance codes); over-subscribed
    // codes are rejected.
    void build(const std::uint8_t* lengths, int n) {
        count.fill(0);
        for (int i = 0; i < n; ++i) ++count[lengths[i]];
        if (count[0] == n) return; // no codes: decoding any symbol fails
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) throw InflateError("over-subscribed Huffman code");
        }
        std::array<std::uint16_t, kMaxBits + 1> offs{};
        for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = static_cast<std::uint16_t>(offs[len] + count[len]);
        for (int i = 0; i < n; ++i)
            if (lengths[i] != 0) symbol[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);

        fast.fill(0);
        int code = 0, index = 0;
        for (int len = 1; len <= kFastBits; ++len) {
            for (int k = 0; k < count[len]; ++k, ++code, ++index) {
                int reversed = 0; // the stream sends codes most significant bit first
                for (int b = 0; b < len; ++b) reversed |= ((code >> b) & 1) << (len - 1 - b);
                for (int fill = reversed; fill < (1 << kFastBits); fill += 1 << len)
                    fast[static_cast<std::size_t>(fill)] = static_cast<std::uint16_t>(symbol[static_cast<std::size_t>(index)] | (len << 12));
            }
            code <<= 1;
        }
    }
};

class Inflater {
public:
    Inflater(std::span<const std::uint8_t> in, std::vector<std::uint8_t>& out, std::size_t limit)
        : in_(in), out_(out), limit_(limit) {}

    void run() {
        bool last = false;
        while (!last) {
            last = bits(1) != 0;
            switch (bits(2)) {
            case 0: stored(); break;
            case 1: fixed(); break;
            case 2: dynamic(); break;
            default: throw InflateError("invalid block type");
            }
        }
    }

    // Input bytes used, up to the end of the final block.
    std::size_t consumed() const { return pos_ - static_cast<std::size_t>(bitCount_ / 8); }

private:
    std::span<const std::uint8_t> in_;
    std::vector<std::uint8_t>& out_;
    std::size_t limit_;
    std::size_t pos_ = 0;
    std::uint32_t bitBuf_ = 0;
    int bitCount_ = 0;

    // Refuses output beyond the limit (a damaged or hostile stream).
    void room(std::size_t n) const {
        if (n > limit_ - out_.size()) throw InflateError("output larger than expected");
    }

    int bits(int need) {
        std::uint32_t v = bitBuf_;
        while (bitCount_ < need) {
            if (pos_ >= in_.size()) throw InflateError("unexpected end of compressed data");
            v |= static_cast<std::uint32_t>(in_[pos_++]) << bitCount_;
            bitCount_ += 8;
        }
        bitBuf_ = v >> need;
        bitCount_ -= need;
        return static_cast<int>(v & ((1u << need) - 1));
    }

    int decode(const Huffman& h) {
        while (bitCount_ <= 24 && pos_ < in_.size()) {
            bitBuf_ |= static_cast<std::uint32_t>(in_[pos_++]) << bitCount_;
            bitCount_ += 8;
        }
        const std::uint16_t e = h.fast[bitBuf_ & ((1u << kFastBits) - 1)];
        const int fastLen = e >> 12;
        if (fastLen != 0 && fastLen <= bitCount_) {
            bitBuf_ >>= fastLen;
            bitCount_ -= fastLen;
            return e & 0x0FFF;
        }
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= bits(1);
            const int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw InflateError("invalid Huffman code");
    }

    void stored() {
        // Skip to the byte boundary and return whole bytes read ahead.
        pos_ -= static_cast<std::size_t>(bitCount_ / 8);
        bitBuf_ = 0;
        bitCount_ = 0;
        if (pos_ + 4 > in_.size()) throw InflateError("unexpected end of compressed data");
        const unsigned len = in_[pos_] | (in_[pos_ + 1] << 8);
        const unsigned nlen = in_[pos_ + 2] | (in_[pos_ + 3] << 8);
        pos_ += 4;
        if (len != (~nlen & 0xFFFFu)) throw InflateError("stored block length mismatch");
        if (pos_ + len > in_.size()) throw InflateError("unexpected end of compressed data");
        room(len);
        out_.insert(out_.end(), in_.begin() + static_cast<std::ptrdiff_t>(pos_), in_.begin() + static_cast<std::ptrdiff_t>(pos_ + len));
        pos_ += len;
    }

    void codes(const Huffman& lit, const Huffman& dist) {
        static constexpr std::uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr std::uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr std::uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                                        33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                                        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lit);
            if (sym < 256) {
                room(1);
                out_.push_back(static_cast<std::uint8_t>(sym));
                continue;
            }
            if (sym == 256) return;
            sym -= 257;
            if (sym >= 29) throw InflateError("invalid length code");
            const std::size_t len = kLenBase[sym] + static_cast<std::size_t>(bits(kLenExtra[sym]));
            const int dsym = decode(dist);
            if (dsym >= 30) throw InflateError("invalid distance code");
            const std::size_t d = kDistBase[dsym] + static_cast<std::size_t>(bits(kDistExtra[dsym]));
            if (d > out_.size()) throw InflateError("distance too far back");
            room(len);
            // Byte by byte: the source may overlap the bytes being written.
            std::size_t from = out_.size() - d;
            for (std::size_t i = 0; i < len; ++i) out_.push_back(out_[from++]);
        }
    }

    void fixed() {
        static const auto tables = [] {
            std::pair<Huffman, Huffman> t;
            std::uint8_t lengths[kMaxLitLen];
            int i = 0;
            for (; i < 144; ++i) lengths[i] = 8;
            for (; i < 256; ++i) lengths[i] = 9;
            for (; i < 280; ++i) lengths[i] = 7;
            for (; i < kMaxLitLen; ++i) lengths[i] = 8;
            t.first.build(lengths, kMaxLitLen);
            for (i = 0; i < kMaxDist; ++i) lengths[i] = 5;
            t.second.build(lengths, kMaxDist);
            return t;
        }();
        codes(tables.first, tables.second);
    }

    void dynamic() {
        static constexpr std::uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > kMaxDist) throw InflateError("bad code counts");
        std::uint8_t lengths[kMaxLitLen + kMaxDist] = {};
        for (int i = 0; i < ncode; ++i) lengths[kOrder[i]] = static_cast<std::uint8_t>(bits(3));
        Huffman lencode;
        lencode.build(lengths, 19);

        int index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 16) {
                lengths[index++] = static_cast<std::uint8_t>(sym);
                continue;
            }
            std::uint8_t value = 0;
            int repeat = 0;
            if (sym == 16) {
                if (index == 0) throw InflateError("repeat with no previous length");
                value = lengths[index - 1];
                repeat = 3 + bits(2);
            } else if (sym == 17) {
                repeat = 3 + bits(3);
            } else {
                repeat = 11 + bits(7);
            }
            if (index + repeat > nlen + ndist) throw InflateError("too many code lengths");
            while (repeat--) lengths[index++] = value;
        }
        if (lengths[256] == 0) throw InflateError("no end-of-block code");
        Huffman lit, dist;
        lit.build(lengths, nlen);
        dist.build(lengths + nlen, ndist);
        codes(lit, dist);
    }
};

} // namespace

std::vector<std::uint8_t> inflate(std::span<const std::uint8_t> compressed, std::size_t sizeHint, std::size_t maxSize,
                                  std::size_t* consumed) {
    std::vector<std::uint8_t> out;
    // DEFLATE expands at most 1032:1; a larger hint is not trusted.
    out.reserve(std::min({sizeHint, maxSize, compressed.size() * 1032 + 1024}));
    Inflater inflater(compressed, out, maxSize);
    inflater.run();
    if (consumed) *consumed = inflater.consumed();
    return out;
}

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (const std::uint8_t b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

} // namespace openday::formats
