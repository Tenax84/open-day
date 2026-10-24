#include "formats/game_path.hpp"

#include <system_error>

namespace openday::formats {

namespace fs = std::filesystem;

bool sameNameIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

fs::path findGameFile(const fs::path& root, std::string_view relative) {
    const fs::path exact = root / fs::path(relative);
    std::error_code ec;
    if (fs::exists(exact, ec)) return exact;

    fs::path cur = root;
    std::size_t pos = 0;
    while (pos <= relative.size()) {
        std::size_t slash = relative.find('/', pos);
        if (slash == std::string_view::npos) slash = relative.size();
        const std::string_view part = relative.substr(pos, slash - pos);
        pos = slash + 1;
        if (part.empty()) continue;
        fs::path next = cur / fs::path(part);
        if (!fs::exists(next, ec)) {
            bool found = false;
            for (fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec)) {
                const std::u8string name = it->path().filename().u8string();
                if (sameNameIgnoreCase(std::string_view(reinterpret_cast<const char*>(name.data()), name.size()), part)) {
                    next = it->path();
                    found = true;
                    break;
                }
            }
            if (!found) return exact;
        }
        cur = next;
    }
    return cur;
}

} // namespace openday::formats
