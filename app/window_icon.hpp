// The program icon on the window (assets/icon). Windows takes it from the
// exe's icon resource (openday.rc) and macOS from the app bundle; elsewhere
// it is set here from the embedded 256 px picture.
#pragma once

#include <SDL3/SDL.h>

namespace openday::app {

void setWindowIcon(SDL_Window* window);

} // namespace openday::app
