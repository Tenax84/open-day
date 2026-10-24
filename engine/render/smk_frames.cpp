#include "render/smk_frames.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include "formats/smk.hpp"

namespace openday::render {

SmkFrames SmkFrames::load(const std::filesystem::path& smk) {
    std::ifstream f(smk, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + smk.string());
    formats::SmkVideo v(std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()));
    SmkFrames out;
    out.width_ = v.width();
    out.height_ = v.height();
    out.frameMicros_ = static_cast<int>(v.frameMicros());
    while (v.nextFrame()) {
        if (out.frames_.empty()) out.palette_ = v.palette();
        out.frames_.push_back(v.frame());
    }
    return out;
}

} // namespace openday::render
