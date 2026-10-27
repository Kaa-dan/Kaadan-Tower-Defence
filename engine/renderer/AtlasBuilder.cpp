#include "renderer/AtlasBuilder.h"

#include <cstddef>
#include <cstring>
#include <utility>

namespace ktd::render {

bool buildAtlas(Span<const PixelView> sprites, int width, int height, int padding, BuiltAtlas& out,
                std::string& error) {
    if (width <= 0 || height <= 0) {
        error = "atlas size must be positive";
        return false;
    }

    AtlasPacker packer(width, height, padding);

    std::vector<AtlasRect> placements;
    placements.reserve(sprites.size());
    for (std::size_t i = 0; i < sprites.size(); ++i) {
        const PixelView& sprite = sprites[i];
        if (sprite.rgba == nullptr) {
            error = "atlas sprite " + std::to_string(i) + " has no pixels";
            return false;
        }
        AtlasRect rect{};
        if (!packer.place(sprite.width, sprite.height, rect)) {
            error = "atlas sprite " + std::to_string(i) + " (" + std::to_string(sprite.width) +
                    "x" + std::to_string(sprite.height) + ") does not fit in a " +
                    std::to_string(width) + "x" + std::to_string(height) + " atlas";
            return false;
        }
        placements.push_back(rect);
    }

    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    std::vector<std::uint8_t> pixels(stride * static_cast<std::size_t>(height), 0);
    std::vector<UvRect> uvs;
    uvs.reserve(sprites.size());

    const float invW = 1.0f / static_cast<float>(width);
    const float invH = 1.0f / static_cast<float>(height);

    for (std::size_t i = 0; i < sprites.size(); ++i) {
        const PixelView& sprite = sprites[i];
        const AtlasRect& rect = placements[i];

        // Row for row: the packer's y is an atlas row index, and both buffers
        // are top row first, so sprite row r lands on atlas row rect.y + r.
        const std::size_t rowBytes = static_cast<std::size_t>(sprite.width) * 4u;
        for (int row = 0; row < sprite.height; ++row) {
            const std::uint8_t* src = sprite.rgba + static_cast<std::size_t>(row) * rowBytes;
            std::uint8_t* dst = pixels.data() + static_cast<std::size_t>(rect.y + row) * stride +
                                static_cast<std::size_t>(rect.x) * 4u;
            std::memcpy(dst, src, rowBytes);
        }

        // v0 is the sprite's top row: the texture is uploaded top row first,
        // and quadVertices puts v0 on the quad's top edge.
        uvs.push_back({static_cast<float>(rect.x) * invW, static_cast<float>(rect.y) * invH,
                       static_cast<float>(rect.x + rect.width) * invW,
                       static_cast<float>(rect.y + rect.height) * invH});
    }

    out.width = width;
    out.height = height;
    out.rgba = std::move(pixels);
    out.uvs = std::move(uvs);
    out.placements = std::move(placements);
    return true;
}

}  // namespace ktd::render
