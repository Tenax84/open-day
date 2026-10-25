// LZMA and LZMA2 decompression, as the Inno Setup installers store their
// headers and files (docs/formats/INNO.md). Written from the public LZMA
// specification (the LZMA SDK's lzma-specification.txt); decoding is
// pull-based, so the caller reads only as much as it needs.
#pragma once

#include <memory>
#include <stdexcept>

#include "formats/byte_source.hpp"

namespace openday::formats {

struct LzmaError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// An LZMA stream that starts with the five property bytes (the lc/lp/pb
// byte and the 32-bit dictionary size) and has no size field. It ends at an
// end marker, or wherever the caller stops reading.
std::unique_ptr<ByteSource> makeLzma1Decoder(ByteSource& in);

// An LZMA2 stream that starts with its dictionary-size byte.
std::unique_ptr<ByteSource> makeLzma2Decoder(ByteSource& in);

// An LZMA2 stream with the dictionary size given separately.
std::unique_ptr<ByteSource> makeLzma2Decoder(ByteSource& in, std::uint32_t dictSize);

} // namespace openday::formats
