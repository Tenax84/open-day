#include "formats/lzma.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace openday::formats {
namespace {

using Prob = std::uint16_t;

constexpr int kNumBitModelTotalBits = 11;
constexpr std::uint32_t kBitModelTotal = 1u << kNumBitModelTotalBits;
constexpr int kNumMoveBits = 5;
constexpr std::uint32_t kTopValue = 1u << 24;

constexpr int kNumStates = 12;
constexpr int kNumPosBitsMax = 4;
constexpr int kNumLenToPosStates = 4;
constexpr int kNumAlignBits = 4;
constexpr int kStartPosModelIndex = 4;
constexpr int kEndPosModelIndex = 14;
constexpr int kNumFullDistances = 1 << (kEndPosModelIndex >> 1);
constexpr int kMatchMinLen = 2;

// Buffered input with a count of the bytes consumed (LZMA2 checks each
// chunk's packed size against it).
class Input {
public:
    explicit Input(ByteSource& src) : src_(src) {}
    std::uint8_t next() {
        if (pos_ == len_) {
            len_ = src_.read(buf_.data(), buf_.size());
            pos_ = 0;
            if (len_ == 0) throw LzmaError("truncated LZMA stream");
        }
        ++consumed_;
        return buf_[pos_++];
    }
    std::uint64_t consumed() const { return consumed_; }

private:
    ByteSource& src_;
    std::array<std::uint8_t, 1 << 16> buf_{};
    std::size_t pos_ = 0, len_ = 0;
    std::uint64_t consumed_ = 0;
};

class RangeDecoder {
public:
    explicit RangeDecoder(Input& in) : in_(in) {}

    void init() {
        if (in_.next() != 0) throw LzmaError("corrupt LZMA stream (range coder start)");
        code_ = 0;
        range_ = 0xFFFFFFFFu;
        for (int i = 0; i < 4; ++i) code_ = (code_ << 8) | in_.next();
        if (code_ == range_) throw LzmaError("corrupt LZMA stream (range coder start)");
    }
    bool finishedOk() const { return code_ == 0; }

    unsigned bit(Prob& p) {
        const std::uint32_t bound = (range_ >> kNumBitModelTotalBits) * p;
        unsigned b;
        if (code_ < bound) {
            p = static_cast<Prob>(p + ((kBitModelTotal - p) >> kNumMoveBits));
            range_ = bound;
            b = 0;
        } else {
            p = static_cast<Prob>(p - (p >> kNumMoveBits));
            code_ -= bound;
            range_ -= bound;
            b = 1;
        }
        normalize();
        return b;
    }

    std::uint32_t direct(int numBits) {
        std::uint32_t res = 0;
        do {
            range_ >>= 1;
            code_ -= range_;
            const std::uint32_t t = 0u - (code_ >> 31);
            code_ += range_ & t;
            if (code_ == range_) throw LzmaError("corrupt LZMA stream (direct bits)");
            normalize();
            res = (res << 1) + (t + 1);
        } while (--numBits);
        return res;
    }

    unsigned tree(Prob* probs, int numBits) {
        unsigned m = 1;
        for (int i = 0; i < numBits; ++i) m = (m << 1) + bit(probs[m]);
        return m - (1u << numBits);
    }

    unsigned reverseTree(Prob* probs, int numBits) {
        unsigned m = 1, sym = 0;
        for (int i = 0; i < numBits; ++i) {
            const unsigned b = bit(probs[m]);
            m = (m << 1) + b;
            sym |= b << i;
        }
        return sym;
    }

private:
    void normalize() {
        if (range_ < kTopValue) {
            range_ <<= 8;
            code_ = (code_ << 8) | in_.next();
        }
    }

    Input& in_;
    std::uint32_t range_ = 0, code_ = 0;
};

struct LenDecoder {
    Prob choice, choice2;
    std::array<Prob, (1 << kNumPosBitsMax) << 3> low, mid;
    std::array<Prob, 256> high;

    void reset() {
        choice = choice2 = kBitModelTotal / 2;
        low.fill(kBitModelTotal / 2);
        mid.fill(kBitModelTotal / 2);
        high.fill(kBitModelTotal / 2);
    }
    unsigned decode(RangeDecoder& rc, unsigned posState) {
        if (rc.bit(choice) == 0) return rc.tree(&low[posState << 3], 3);
        if (rc.bit(choice2) == 0) return 8 + rc.tree(&mid[posState << 3], 3);
        return 16 + rc.tree(high.data(), 8);
    }
};

// The sliding dictionary. It grows up to the dictionary size, then wraps.
class Window {
public:
    void setSize(std::uint32_t dictSize) { dictSize_ = std::max<std::uint32_t>(dictSize, 4096); }
    void reset() {
        pos_ = 0;
        full_ = false;
        total_ = 0;
    }
    bool empty() const { return total_ == 0; }
    std::uint64_t total() const { return total_; }

