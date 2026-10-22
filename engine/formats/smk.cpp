#include "formats/smk.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "formats/binary.hpp"

namespace openday::formats {
namespace {

using binary::Bytes;
using binary::putU16;
using binary::putU32;
using binary::readU32;

constexpr std::size_t kHeaderSize = 0x68;
constexpr std::uint32_t kFlagRingFrame = 1;

constexpr std::uint32_t kAudioCompressed = 1u << 31;
constexpr std::uint32_t kAudioPresent = 1u << 30;
constexpr std::uint32_t kAudio16Bit = 1u << 29;
constexpr std::uint32_t kAudioStereo = 1u << 28;
constexpr std::uint32_t kAudioCodecMask = 3u << 26; // non-zero: Bink audio, not used by SMK2
constexpr std::uint32_t kAudioRateMask = 0x00FFFFFF;

// Code-length limits; deeper trees are rejected as corrupt.
constexpr int kMaxByteTreeDepth = 32;
constexpr int kMaxBigTreeDepth = 500;

// Huffman trees are stored flat: an inner node holds kNode | distance to
// its "1" child (its "0" child follows it directly); a leaf holds its value.
constexpr std::int32_t kNode = 1 << 30;

// Run lengths selected by bits 2..7 of a block type value.
constexpr std::array<std::uint16_t, 64> kRuns = [] {
    std::array<std::uint16_t, 64> r{};
    for (std::size_t i = 0; i < 59; ++i) r[i] = static_cast<std::uint16_t>(i + 1);
    r[59] = 128;
    r[60] = 256;
    r[61] = 512;
    r[62] = 1024;
    r[63] = 2048;
    return r;
}();

enum BlockKind : unsigned { kMono = 0, kFull = 1, kSkip = 2, kSolid = 3 };

// 6-bit palette components are widened by replicating the top bits.
std::uint8_t widen6(unsigned v) {
    v &= 0x3F;
    return static_cast<std::uint8_t>((v << 2) | (v >> 4));
}

// Bits are consumed starting with the least significant bit of each byte.
class BitReader {
public:
    explicit BitReader(Bytes d) : d_(d) {}

    unsigned bit() {
        if (pos_ >= d_.size() * 8) throw SmkError("bitstream overrun");
        const unsigned b = (d_[pos_ >> 3] >> (pos_ & 7)) & 1u;
        ++pos_;
        return b;
    }

    unsigned bits(int n) {
        unsigned v = 0;
        for (int i = 0; i < n; ++i) v |= bit() << i;
        return v;
    }

