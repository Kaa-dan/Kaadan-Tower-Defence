#include "image/LoadTexture.h"

#include "image/Image.h"

#include <cstdint>

namespace ktd {

render::TextureId loadTexture(Renderer& renderer, const Platform& platform, const std::string& path,
                              std::string& error) {
    std::string bytes;
    if (!platform.readFile(path, bytes, error)) {
        return render::kWhiteTexture;
    }

    // unsigned char may alias any object, so viewing the string's chars as
    // bytes is well-defined.
    const Span<const std::uint8_t> view(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                        bytes.size());
    Image image;
    if (!decodePng(view, image, error)) {
        error = path + ": " + error;
        return render::kWhiteTexture;
    }
    return renderer.createTexture(image.width, image.height, image.rgba.data());
}

}  // namespace ktd