    void put(std::uint8_t b) {
        if (pos_ == buf_.size()) {
            if (buf_.size() < dictSize_) {
                buf_.resize(std::min<std::size_t>(dictSize_, std::max<std::size_t>(buf_.size() * 2, 1 << 16)));
            } else {
                pos_ = 0;
                full_ = true;
            }
        }
        buf_[pos_++] = b;
        ++total_;
    }
    // The byte `dist` + 1 positions back.
    std::uint8_t back(std::uint32_t dist) const {
        if (full_) return buf_[pos_ > dist ? pos_ - dist - 1 : buf_.size() + pos_ - dist - 1];
        return buf_[pos_ - dist - 1];
    }
    bool valid(std::uint32_t dist) const { return full_ ? dist < buf_.size() : dist < pos_; }

private:
    std::vector<std::uint8_t> buf_;
    std::size_t pos_ = 0;
    std::uint32_t dictSize_ = 4096;
    bool full_ = false;
    std::uint64_t total_ = 0;
};

// LZMA decoding state shared by the LZMA and LZMA2 streams.
class LzmaCore {
public:
    explicit LzmaCore(Input& in) : rc_(in) {}

    void setProps(unsigned propByte) {
        if (propByte >= 9 * 5 * 5) throw LzmaError("invalid LZMA properties");
        lc_ = propByte % 9;
        propByte /= 9;
        lp_ = propByte % 5;
        pb_ = propByte / 5;
        literals_.resize(0x300u << (lc_ + lp_));
    }
    void setDictSize(std::uint32_t size) { window_.setSize(size); }
    void resetDict() { window_.reset(); }
    void resetState() {
        std::fill(literals_.begin(), literals_.end(), static_cast<Prob>(kBitModelTotal / 2));
        posSlot_.fill(kBitModelTotal / 2);
        posDecoders_.fill(kBitModelTotal / 2);
        align_.fill(kBitModelTotal / 2);
        isMatch_.fill(kBitModelTotal / 2);
        isRep_.fill(kBitModelTotal / 2);
        isRepG0_.fill(kBitModelTotal / 2);
        isRepG1_.fill(kBitModelTotal / 2);
        isRepG2_.fill(kBitModelTotal / 2);
        isRep0Long_.fill(kBitModelTotal / 2);
        len_.reset();
        repLen_.reset();
        state_ = 0;
        rep0_ = rep1_ = rep2_ = rep3_ = 0;
        pending_ = 0;
    }
    void initRangeCoder() { rc_.init(); }
    bool rangeCoderFinished() const { return rc_.finishedOk(); }
    Window& window() { return window_; }
    bool pendingMatch() const { return pending_ > 0; }

