#pragma once

#include "game/Span.h"

#include <cstdint>
#include <string>
#include <vector>

// Image decoding: bytes in, RGBA8 out.
//
// Takes bytes, never a path. Files enter through Platform::readFile and nowhere
// else, so the decoder cannot open anything and a console port swaps the
// package reader without touching this. It also means the decoder builds with
// no SDL and no GL, so the tests in the core preset reach it (ADR 0013).
//
// The one translation unit that compiles stb_image is Image.cpp
// (ADR 0014). Nothing else may include it -- purity_render_core and
// purity_platform fail the build if render_core or game/ ever try.

namespace ktd {

// Decoded pixels: `width * height` RGBA8 texels, top row first -- the layout
// Renderer::createTexture takes, so a decoded image uploads as-is.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// The largest edge decodePng accepts. A sprite sheet bigger than this is a
// mistake, and a PNG header is free to claim any size: the cap is what stops
// a corrupt or hostile file from asking for a multi-gigabyte allocation before
// a single pixel has been read.
inline constexpr int kMaxImageDimension = 8192;

// Decodes a PNG of any colour type into RGBA8. Greyscale, RGB and palette
// images are expanded; missing alpha is opaque.
//
// Returns false with a reason in `error` and leaves `out` untouched on failure,
// so a caller that ignores the result still holds whatever it had before rather
// than a half-written image.
[[nodiscard]] bool decodePng(Span<const std::uint8_t> bytes, Image& out, std::string& error);

}  // namespace ktd
