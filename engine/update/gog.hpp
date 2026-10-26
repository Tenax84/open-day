// The parts of the GOG download that need no network (docs/gog-download.md):
// the sign-in addresses, reading the sign-in code out of the address GOG's
// page ends on, and reading GOG's answers. app/gog_download.* does the
// requests.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openday::update::gog {

// GOG's product number of Fallen Haven: Liberation Day.
inline constexpr std::string_view kProduct = "1119459074";

// The page where the user signs in to GOG. It ends on an address that starts
// with landingAddress() and carries the code loginCode() reads.
std::string signInAddress();

// The start of the address GOG's sign-in page ends on.
std::string_view landingAddress();

// The sign-in code in what the user copied or the panel kept: the whole
// address GOG's page ended on, or the code alone. Spaces and line ends
// around it are ignored. nullopt when the text holds none.
std::optional<std::string> loginCode(std::string_view text);

// GET: exchanges the code for a token.
std::string tokenAddress(std::string_view code);

// The access token of the token request's answer; empty when it has none.
std::string accessToken(std::string_view json);

// GET (no sign-in needed): the product with its downloads.
std::string productAddress();

// One file of an installer: the address that gives its real address (asked
// with the token) and its size.
struct InstallerFile {
    std::string downlink; // https://api.gog.com/...
    std::uint64_t size = 0;
};

// The files of the product's Windows installer: the English one, else any.
// Empty when the answer lists none (or is not JSON). The first file is the
// installer itself; any others are its .bin parts.
std::vector<InstallerFile> windowsInstaller(std::string_view productJson);

// The file's real address in the answer of its downlink; empty when none.
std::string secureLink(std::string_view json);

// The last part of an address's path, unescaped, when it is a plain file
// name (letters, digits and . _ - ( ), not starting with a dot); else empty.
std::string fileNameOf(std::string_view address);

// True when the installer's bytes may come from this address: https on a
// GOG host.
bool downloadHost(std::string_view address);

// A query value with every byte but the unreserved ones escaped.
std::string escaped(std::string_view text);

} // namespace openday::update::gog
