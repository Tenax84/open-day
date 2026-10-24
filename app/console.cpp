#include "app/setup.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#endif

namespace openday::app::setup {

void attachParentConsole() {
#ifdef _WIN32
    // Redirected output (pipe or file) already has valid standard handles.
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const bool hasOut = out != nullptr && out != INVALID_HANDLE_VALUE;
    const bool hasErr = err != nullptr && err != INVALID_HANDLE_VALUE;
    if ((hasOut && hasErr) || !AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f = nullptr;
    if (!hasOut) freopen_s(&f, "CONOUT$", "w", stdout);
    if (!hasErr) freopen_s(&f, "CONOUT$", "w", stderr);
#endif
}

} // namespace openday::app::setup
