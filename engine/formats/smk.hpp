// Decoder for Smacker 2 video files (the "SMK2" variant), video and
// audio. Written from the public description of the Smacker format; see
// docs/formats/FF.md for the subset used by the game data.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "formats/palette.hpp"

namespace openday::formats {

namespace detail {
// One of the four 16-bit Huffman tables of a Smacker file (layout in smk.cpp).
struct SmkTable {
    std::vector<std::int32_t> nodes;
    std::size_t recent[3] = {0, 0, 0}; // slots of the three "recently used" values
};
} // namespace detail

struct SmkError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct SmkAudioTrack {
    bool present = false;
    bool compressed = false;
    bool sixteenBit = false;
    bool stereo = false;
    std::uint32_t sampleRate = 0;
};

// Decoded audio of one track: interleaved PCM, 8-bit unsigned or 16-bit
// signed little-endian, as in a WAV file.
struct SmkAudio {
    std::uint32_t sampleRate = 0;
    int channels = 1;
    int bitsPerSample = 8;
    std::vector<std::uint8_t> pcm;
};

class SmkVideo {
public:
    static constexpr int kAudioTracks = 7;

    // Takes a complete Smacker file. Throws SmkError on malformed headers.
    explicit SmkVideo(std::vector<std::uint8_t> bytes);

    int width() const { return width_; }
    int height() const { return height_; }
    // Number of frames, not counting the ring frame.
    int frameCount() const { return frameCount_; }
    bool hasRingFrame() const { return ringFrame_; }
    // Frame duration in microseconds.
    std::uint32_t frameMicros() const { return frameMicros_; }
    const SmkAudioTrack& audioTrack(int track) const { return audio_[static_cast<std::size_t>(track)]; }

    // Decodes the next frame into frame()/palette(). Frames build on the
    // previous one, so they must be decoded in order; rewind() restarts.
    // Returns false after the last frame (the ring frame is never decoded).
    bool nextFrame();
    void rewind();
    int currentFrame() const { return current_; } // index of the decoded frame, -1 before the first
    const std::vector<std::uint8_t>& frame() const { return pixels_; } // width * height palette indices
    const Palette& palette() const { return palette_; }
    // Video bits of the last decoded frame left unread (only padding in a
    // well-formed file). A validation aid.
    std::size_t unusedVideoBits() const { return unusedBits_; }

    // Concatenates the audio chunks of a track over all frames and decodes them.
    SmkAudio decodeAudio(int track) const;

private:
    std::vector<std::uint8_t> data_;
    int width_ = 0;
    int height_ = 0;
    int frameCount_ = 0;
    bool ringFrame_ = false;
    std::uint32_t frameMicros_ = 0;
    SmkAudioTrack audio_[kAudioTracks];
    std::vector<std::size_t> frameOffsets_;
    std::vector<std::uint32_t> frameSizes_;
    std::vector<std::uint8_t> frameTypes_;
    detail::SmkTable mmap_, mclr_, full_, type_;

    int current_ = -1;
    std::vector<std::uint8_t> pixels_;
    Palette palette_{};
    std::size_t unusedBits_ = 0;
};

// A frame given to writeSmk: palette indices plus the palette to use.
struct SmkFrame {
    std::vector<std::uint8_t> pixels;
    Palette palette{};
};

// Writes a Smacker 2 file. Meant for synthetic test fixtures, not for
// efficient output: every block is coded with a run length of one. Width and
// height must be multiples of 4. audio8 (optional) is stored as one
// compressed 8-bit mono track in the first frame.
std::vector<std::uint8_t> writeSmk(int width, int height, const std::vector<SmkFrame>& frames,
                                   std::uint32_t frameMicros, const std::vector<std::uint8_t>& audio8 = {},
                                   std::uint32_t sampleRate = 22050);

} // namespace openday::formats
