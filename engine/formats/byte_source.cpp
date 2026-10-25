#include "formats/byte_source.hpp"

#include <algorithm>
#include <cstring>

namespace openday::formats {

void ByteSource::readExact(std::uint8_t* out, std::size_t n, const char* what) {
    while (n > 0) {
        const std::size_t got = read(out, n);
        if (got == 0) throw StreamError(std::string("truncated ") + what);
        out += got;
        n -= got;
    }
}

void ByteSource::skip(std::uint64_t n, const char* what) {
    std::uint8_t buf[4096];
    while (n > 0) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(n, sizeof buf));
        readExact(buf, want, what);
        n -= want;
    }
}

FileSource::FileSource(const std::filesystem::path& path) : file_(path, std::ios::binary) {
    if (!file_) throw StreamError("cannot open " + path.string());
    file_.seekg(0, std::ios::end);
    size_ = static_cast<std::uint64_t>(file_.tellg());
    file_.seekg(0);
    end_ = size_;
}

void FileSource::setRange(std::uint64_t offset, std::uint64_t length) {
    if (offset > size_) throw StreamError("seek beyond the end of the file");
    pos_ = offset;
    end_ = length > size_ - offset ? size_ : offset + length;
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset));
}

std::size_t FileSource::read(std::uint8_t* out, std::size_t n) {
    const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(n, end_ - pos_));
    if (want == 0) return 0;
    file_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(want));
    const auto got = static_cast<std::size_t>(file_.gcount());
    pos_ += got;
    return got;
}

std::size_t SpanSource::read(std::uint8_t* out, std::size_t n) {
    const std::size_t want = std::min(n, data_.size() - pos_);
    if (want) std::memcpy(out, data_.data() + pos_, want);
    pos_ += want;
    return want;
}

std::size_t LimitedSource::read(std::uint8_t* out, std::size_t n) {
    const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(n, left_));
    if (want == 0) return 0;
    const std::size_t got = base_.read(out, want);
    left_ -= got;
    return got;
}

std::uint8_t Reader::u8() {
    std::uint8_t b;
    bytes(&b, 1);
    return b;
}

std::uint16_t Reader::u16() {
    std::uint8_t b[2];
    bytes(b, 2);
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

std::uint32_t Reader::u32() {
    std::uint8_t b[4];
    bytes(b, 4);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

std::uint64_t Reader::u64() {
    const std::uint64_t lo = u32();
    return lo | (static_cast<std::uint64_t>(u32()) << 32);
}

std::vector<std::uint8_t> Reader::bytes(std::size_t n) {
    std::vector<std::uint8_t> v(n);
    if (n) bytes(v.data(), n);
    return v;
}

std::vector<std::uint8_t> readAll(ByteSource& src) {
    std::vector<std::uint8_t> out;
    std::uint8_t buf[65536];
    for (std::size_t got; (got = src.read(buf, sizeof buf)) > 0;) out.insert(out.end(), buf, buf + got);
    return out;
}

} // namespace openday::formats
