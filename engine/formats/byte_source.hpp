// Pull-based byte streams shared by the archive readers (Inno Setup
// installers, disc images): a file range, a memory span, and a buffered
// reader with the little-endian helpers the header parsers use.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openday::formats {

struct StreamError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class ByteSource {
public:
    virtual ~ByteSource() = default;
    // Reads up to n bytes; returns 0 only at the end of the stream.
    virtual std::size_t read(std::uint8_t* out, std::size_t n) = 0;

    // Reads exactly n bytes; throws StreamError("truncated ...") otherwise.
    void readExact(std::uint8_t* out, std::size_t n, const char* what = "data");
    // Discards n bytes.
    void skip(std::uint64_t n, const char* what = "data");
};

// A byte range of a file.
class FileSource : public ByteSource {
public:
    explicit FileSource(const std::filesystem::path& path);
    std::size_t read(std::uint8_t* out, std::size_t n) override;

    std::uint64_t size() const { return size_; }
    // Restricts reading to [offset, offset + length) of the file.
    void setRange(std::uint64_t offset, std::uint64_t length);
    std::uint64_t position() const { return pos_; }

private:
    std::ifstream file_;
    std::uint64_t size_ = 0, pos_ = 0, end_ = 0;
};

class SpanSource : public ByteSource {
public:
    explicit SpanSource(std::span<const std::uint8_t> data) : data_(data) {}
    std::size_t read(std::uint8_t* out, std::size_t n) override;

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
};

// Passes at most `limit` bytes of another source.
class LimitedSource : public ByteSource {
public:
    LimitedSource(ByteSource& base, std::uint64_t limit) : base_(base), left_(limit) {}
    std::size_t read(std::uint8_t* out, std::size_t n) override;
    std::uint64_t remaining() const { return left_; }

private:
    ByteSource& base_;
    std::uint64_t left_;
};

// Little-endian field reader over a source; throws StreamError at the end.
class Reader {
public:
    explicit Reader(ByteSource& src) : src_(src) {}
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    void bytes(std::uint8_t* out, std::size_t n) { src_.readExact(out, n, "header"); }
    std::vector<std::uint8_t> bytes(std::size_t n);
    void skip(std::uint64_t n) { src_.skip(n, "header"); }
    ByteSource& source() { return src_; }

private:
    ByteSource& src_;
};

// Reads a whole source into memory.
std::vector<std::uint8_t> readAll(ByteSource& src);

} // namespace openday::formats