    std::size_t remaining() const { return d_.size() * 8 - pos_; }

private:
    Bytes d_;
    std::size_t pos_ = 0;
};

// An 8-bit tree. An absent tree (no nodes) decodes to 0 without reading bits.
using ByteTree = std::vector<std::int32_t>;

void readByteNodes(BitReader& br, ByteTree& nodes, int depth) {
    if (depth > kMaxByteTreeDepth) throw SmkError("byte tree too deep");
    if (!br.bit()) {
        nodes.push_back(static_cast<std::int32_t>(br.bits(8)));
        return;
    }
    const std::size_t t = nodes.size();
    nodes.push_back(0);
    readByteNodes(br, nodes, depth + 1);
    nodes[t] = kNode | static_cast<std::int32_t>(nodes.size() - t - 1);
    readByteNodes(br, nodes, depth + 1);
}

// Presence bit, the tree, then one terminating bit.
ByteTree readByteTree(BitReader& br) {
    ByteTree t;
    if (!br.bit()) return t;
    readByteNodes(br, t, 0);
    br.bit();
    return t;
}

std::int32_t walk(const std::vector<std::int32_t>& nodes, BitReader& br) {
    std::size_t p = 0;
    while (nodes[p] & kNode) {
        if (br.bit()) p += static_cast<std::size_t>(nodes[p] & ~kNode);
        ++p;
    }
    return nodes[p];
}

unsigned decodeByte(const ByteTree& t, BitReader& br) {
    return t.empty() ? 0u : static_cast<unsigned>(walk(t, br));
}

struct BigTreeContext {
    const ByteTree& low;
    const ByteTree& high;
    unsigned escapes[3];
    std::int64_t slots[3] = {-1, -1, -1};
};

void readBigNodes(BitReader& br, std::vector<std::int32_t>& nodes, BigTreeContext& ctx, int depth) {
    if (depth > kMaxBigTreeDepth) throw SmkError("tree too deep");
    if (!br.bit()) {
        unsigned v = decodeByte(ctx.low, br);
        v |= decodeByte(ctx.high, br) << 8;
        // A leaf holding one of the escape values becomes a "recent value"
        // slot whose content changes during decoding.
        for (int i = 0; i < 3; ++i) {
            if (v == ctx.escapes[i]) {
                ctx.slots[i] = static_cast<std::int64_t>(nodes.size());
                v = 0;
                break;
            }
        }
        nodes.push_back(static_cast<std::int32_t>(v));
        return;
    }
    const std::size_t t = nodes.size();
    nodes.push_back(0);
    readBigNodes(br, nodes, ctx, depth + 1);
    nodes[t] = kNode | static_cast<std::int32_t>(nodes.size() - t - 1);
    readBigNodes(br, nodes, ctx, depth + 1);
}

detail::SmkTable readTable(BitReader& br) {
    detail::SmkTable t;
    if (!br.bit()) {
        // Absent table: always decodes to 0.
        t.nodes = {0, 0, 0, 0};
        t.recent[0] = 1;
        t.recent[1] = 2;
        t.recent[2] = 3;
        return t;
    }
    const ByteTree low = readByteTree(br);
    const ByteTree high = readByteTree(br);
    BigTreeContext ctx{low, high, {br.bits(16), br.bits(16), br.bits(16)}};
    readBigNodes(br, t.nodes, ctx, 0);
    br.bit();
    for (int i = 0; i < 3; ++i) {
        if (ctx.slots[i] < 0) { // escape value not in the tree: use a spare cell
            ctx.slots[i] = static_cast<std::int64_t>(t.nodes.size());
            t.nodes.push_back(0);
        }
        t.recent[i] = static_cast<std::size_t>(ctx.slots[i]);
    }
    return t;
}

// Decodes a value and updates the three-entry recent-value cache.
unsigned getCode(detail::SmkTable& t, BitReader& br) {
    const std::int32_t v = walk(t.nodes, br);
    if (v != t.nodes[t.recent[0]]) {
        t.nodes[t.recent[2]] = t.nodes[t.recent[1]];
        t.nodes[t.recent[1]] = t.nodes[t.recent[0]];
        t.nodes[t.recent[0]] = v;
    }
    return static_cast<unsigned>(v);
}

void resetRecent(detail::SmkTable& t) {
    for (std::size_t slot : t.recent) t.nodes[slot] = 0;
}

} // namespace

SmkVideo::SmkVideo(std::vector<std::uint8_t> bytes) : data_(std::move(bytes)) {
    Bytes d = data_;
    if (d.size() < kHeaderSize) throw SmkError("file too small");
    if (!(d[0] == 'S' && d[1] == 'M' && d[2] == 'K' && d[3] == '2')) throw SmkError("not a Smacker 2 file");

    const std::uint32_t w = readU32(d, 0x04);
    const std::uint32_t h = readU32(d, 0x08);
    const std::uint32_t frames = readU32(d, 0x0C);
    const auto rate = static_cast<std::int32_t>(readU32(d, 0x10));
    const std::uint32_t flags = readU32(d, 0x14);
    const std::uint32_t treesSize = readU32(d, 0x34);

    if (w == 0 || h == 0 || w > 4096 || h > 4096) throw SmkError("bad dimensions");
    if (w % 4 != 0 || h % 4 != 0) throw SmkError("dimensions must be multiples of 4");
    if (flags & ~kFlagRingFrame) throw SmkError("unsupported flags (interlaced or doubled video)");
    width_ = static_cast<int>(w);
    height_ = static_cast<int>(h);
    ringFrame_ = (flags & kFlagRingFrame) != 0;
    frameMicros_ = rate > 0 ? static_cast<std::uint32_t>(rate) * 1000u
                 : rate < 0 ? static_cast<std::uint32_t>(-static_cast<std::int64_t>(rate)) * 10u
                            : 100000u;

    for (std::size_t i = 0; i < kAudioTracks; ++i) {
        const std::uint32_t a = readU32(d, 0x48 + 4 * i);
        SmkAudioTrack& t = audio_[i];
        t.present = (a & kAudioPresent) != 0;
        t.compressed = (a & kAudioCompressed) != 0;
        t.sixteenBit = (a & kAudio16Bit) != 0;
        t.stereo = (a & kAudioStereo) != 0;
        t.sampleRate = a & kAudioRateMask;
        if (t.present && (a & kAudioCodecMask)) throw SmkError("unsupported audio codec");
    }

    const std::size_t total = std::size_t{frames} + (ringFrame_ ? 1 : 0);
    if (total == 0 || total > (d.size() - kHeaderSize) / 5) throw SmkError("bad frame count");
    frameCount_ = static_cast<int>(frames);

    const std::size_t typesAt = kHeaderSize + total * 4;
    const std::size_t treesAt = typesAt + total;
    if (treesSize > d.size() - treesAt) throw SmkError("trees past end of file");
    std::size_t at = treesAt + treesSize;
    for (std::size_t i = 0; i < total; ++i) {
        const std::uint32_t size = readU32(d, kHeaderSize + i * 4) & ~3u; // low bits: keyframe flags
        if (size > d.size() - at) throw SmkError("frame " + std::to_string(i) + " past end of file");
        frameOffsets_.push_back(at);
        frameSizes_.push_back(size);
        frameTypes_.push_back(d[typesAt + i]);
        at += size;
    }

    BitReader br(d.subspan(treesAt, treesSize));
    mmap_ = readTable(br);
    mclr_ = readTable(br);
    full_ = readTable(br);
    type_ = readTable(br);

    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0);
}

