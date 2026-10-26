// HD interface (docs/mechanics/interface.md, "HD interface"): the interface
// art of a frame (render/image_layer.hpp) drawn again from high-resolution
// versions, over the scaled picture, where the original was visible. The
// versions come from the pack (PNG files in a folder); elements without one
// keep their original pixels.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <vector>

#include "render/image_layer.hpp"

namespace openday::app {

class HdInterface {
public:
    HdInterface(SDL_Renderer* renderer, std::filesystem::path packDir);
    ~HdInterface();
    HdInterface(const HdInterface&) = delete;
    HdInterface& operator=(const HdInterface&) = delete;

    struct Picture {
        int width = 0, height = 0;
        std::vector<std::uint32_t> argb; // 0xAARRGGBB
    };

    // The pack file of a picture key (App::hdKey): <pack>/640/NNNN.png or
    // <pack>/1024/NNNN.png.
    static std::filesystem::path packFile(const std::filesystem::path& packDir, int key);
    // A PNG file as a picture (empty if it cannot be read).
    static Picture loadPicture(const std::filesystem::path& file);

    // `picture`: where the frame is shown in the window (output pixels),
    // `scale` output pixels per frame pixel.
    void draw(const render::ImageLayer& layer, const SDL_FRect& picture, float scale);

    // Forgets every version, so that the pack folder is looked at again (a
    // pack was made while the game ran).
    void reload();

private:
    struct Entry {
        SDL_Texture* texture = nullptr;
        std::future<Picture> job;
        bool packChecked = false, inPack = false, failed = false;
        std::uint64_t lastUsed = 0;
        std::int64_t texels = 0;
    };

    // The entry's texture, made or asked for if needed (nullptr until ready).
    SDL_Texture* texture(int key, Entry& e);
    void evict();

    SDL_Renderer* renderer_;
    std::filesystem::path packDir_;
    std::map<int, Entry> entries_;
    std::uint64_t frame_ = 0;
    std::vector<SDL_Vertex> vertices_;
    std::vector<int> indices_;
};

} // namespace openday::app
