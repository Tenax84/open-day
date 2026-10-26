#include "app/window_icon.hpp"

#ifdef OPENDAY_WINDOW_ICON
#include <cstdint>
#include <span>

#include "stb_image.h"

namespace openday::embedded {
std::span<const std::uint8_t> windowIcon();
} // namespace openday::embedded
#endif

namespace openday::app {

void setWindowIcon([[maybe_unused]] SDL_Window* window) {
#ifdef OPENDAY_WINDOW_ICON
    const std::span<const std::uint8_t> png = embedded::windowIcon();
    int w = 0, h = 0, n = 0;
    stbi_uc* rgba = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &n, 4);
    if (!rgba) return;
    if (SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, rgba, w * 4)) {
        SDL_SetWindowIcon(window, surface); // copies the pixels
        SDL_DestroySurface(surface);
    }
    stbi_image_free(rgba);
#endif
}

} // namespace openday::app
