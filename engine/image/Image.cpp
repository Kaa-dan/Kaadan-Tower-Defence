#include "image/Image.h"

#include <climits>
#include <cstddef>
#include <cstring>

// stb_image v2.30, nothings/stb@013ac3beddff3dbffafd5177e7972067cd2b5083.
// Vendored unmodified in third_party/; see third_party/README.md for the hash.
//
// Configured down to what the game needs and nothing it does not:
//   ONLY_PNG       -- the pixel-art pipeline exports PNG. Every other format is
//                     parser surface with no caller.
//   NO_STDIO       -- bytes come from Platform::readFile. The decoder opening
//                     files itself would be a second, untestable way in.
//   NO_HDR/LINEAR  -- float images; pixel art has none.
//   MAX_DIMENSIONS -- rejects an oversized header before allocating.
//   FAILURE_USERMSG -- readable reasons, since they end up in an error string.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_FAILURE_USERMSG
#define STBI_MAX_DIMENSIONS 8192
#define STB_IMAGE_IMPLEMENTATION

// third_party/ is a SYSTEM include directory, which silences the header's own
// warnings on GCC and Clang. MSVC needs telling separately.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace ktd {

static_assert(STBI_MAX_DIMENSIONS == kMaxImageDimension,
              "the cap stb enforces and the cap the header advertises must agree");

bool decodePng(Span<const std::uint8_t> bytes, Image& out, std::string& error) {
    if (bytes.empty()) {
        error = "image is empty";
        return false;
    }
    if (bytes.size() > static_cast<std::size_t>(INT_MAX)) {
        error = "image is larger than 2 GiB";
        return false;
    }

    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                                            &height, &channelsInFile, STBI_rgb_alpha);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        error = std::string("could not decode PNG: ") + (reason != nullptr ? reason : "unknown");
        return false;
    }

    // Copy out of stb's malloc'd buffer rather than adopting it, so the
    // ownership story is a std::vector like every other buffer in the engine.
    const std::size_t size =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    out.width = width;
    out.height = height;
    out.rgba.resize(size);
    std::memcpy(out.rgba.data(), pixels, size);
    stbi_image_free(pixels);
    return true;
}

}  // namespace ktd