    // Produces up to n bytes. Returns the count; sets `ended` at an end
    // marker.
    std::size_t decode(std::uint8_t* out, std::size_t n, bool& ended) {
        std::size_t done = 0;
        while (done < n) {
            if (pending_ > 0) {
                const std::size_t k = std::min<std::size_t>(pending_, n - done);
                for (std::size_t i = 0; i < k; ++i) {
                    const std::uint8_t b = window_.back(rep0_);
                    window_.put(b);
                    out[done++] = b;
                }
                pending_ -= static_cast<std::uint32_t>(k);
                continue;
            }
            const unsigned posState = static_cast<unsigned>(window_.total()) & ((1u << pb_) - 1);
            const unsigned state2 = (state_ << kNumPosBitsMax) + posState;
            if (rc_.bit(isMatch_[state2]) == 0) {
                const std::uint8_t b = decodeLiteral();
                window_.put(b);
                out[done++] = b;
                state_ = state_ < 4 ? 0 : state_ < 10 ? state_ - 3 : state_ - 6;
                continue;
            }
            unsigned len;
            if (rc_.bit(isRep_[state_]) != 0) {
                if (window_.empty()) throw LzmaError("corrupt LZMA stream (repeat at start)");
                if (rc_.bit(isRepG0_[state_]) == 0) {
                    if (rc_.bit(isRep0Long_[state2]) == 0) {
                        state_ = state_ < 7 ? 9 : 11;
                        const std::uint8_t b = window_.back(rep0_);
                        window_.put(b);
                        out[done++] = b;
                        continue;
                    }
                } else {
                    std::uint32_t dist;
                    if (rc_.bit(isRepG1_[state_]) == 0) {
                        dist = rep1_;
                    } else {
                        if (rc_.bit(isRepG2_[state_]) == 0) {
                            dist = rep2_;
                        } else {
                            dist = rep3_;
                            rep3_ = rep2_;
                        }
                        rep2_ = rep1_;
                    }
                    rep1_ = rep0_;
                    rep0_ = dist;
                }
                len = repLen_.decode(rc_, posState);
                state_ = state_ < 7 ? 8 : 11;
            } else {
                rep3_ = rep2_;
                rep2_ = rep1_;
                rep1_ = rep0_;
                len = len_.decode(rc_, posState);
                state_ = state_ < 7 ? 7 : 10;
                rep0_ = decodeDistance(len);
                if (rep0_ == 0xFFFFFFFFu) {
                    ended = true;
                    return done;
                }
            }
            if (!window_.valid(rep0_)) throw LzmaError("corrupt LZMA stream (distance)");
            pending_ = len + kMatchMinLen;
        }
        return done;
    }

private:
    std::uint8_t decodeLiteral() {
        const unsigned prev = window_.empty() ? 0 : window_.back(0);
        const unsigned litState =
            ((static_cast<unsigned>(window_.total()) & ((1u << lp_) - 1)) << lc_) + (prev >> (8 - lc_));
        Prob* probs = &literals_[0x300u * litState];
        unsigned sym = 1;
        if (state_ >= 7) {
            unsigned matchByte = window_.back(rep0_);
            do {
                const unsigned matchBit = (matchByte >> 7) & 1;
                matchByte <<= 1;
                const unsigned b = rc_.bit(probs[((1 + matchBit) << 8) + sym]);
                sym = (sym << 1) | b;
                if (matchBit != b) break;
            } while (sym < 0x100);
        }
        while (sym < 0x100) sym = (sym << 1) | rc_.bit(probs[sym]);
        return static_cast<std::uint8_t>(sym - 0x100);
    }

    std::uint32_t decodeDistance(unsigned len) {
        const unsigned lenState = std::min(len, static_cast<unsigned>(kNumLenToPosStates - 1));
        const unsigned posSlot = rc_.tree(&posSlot_[lenState << 6], 6);
        if (posSlot < static_cast<unsigned>(kStartPosModelIndex)) return posSlot;
        const int numDirectBits = static_cast<int>(posSlot >> 1) - 1;
        std::uint32_t dist = (2u | (posSlot & 1u)) << numDirectBits;
        if (posSlot < static_cast<unsigned>(kEndPosModelIndex)) {
            // posDecoders_ is indexed from 1 inside reverseTree.
            dist += rc_.reverseTree(&posDecoders_[dist - posSlot], numDirectBits);
        } else {
            dist += rc_.direct(numDirectBits - kNumAlignBits) << kNumAlignBits;
            dist += rc_.reverseTree(align_.data(), kNumAlignBits);
        }
        return dist;
    }

    RangeDecoder rc_;
    Window window_;
    unsigned lc_ = 3, lp_ = 0, pb_ = 2;
    std::vector<Prob> literals_;
    std::array<Prob, kNumLenToPosStates << 6> posSlot_{};
    std::array<Prob, 1 + kNumFullDistances - kEndPosModelIndex> posDecoders_{};
    std::array<Prob, 1 << kNumAlignBits> align_{};
    std::array<Prob, kNumStates << kNumPosBitsMax> isMatch_{}, isRep0Long_{};
    std::array<Prob, kNumStates> isRep_{}, isRepG0_{}, isRepG1_{}, isRepG2_{};
    LenDecoder len_{}, repLen_{};
    unsigned state_ = 0;
    std::uint32_t rep0_ = 0, rep1_ = 0, rep2_ = 0, rep3_ = 0;
    std::uint32_t pending_ = 0;
};

class Lzma1Source : public ByteSource {
public:
    explicit Lzma1Source(ByteSource& src) : in_(src), core_(in_) {
        const unsigned props = in_.next();
        std::uint32_t dict = 0;
        for (int i = 0; i < 4; ++i) dict |= static_cast<std::uint32_t>(in_.next()) << (8 * i);
        core_.setProps(props);
        core_.setDictSize(dict);
        core_.resetDict();
        core_.resetState();
        core_.initRangeCoder();
    }

