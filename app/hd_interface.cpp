#include "app/hd_interface.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "stb_image.h"

namespace openday::app {
namespace {

constexpr int kMaxJobs = 4;             // pictures loading at a time
constexpr std::int64_t kBudget = 64LL << 20; // texels kept (256 MB)

} // namespace

HdInterface::HdInterface(SDL_Renderer* renderer, std::filesystem::path packDir)
    : renderer_(renderer), packDir_(std::move(packDir)) {}

HdInterface::~HdInterface() { reload(); }

void HdInterface::reload() {
    for (auto& [key, e] : entries_) {
        if (e.job.valid()) e.job.wait();
        if (e.texture) SDL_DestroyTexture(e.texture);
    }
    entries_.clear();
}

std::filesystem::path HdInterface::packFile(const std::filesystem::path& packDir, int key) {
    char name[16];
    std::snprintf(name, sizeof name, "%04d.png", key % 100000);
    return packDir / (key >= 100000 ? "1024" : "640") / name;
}

HdInterface::Picture HdInterface::loadPicture(const std::filesystem::path& file) {
    Picture p;
    std::ifstream f(file, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return p;
    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!data) return p;
    p.width = w;
    p.height = h;
    p.argb.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (std::size_t i = 0; i < p.argb.size(); ++i) {
        const stbi_uc* c = data + i * 4;
        p.argb[i] = static_cast<std::uint32_t>(c[3]) << 24 | static_cast<std::uint32_t>(c[0]) << 16 |
                    static_cast<std::uint32_t>(c[1]) << 8 | static_cast<std::uint32_t>(c[2]);
    }
    stbi_image_free(data);
    return p;
}

SDL_Texture* HdInterface::texture(int key, Entry& e) {
    e.lastUsed = frame_;
    if (e.job.valid() && e.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const Picture p = e.job.get();
        SDL_Texture* t = p.width > 0 && p.height > 0
                             ? SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, p.width, p.height)
                             : nullptr;
        if (t) {
            SDL_UpdateTexture(t, nullptr, p.argb.data(), p.width * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            e.texture = t;
            e.texels = static_cast<std::int64_t>(p.width) * p.height;
        } else {
            e.failed = true; // unreadable: the original pixels show
        }
    }
    if (e.failed || e.texture || e.job.valid()) return e.texture;
    if (!e.packChecked) {
        e.packChecked = true;
        std::error_code ec;
        e.inPack = !packDir_.empty() && std::filesystem::is_regular_file(packFile(packDir_, key), ec);
    }
    if (!e.inPack) return nullptr;
    const int running = static_cast<int>(
        std::count_if(entries_.begin(), entries_.end(), [](const auto& kv) { return kv.second.job.valid(); }));
    if (running < kMaxJobs) e.job = std::async(std::launch::async, loadPicture, packFile(packDir_, key));
    return nullptr;
}

void HdInterface::evict() {
    std::int64_t total = 0;
    for (const auto& [key, e] : entries_) total += e.texels;
    while (total > kBudget) {
        // The least recently used, not shown in this frame.
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.texture && it->second.lastUsed < frame_ && !it->second.job.valid() &&
                (oldest == entries_.end() || it->second.lastUsed < oldest->second.lastUsed))
                oldest = it;
        if (oldest == entries_.end()) break;
        total -= oldest->second.texels;
        SDL_DestroyTexture(oldest->second.texture);
        entries_.erase(oldest);
    }
}

void HdInterface::draw(const render::ImageLayer& layer, const SDL_FRect& picture, float scale) {
    ++frame_;
    // At 1:1 or smaller the original pixels are as sharp as it gets.
    if (scale <= 1.01f) return;
    for (const render::ImageLayer::Item& item : layer.items()) {
        if (item.visible.empty()) continue;
        Entry& e = entries_[item.key];
        SDL_Texture* t = texture(item.key, e);
        if (!t) continue;
        const auto sw = static_cast<float>(item.sprite->width), sh = static_cast<float>(item.sprite->height);
        vertices_.clear();
        indices_.clear();
        const SDL_FColor white{1, 1, 1, 1};
        for (const render::ImageLayer::Rect& r : item.visible) {
            const auto first = static_cast<int>(vertices_.size());
            const auto fx0 = static_cast<float>(r.x), fy0 = static_cast<float>(r.y);
            const auto fx1 = static_cast<float>(r.x + r.w), fy1 = static_cast<float>(r.y + r.h);
            // Sprite coordinates of the frame rectangle.
            const float u0 = (static_cast<float>(item.source.x - item.x) + fx0) / sw;
            const float u1 = (static_cast<float>(item.source.x - item.x) + fx1) / sw;
            const float v0 = (static_cast<float>(item.source.y - item.y) + fy0) / sh;
            const float v1 = (static_cast<float>(item.source.y - item.y) + fy1) / sh;
            const float x0 = picture.x + fx0 * scale, x1 = picture.x + fx1 * scale;
            const float y0 = picture.y + fy0 * scale, y1 = picture.y + fy1 * scale;
            vertices_.push_back({{x0, y0}, white, {u0, v0}});
            vertices_.push_back({{x1, y0}, white, {u1, v0}});
            vertices_.push_back({{x1, y1}, white, {u1, v1}});
            vertices_.push_back({{x0, y1}, white, {u0, v1}});
            for (const int i : {0, 1, 2, 0, 2, 3}) indices_.push_back(first + i);
        }
        SDL_RenderGeometry(renderer_, t, vertices_.data(), static_cast<int>(vertices_.size()), indices_.data(),
                           static_cast<int>(indices_.size()));
    }
    evict();
}

} // namespace openday::app
