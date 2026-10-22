#include "formats/ff.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "formats/binary.hpp"

namespace openday::formats {
namespace {

using binary::putU32;
using binary::readU32;

constexpr std::uint8_t kMagic[4] = {'M', 'F', 'F', 0};

} // namespace

std::string FfArchive::normalize(std::string_view name) {
    std::string s(name);
    for (char& c : s) {
        if (c == '/')
            c = '\\';
        else if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

void FfArchive::parse(const std::uint8_t* dir, std::size_t dirSize, std::uint64_t archiveSize) {
    archiveSize_ = archiveSize;
    const binary::Bytes d(dir, dirSize);
    const std::size_t n = readU32(d, 4);
    const std::uint64_t dirEnd = kHeaderSize + std::uint64_t{n} * kEntrySize;
    if (dirEnd > archiveSize) throw FfError("directory larger than file");
    if (dirSize < dirEnd) throw FfError("directory truncated");

    entries_.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t at = kHeaderSize + i * kEntrySize;
        const auto* name = reinterpret_cast<const char*>(dir + at);
        const auto len = static_cast<std::size_t>(std::find(name, name + kNameSize, '\0') - name);
        if (len == kNameSize) throw FfError("entry " + std::to_string(i) + ": name not terminated");
        FfEntry e;
        e.name.assign(name, len);
        e.offset = readU32(d, at + kNameSize);
        if (e.offset < dirEnd || e.offset > archiveSize) throw FfError(e.name + ": offset outside the data area");
        if (!index_.emplace(normalize(e.name), i).second) throw FfError(e.name + ": duplicate name");
        entries_.push_back(std::move(e));
    }

    // There is no size field: an entry runs up to the next entry's data.
    std::vector<std::uint32_t> starts;
    starts.reserve(n);
    for (const FfEntry& e : entries_) starts.push_back(e.offset);
    std::sort(starts.begin(), starts.end());
    for (FfEntry& e : entries_) {
        const auto next = std::upper_bound(starts.begin(), starts.end(), e.offset);
        const std::uint64_t end = next == starts.end() ? archiveSize : *next;
        e.size = static_cast<std::uint32_t>(end - e.offset);
    }
}

FfArchive FfArchive::open(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw FfError("cannot open " + path.string());
    const std::uint64_t size = std::filesystem::file_size(path);
    if (size > 0xFFFFFFFFu) throw FfError("file too large");

    std::vector<std::uint8_t> dir(kHeaderSize);
    if (size < kHeaderSize || !in.read(reinterpret_cast<char*>(dir.data()), kHeaderSize))
        throw FfError("file too small");
    if (!std::equal(std::begin(kMagic), std::end(kMagic), dir.begin())) throw FfError("bad magic");
    const std::uint64_t dirEnd = kHeaderSize + std::uint64_t{readU32(dir, 4)} * kEntrySize;
    if (dirEnd > size) throw FfError("directory larger than file");
    dir.resize(static_cast<std::size_t>(dirEnd));
    if (!in.read(reinterpret_cast<char*>(dir.data() + kHeaderSize), static_cast<std::streamsize>(dirEnd - kHeaderSize)))
        throw FfError("cannot read directory");

    FfArchive a;
    a.path_ = path;
    a.parse(dir.data(), dir.size(), size);
    return a;
}

FfArchive FfArchive::fromBytes(std::vector<std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) throw FfError("file too small");
    if (!std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin())) throw FfError("bad magic");
    FfArchive a;
    a.memory_ = std::move(bytes);
    a.inMemory_ = true;
    a.parse(a.memory_.data(), a.memory_.size(), a.memory_.size());
    return a;
}

std::optional<std::size_t> FfArchive::find(std::string_view name) const {
    const auto it = index_.find(normalize(name));
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::uint8_t> FfArchive::read(std::size_t i) const {
    const FfEntry& e = entries_.at(i);
    if (inMemory_) {
        const auto begin = memory_.begin() + static_cast<std::ptrdiff_t>(e.offset);
        return {begin, begin + static_cast<std::ptrdiff_t>(e.size)};
    }
    std::ifstream in(path_, std::ios::binary);
    std::vector<std::uint8_t> out(e.size);
    if (!in || !in.seekg(e.offset) || !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(e.size)))
        throw FfError("cannot read " + e.name);
    return out;
}

std::vector<std::uint8_t> writeFf(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files) {
    std::vector<std::uint8_t> out = {'M', 'F', 'F', 0};
    putU32(out, static_cast<std::uint32_t>(files.size()));
    auto offset = static_cast<std::uint32_t>(FfArchive::kHeaderSize + files.size() * FfArchive::kEntrySize);
    for (const auto& [name, data] : files) {
        if (name.size() >= FfArchive::kNameSize) throw FfError(name + ": name too long");
        const std::size_t at = out.size();
        out.insert(out.end(), name.begin(), name.end());
        out.resize(at + FfArchive::kNameSize, 0);
        putU32(out, offset);
        putU32(out, 0); // reserved
        offset += static_cast<std::uint32_t>(data.size());
    }
    for (const auto& file : files) out.insert(out.end(), file.second.begin(), file.second.end());
    return out;
}

} // namespace openday::formats