void SmkVideo::rewind() {
    current_ = -1;
    unusedBits_ = 0;
    std::fill(pixels_.begin(), pixels_.end(), std::uint8_t{0});
    palette_ = Palette{};
}

bool SmkVideo::nextFrame() {
    if (current_ + 1 >= frameCount_) return false;
    ++current_;
    const auto idx = static_cast<std::size_t>(current_);
    const Bytes f = Bytes(data_).subspan(frameOffsets_[idx], frameSizes_[idx]);
    const std::uint8_t types = frameTypes_[idx];
    std::size_t p = 0;

    if (types & 1) {
        // Palette chunk: the first byte is the chunk length in 4-byte units.
        if (f.empty()) throw SmkError("missing palette chunk");
        const std::size_t len = std::size_t{f[0]} * 4;
        if (len == 0 || len > f.size()) throw SmkError("bad palette chunk length");
        const Bytes c = f.subspan(1, len - 1);
        const Palette old = palette_;
        std::size_t i = 0, e = 0;
        while (e < 256 && i < c.size()) {
            const std::uint8_t b = c[i++];
            if (b & 0x80) { // keep (b & 0x7F) + 1 entries
                e += (b & 0x7Fu) + 1;
            } else if (b & 0x40) { // copy entries from the previous palette
                if (i >= c.size()) throw SmkError("truncated palette chunk");
                const std::size_t n = (b & 0x3Fu) + 1, src = c[i++];
                if (e + n > 256 || src + n > 256) throw SmkError("palette copy out of range");
                std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(src), n,
                            palette_.begin() + static_cast<std::ptrdiff_t>(e));
                e += n;
            } else { // literal 6-bit red, green, blue
                if (i + 2 > c.size()) throw SmkError("truncated palette chunk");
                palette_[e++] = Rgb{widen6(b), widen6(c[i]), widen6(c[i + 1])};
                i += 2;
            }
        }
        if (e > 256) throw SmkError("palette skip out of range");
        p = len;
    }

    for (int t = 0; t < kAudioTracks; ++t) {
        if (!(types & (2u << t))) continue;
        if (p + 4 > f.size()) throw SmkError("truncated audio chunk");
        const std::uint32_t len = readU32(f, p);
        if (len < 4 || len > f.size() - p) throw SmkError("bad audio chunk length");
        p += len;
    }

    // Video: 4x4 blocks in raster order.
    BitReader br(f.subspan(p));
    resetRecent(mmap_);
    resetRecent(mclr_);
    resetRecent(full_);
    resetRecent(type_);
    const std::size_t w = static_cast<std::size_t>(width_);
    const std::size_t blocksPerRow = w / 4;
    const std::size_t blocks = blocksPerRow * static_cast<std::size_t>(height_ / 4);
    std::size_t blk = 0;
    while (blk < blocks) {
        const unsigned type = getCode(type_, br);
        std::size_t run = kRuns[(type >> 2) & 0x3F];
        for (; run > 0 && blk < blocks; --run, ++blk) {
            std::uint8_t* out = pixels_.data() + (blk / blocksPerRow) * 4 * w + (blk % blocksPerRow) * 4;
            switch (type & 3) {
            case kMono: {
                const unsigned clr = getCode(mclr_, br);
                unsigned map = getCode(mmap_, br);
                const auto hi = static_cast<std::uint8_t>(clr >> 8), lo = static_cast<std::uint8_t>(clr);
                for (int y = 0; y < 4; ++y, out += w, map >>= 4)
                    for (int x = 0; x < 4; ++x) out[x] = ((map >> x) & 1) ? hi : lo;
                break;
            }
            case kFull:
                for (int y = 0; y < 4; ++y, out += w) {
                    unsigned v = getCode(full_, br);
                    out[2] = static_cast<std::uint8_t>(v);
                    out[3] = static_cast<std::uint8_t>(v >> 8);
                    v = getCode(full_, br);
                    out[0] = static_cast<std::uint8_t>(v);
                    out[1] = static_cast<std::uint8_t>(v >> 8);
                }
                break;
            case kSkip:
                break;
            case kSolid: {
                const auto c = static_cast<std::uint8_t>(type >> 8);
                for (int y = 0; y < 4; ++y, out += w) std::fill_n(out, 4, c);
                break;
            }
            }
        }
    }
    unusedBits_ = br.remaining();
    return true;
}

