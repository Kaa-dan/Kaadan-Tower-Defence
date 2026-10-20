#pragma once

#include "game/Span.h"
#include "renderer/Atlas.h"
#include "renderer/Vertex.h"

#include <cstdint>
#include <string>
#include <vector>

// Composes many sprites into one texture.
//
// AtlasPacker answers where each sprite goes; this copies the pixels there and
// hands back the UVs that address them. Like the packer it takes pixel buffers,
// never file paths -- decoding sits above ktd_render_core (ADR 0014), so this
// stays testable in the core preset with sprites built in memory.

namespace ktd::render {

// A borrowed sprite: `width * height` RGBA8 texels, top row first.
struct PixelView {
    int width = 0;
    int height = 0;
    const std::uint8_t* rgba = nullptr;
};

struct BuiltAtlas {
    int width = 0;
    int height = 0;
    // Top row first, ready for Renderer::createTexture. Texels no sprite
    // covers -- padding gutters, unused space -- are transparent black.
    std::vector<std::uint8_t> rgba;
    // One per input sprite, in input order. Addresses the sprite's texel edges
    // exactly, so a quad drawn at the sprite's native size maps one texel to
    // one texel with GL_NEAREST.
    std::vector<UvRect> uvs;
    std::vector<AtlasRect> placements;
};

// Packs `sprites` in input order into a `width` x `height` atlas with `padding`
// texels of gutter.
//
// All or nothing: returns false with the index of the first sprite that did not
// fit in `error`, and leaves `out` untouched. An atlas missing one sprite would
// render that sprite as whatever happens to sit at its UVs, which reads as an
// art bug rather than a capacity one -- the same reason place() is nodiscard.
[[nodiscard]] bool buildAtlas(Span<const PixelView> sprites, int width, int height, int padding,
                              BuiltAtlas& out, std::string& error);

}  // namespace ktd::render