    std::size_t read(std::uint8_t* out, std::size_t n) override {
        if (ended_ && !core_.pendingMatch()) return 0;
        return core_.decode(out, n, ended_);
    }

private:
    Input in_;
    LzmaCore core_;
    bool ended_ = false;
};

class Lzma2Source : public ByteSource {
public:
    Lzma2Source(ByteSource& src, std::uint32_t dictSize) : in_(src), core_(in_) { core_.setDictSize(dictSize); }

    std::size_t read(std::uint8_t* out, std::size_t n) override {
        std::size_t done = 0;
        while (done < n && !ended_) {
            if (chunkLeft_ == 0) {
                finishChunk();
                startChunk();
                continue;
            }
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(n - done, chunkLeft_));
            std::size_t got;
            if (uncompressed_) {
                for (std::size_t i = 0; i < want; ++i) {
                    const std::uint8_t b = in_.next();
                    core_.window().put(b);
                    out[done + i] = b;
                }
                got = want;
            } else {
                bool marker = false;
                got = core_.decode(out + done, want, marker);
                if (marker) throw LzmaError("corrupt LZMA2 stream (end marker inside a chunk)");
            }
            done += got;
            chunkLeft_ -= got;
        }
        return done;
    }

private:
    void finishChunk() {
        if (!inChunk_) return;
        inChunk_ = false;
        if (uncompressed_) return;
        const std::uint64_t used = in_.consumed() - chunkStart_;
        if (used > packed_ || core_.pendingMatch()) throw LzmaError("corrupt LZMA2 stream (chunk size)");
        for (std::uint64_t i = used; i < packed_; ++i) in_.next();
    }

    void startChunk() {
        const unsigned control = in_.next();
        if (control == 0) {
            ended_ = true;
            return;
        }
        if (control == 1 || control == 2) {
            if (control == 1) core_.resetDict();
            else if (needDictReset_) throw LzmaError("corrupt LZMA2 stream (missing dictionary reset)");
            needDictReset_ = false;
            chunkLeft_ = ((static_cast<std::uint32_t>(in_.next()) << 8) | in_.next()) + 1u;
            uncompressed_ = true;
            inChunk_ = true;
            return;
        }
        if (control < 0x80) throw LzmaError("corrupt LZMA2 stream (control byte)");
        const unsigned mode = (control >> 5) & 3;
        std::uint32_t unpacked = (control & 0x1Fu) << 16;
        unpacked |= static_cast<std::uint32_t>(in_.next()) << 8;
        unpacked |= in_.next();
        std::uint32_t packed = static_cast<std::uint32_t>(in_.next()) << 8;
        packed |= in_.next();
        chunkLeft_ = unpacked + 1u;
        if (mode == 3) {
            core_.resetDict();
            needDictReset_ = false;
        } else if (needDictReset_) {
            throw LzmaError("corrupt LZMA2 stream (missing dictionary reset)");
        }
        if (mode >= 2) {
            const unsigned props = in_.next();
            if ((props % 9) + ((props / 9) % 5) > 4) throw LzmaError("invalid LZMA2 properties");
            core_.setProps(props);
            haveProps_ = true;
        } else if (!haveProps_) {
            throw LzmaError("corrupt LZMA2 stream (missing properties)");
        }
        if (mode >= 1) core_.resetState();
        chunkStart_ = in_.consumed();
        packed_ = packed + 1u;
        core_.initRangeCoder();
        uncompressed_ = false;
        inChunk_ = true;
    }

    Input in_;
    LzmaCore core_;
    std::uint64_t chunkLeft_ = 0, chunkStart_ = 0, packed_ = 0;
    bool uncompressed_ = false, inChunk_ = false, ended_ = false;
    bool needDictReset_ = true, haveProps_ = false;
};

} // namespace

std::unique_ptr<ByteSource> makeLzma1Decoder(ByteSource& in) { return std::make_unique<Lzma1Source>(in); }

std::unique_ptr<ByteSource> makeLzma2Decoder(ByteSource& in, std::uint32_t dictSize) {
    return std::make_unique<Lzma2Source>(in, dictSize);
}

std::unique_ptr<ByteSource> makeLzma2Decoder(ByteSource& in) {
    std::uint8_t prop;
    in.readExact(&prop, 1, "LZMA2 properties");
    if (prop > 40) throw LzmaError("invalid LZMA2 dictionary size");
    const std::uint32_t dict = prop == 40 ? 0xFFFFFFFFu : (2u | (prop & 1u)) << (prop / 2 + 11);
    return makeLzma2Decoder(in, dict);
}

} // namespace openday::formats