SmkAudio SmkVideo::decodeAudio(int track) const {
    if (track < 0 || track >= kAudioTracks) throw SmkError("bad audio track");
    const SmkAudioTrack& info = audio_[static_cast<std::size_t>(track)];
    SmkAudio out;
    out.sampleRate = info.sampleRate;
    out.channels = info.stereo ? 2 : 1;
    out.bitsPerSample = info.sixteenBit ? 16 : 8;
    if (!info.present) return out;

    for (std::size_t idx = 0; idx < frameOffsets_.size(); ++idx) {
        const std::uint8_t types = frameTypes_[idx];
        if (!(types & (2u << track))) continue;
        const Bytes f = Bytes(data_).subspan(frameOffsets_[idx], frameSizes_[idx]);
        std::size_t p = (types & 1) && !f.empty() ? std::size_t{f[0]} * 4 : 0;
        Bytes chunk;
        for (int t = 0; t <= track; ++t) {
            if (!(types & (2u << t))) continue;
            if (p + 4 > f.size()) throw SmkError("truncated audio chunk");
            const std::uint32_t len = readU32(f, p);
            if (len < 4 || len > f.size() - p) throw SmkError("bad audio chunk length");
            if (t == track) chunk = f.subspan(p + 4, len - 4);
            p += len;
        }

        if (!info.compressed) {
            out.pcm.insert(out.pcm.end(), chunk.begin(), chunk.end());
            continue;
        }
        if (chunk.size() < 4) throw SmkError("truncated audio chunk");
        const std::uint32_t unpacked = readU32(chunk, 0);
        BitReader br(chunk.subspan(4));
        if (!br.bit()) continue; // chunk without samples
        const bool stereo = br.bit() != 0;
        const bool sixteen = br.bit() != 0;
        if (stereo != info.stereo || sixteen != info.sixteenBit) throw SmkError("audio chunk format mismatch");

        // Delta coding with one tree per channel (8-bit) or two (16-bit:
        // low and high byte); the first sample of each channel is stored as is.
        ByteTree trees[4];
        const int nTrees = 1 << ((sixteen ? 1 : 0) + (stereo ? 1 : 0));
        for (int i = 0; i < nTrees; ++i) trees[i] = readByteTree(br);
        const std::size_t channels = stereo ? 2 : 1;
        const std::size_t samples = sixteen ? unpacked / 2 : unpacked;
        if (samples < channels) continue;

        if (sixteen) {
            std::int16_t pred[2] = {0, 0};
            for (std::size_t c = channels; c-- > 0;) {
                const unsigned v = br.bits(16);
                pred[c] = static_cast<std::int16_t>(((v & 0xFF) << 8) | (v >> 8));
            }
            for (std::size_t i = 0; i < samples; ++i) {
                const std::size_t c = i % channels;
                if (i >= channels) {
                    unsigned delta = decodeByte(trees[c * 2], br);
                    delta |= decodeByte(trees[c * 2 + 1], br) << 8;
                    pred[c] = static_cast<std::int16_t>(pred[c] + static_cast<std::int16_t>(delta));
                }
                putU16(out.pcm, static_cast<std::uint16_t>(pred[c]));
            }
        } else {
            std::uint8_t pred[2] = {0, 0};
            for (std::size_t c = channels; c-- > 0;) pred[c] = static_cast<std::uint8_t>(br.bits(8));
            for (std::size_t i = 0; i < samples; ++i) {
                const std::size_t c = i % channels;
                if (i >= channels) pred[c] = static_cast<std::uint8_t>(pred[c] + decodeByte(trees[c], br));
                out.pcm.push_back(pred[c]);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Writer (test fixtures)

namespace {

class BitWriter {
public:
    void bit(unsigned b) {
        if (n_ % 8 == 0) out_.push_back(0);
        if (b) out_.back() = static_cast<std::uint8_t>(out_.back() | (1u << (n_ % 8)));
        ++n_;
    }
    void bits(std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i) bit((v >> i) & 1u);
    }
    const std::vector<std::uint8_t>& bytes() const { return out_; }

private:
    std::vector<std::uint8_t> out_;
    std::size_t n_ = 0;
};

struct Code {
    std::uint32_t bits = 0;
    int length = 0;
};
using CodeBook = std::map<unsigned, Code>;

// Writes a balanced tree over the sorted values; leaf(v) writes the leaf payload.
template <typename Leaf>
void writeBalanced(BitWriter& bw, const std::vector<unsigned>& vals, std::size_t lo, std::size_t hi, Code prefix,
                   CodeBook& book, Leaf&& leaf) {
    if (hi - lo == 1) {
        bw.bit(0);
        leaf(vals[lo]);
        book[vals[lo]] = prefix;
        return;
    }
    bw.bit(1);
    const std::size_t mid = lo + (hi - lo) / 2;
    writeBalanced(bw, vals, lo, mid, Code{prefix.bits, prefix.length + 1}, book, leaf);
    writeBalanced(bw, vals, mid, hi, Code{prefix.bits | (1u << prefix.length), prefix.length + 1}, book, leaf);
}

void putCode(BitWriter& bw, const CodeBook& book, unsigned v) {
    const Code& c = book.at(v);
    bw.bits(c.bits, c.length);
}

CodeBook writeByteTree(BitWriter& bw, const std::set<unsigned>& values) {
    CodeBook book;
    const std::vector<unsigned> vals(values.begin(), values.end());
    bw.bit(1);
    writeBalanced(bw, vals, 0, vals.size(), Code{}, book, [&](unsigned v) { bw.bits(v, 8); });
    bw.bit(0);
    return book;
}

CodeBook writeTable(BitWriter& bw, const std::set<unsigned>& values) {
    CodeBook book;
    if (values.empty()) {
        bw.bit(0);
        return book;
    }
    bw.bit(1);
    std::set<unsigned> lows, highs;
    for (unsigned v : values) {
        lows.insert(v & 0xFF);
        highs.insert(v >> 8);
    }
    const CodeBook lowBook = writeByteTree(bw, lows);
    const CodeBook highBook = writeByteTree(bw, highs);
    // Escape values that never occur, so the recent-value cache stays unused.
    int written = 0;
    for (unsigned e = 0xFFFF; written < 3; --e) {
        if (values.count(e)) continue;
        bw.bits(e, 16);
        ++written;
    }
    const std::vector<unsigned> vals(values.begin(), values.end());
    writeBalanced(bw, vals, 0, vals.size(), Code{}, book, [&](unsigned v) {
        putCode(bw, lowBook, v & 0xFF);
        putCode(bw, highBook, v >> 8);
    });
    bw.bit(0);
    return book;
}

struct Block {
    unsigned type = 0;
    std::vector<unsigned> mclr, mmap, full;
};

} // namespace

std::vector<std::uint8_t> writeSmk(int width, int height, const std::vector<SmkFrame>& frames,
                                   std::uint32_t frameMicros, const std::vector<std::uint8_t>& audio8,
                                   std::uint32_t sampleRate) {
    if (width <= 0 || height <= 0 || width % 4 || height % 4) throw SmkError("writeSmk: bad dimensions");
    if (frames.empty()) throw SmkError("writeSmk: no frames");
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t bw4 = w / 4, blocks = bw4 * static_cast<std::size_t>(height / 4);

    std::vector<std::vector<Block>> coded(frames.size());
    std::set<unsigned> mmapVals, mclrVals, fullVals, typeVals;
    for (std::size_t f = 0; f < frames.size(); ++f) {
        if (frames[f].pixels.size() != w * static_cast<std::size_t>(height)) throw SmkError("writeSmk: bad frame size");
        for (std::size_t b = 0; b < blocks; ++b) {
            const std::size_t origin = (b / bw4) * 4 * w + (b % bw4) * 4;
            auto px = [&](std::size_t fr, int x, int y) {
                return frames[fr].pixels[origin + static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)];
            };
            std::set<std::uint8_t> colors;
            bool same = f > 0;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    colors.insert(px(f, x, y));
                    if (f > 0 && px(f, x, y) != px(f - 1, x, y)) same = false;
                }
            Block blk;
            if (same) {
                blk.type = kSkip;
            } else if (colors.size() == 1) {
                blk.type = kSolid | (unsigned{*colors.begin()} << 8);
            } else if (colors.size() == 2) {
                const unsigned lo = *colors.begin(), hi = *colors.rbegin();
                unsigned map = 0;
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                        if (px(f, x, y) == hi) map |= 1u << (y * 4 + x);
                blk.type = kMono;
                blk.mclr.push_back((hi << 8) | lo);
                blk.mmap.push_back(map);
            } else {
                blk.type = kFull;
                for (int y = 0; y < 4; ++y) {
                    blk.full.push_back(px(f, 2, y) | (unsigned{px(f, 3, y)} << 8));
                    blk.full.push_back(px(f, 0, y) | (unsigned{px(f, 1, y)} << 8));
                }
            }
            typeVals.insert(blk.type);
            mclrVals.insert(blk.mclr.begin(), blk.mclr.end());
            mmapVals.insert(blk.mmap.begin(), blk.mmap.end());
            fullVals.insert(blk.full.begin(), blk.full.end());
            coded[f].push_back(std::move(blk));
        }
    }

    BitWriter trees;
    const CodeBook mmapBook = writeTable(trees, mmapVals);
    const CodeBook mclrBook = writeTable(trees, mclrVals);
    const CodeBook fullBook = writeTable(trees, fullVals);
    const CodeBook typeBook = writeTable(trees, typeVals);

    std::vector<std::vector<std::uint8_t>> frameData(frames.size());
    std::vector<std::uint8_t> frameTypes(frames.size(), 0);
    std::uint32_t audioUnpacked = 0;
    for (std::size_t f = 0; f < frames.size(); ++f) {
        std::vector<std::uint8_t>& out = frameData[f];
        if (f == 0 || frames[f].palette != frames[f - 1].palette) {
            frameTypes[f] |= 1;
            std::vector<std::uint8_t> pal = {0};
            for (const Rgb& c : frames[f].palette) {
                pal.push_back(static_cast<std::uint8_t>(c.r >> 2));
                pal.push_back(static_cast<std::uint8_t>(c.g >> 2));
                pal.push_back(static_cast<std::uint8_t>(c.b >> 2));
            }
            pal.resize((pal.size() + 3) & ~std::size_t{3}, 0);
            pal[0] = static_cast<std::uint8_t>(pal.size() / 4);
            out.insert(out.end(), pal.begin(), pal.end());
        }
        if (f == 0 && !audio8.empty()) {
            frameTypes[f] |= 2;
            std::set<unsigned> deltas;
            for (std::size_t i = 1; i < audio8.size(); ++i)
                deltas.insert(static_cast<std::uint8_t>(audio8[i] - audio8[i - 1]));
            if (deltas.empty()) deltas.insert(0);
            BitWriter a;
            a.bit(1); // data present
            a.bit(0); // mono
            a.bit(0); // 8-bit
            const CodeBook book = writeByteTree(a, deltas);
            a.bits(audio8[0], 8);
            for (std::size_t i = 1; i < audio8.size(); ++i)
                putCode(a, book, static_cast<std::uint8_t>(audio8[i] - audio8[i - 1]));
            audioUnpacked = static_cast<std::uint32_t>(audio8.size());
            putU32(out, static_cast<std::uint32_t>(8 + a.bytes().size()));
            putU32(out, audioUnpacked);
            out.insert(out.end(), a.bytes().begin(), a.bytes().end());
        }
        BitWriter v;
        for (const Block& blk : coded[f]) {
            putCode(v, typeBook, blk.type);
            for (std::size_t i = 0; i < blk.mclr.size(); ++i) {
                putCode(v, mclrBook, blk.mclr[i]);
                putCode(v, mmapBook, blk.mmap[i]);
            }
            for (unsigned x : blk.full) putCode(v, fullBook, x);
        }
        out.insert(out.end(), v.bytes().begin(), v.bytes().end());
        out.resize((out.size() + 3) & ~std::size_t{3}, 0);
    }

    std::vector<std::uint8_t> out = {'S', 'M', 'K', '2'};
    putU32(out, static_cast<std::uint32_t>(width));
    putU32(out, static_cast<std::uint32_t>(height));
    putU32(out, static_cast<std::uint32_t>(frames.size()));
    const std::int32_t rate = frameMicros % 1000 == 0 ? static_cast<std::int32_t>(frameMicros / 1000)
                                                      : -static_cast<std::int32_t>(frameMicros / 10);
    putU32(out, static_cast<std::uint32_t>(rate));
    putU32(out, 0); // flags
    for (int i = 0; i < SmkVideo::kAudioTracks; ++i) putU32(out, i == 0 ? audioUnpacked : 0);
    putU32(out, static_cast<std::uint32_t>(trees.bytes().size()));
    for (const std::set<unsigned>* s : {&mmapVals, &mclrVals, &fullVals, &typeVals})
        putU32(out, static_cast<std::uint32_t>((s->size() * 2 + 4) * 4)); // table allocation hints
    for (int i = 0; i < SmkVideo::kAudioTracks; ++i)
        putU32(out, i == 0 && !audio8.empty() ? kAudioCompressed | kAudioPresent | (sampleRate & kAudioRateMask) : 0);
    putU32(out, 0);
    for (std::size_t f = 0; f < frames.size(); ++f)
        putU32(out, static_cast<std::uint32_t>(frameData[f].size()) | (f == 0 ? 1u : 0u));
    out.insert(out.end(), frameTypes.begin(), frameTypes.end());
    out.insert(out.end(), trees.bytes().begin(), trees.bytes().end());
    for (const auto& fd : frameData) out.insert(out.end(), fd.begin(), fd.end());
    return out;
}

} // namespace openday::formats
