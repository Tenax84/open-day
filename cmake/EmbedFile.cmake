# Writes a C++ source that holds the bytes of INPUT as
#   std::span<const std::uint8_t> SYMBOL()
# in namespace openday::embedded. Run with cmake -P.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" length)
math(EXPR size "${length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x..,){16})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}" "// Generated from ${INPUT}; do not edit.
#include <cstdint>
#include <span>
namespace openday::embedded {
namespace {
const std::uint8_t kData[${size}] = {
${bytes}};
} // namespace
std::span<const std::uint8_t> ${SYMBOL}() { return kData; }
} // namespace openday::embedded
")
