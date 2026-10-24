#include "ui/atlas.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "formats/smk.hpp"

namespace openday::ui {

InterfaceAtlas InterfaceAtlas::fromBytes(std::vector<std::uint8_t> smk, const std::vector<AtlasElement>& wanted) {
    InterfaceAtlas atlas;
    if (wanted.empty()) return atlas;
    std::map<int, std::vector<AtlasElement>> byFrame;
    int last = 0;
    for (const AtlasElement& e : wanted) {
        byFrame[e.frame].push_back(e);
        last = std::max(last, e.frame);
    }
    formats::SmkVideo video(std::move(smk));
    const int w = video.width(), h = video.height();
    while (video.currentFrame() < last && video.nextFrame()) {
        const auto it = byFrame.find(video.currentFrame());
        if (it == byFrame.end()) continue;
        const AtlasElement& e = it->second.front();
        formats::DbiSprite s;
        s.width = std::min(e.width, w);
        s.height = std::min(e.height, h);
        s.pixels.resize(static_cast<std::size_t>(s.width) * static_cast<std::size_t>(s.height));
        s.opaque.assign(s.pixels.size(), 1);
        for (int y = 0; y < s.height; ++y)
            std::copy_n(video.frame().begin() + static_cast<std::ptrdiff_t>(y) * w, s.width,
                        s.pixels.begin() + static_cast<std::ptrdiff_t>(y) * s.width);
        atlas.elements_[e.frame] = std::move(s);
    }
    return atlas;
}

InterfaceAtlas InterfaceAtlas::load(const std::filesystem::path& smk, const std::vector<AtlasElement>& wanted) {
    std::ifstream f(smk, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + smk.string());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return fromBytes(std::move(bytes), wanted);
}

const formats::DbiSprite* InterfaceAtlas::get(int frame) const {
    const auto it = elements_.find(frame);
    return it == elements_.end() ? nullptr : &it->second;
}

} // namespace openday::ui
