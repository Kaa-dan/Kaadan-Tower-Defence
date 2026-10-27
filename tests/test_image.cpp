#include "TestFramework.h"
#include "image/Image.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace ktd;

namespace {

// Hand-built PNGs, generated once by a ~20-line zlib script and pasted in.
// Embedded rather than read from tests/data so the decoder's tests need no file
// I/O -- which is the property the decoder itself has.
// 76 bytes
constexpr std::uint8_t kRgba2x2[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D,
    0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
    0x1F, 0x0C, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78, 0x28, 0xA0, 0xDB, 0x77,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

// 72 bytes
constexpr std::uint8_t kRgb2x1[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x7B,
    0x40, 0xE8, 0xDD, 0x00, 0x00, 0x00, 0x0F, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xE0,
    0x12, 0x91, 0x3B, 0x31, 0x2D, 0x05, 0x00, 0x05, 0x07, 0x01, 0xFF, 0x06, 0x7C, 0xB6, 0xFD,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

// 86 bytes
constexpr std::uint8_t kPalette2x1[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x03, 0x00, 0x00, 0x00, 0xC3,
    0xFC, 0x8F, 0xB8, 0x00, 0x00, 0x00, 0x06, 0x50, 0x4C, 0x54, 0x45, 0x01, 0x02, 0x03, 0xFA,
    0xF0, 0xE6, 0xE3, 0xC8, 0xC6, 0x12, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9C, 0x63, 0x60, 0x64, 0x00, 0x00, 0x00, 0x05, 0x00, 0x02, 0xD1, 0x66, 0x33, 0x78, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

// 45 bytes
constexpr std::uint8_t kOversized[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x4E, 0x20, 0x00, 0x00, 0x4E, 0x20, 0x08, 0x06, 0x00, 0x00, 0x00, 0xE3,
    0x70, 0x46, 0x39, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

template <std::size_t N>
Span<const std::uint8_t> bytesOf(const std::uint8_t (&array)[N]) {
    return {array, N};
}

// Texel (x, y) of a decoded image, packed 0xRRGGBBAA for readable failures.
std::uint32_t texel(const Image& image, int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x)) *
                           4u;
    return (std::uint32_t{image.rgba[at]} << 24) | (std::uint32_t{image.rgba[at + 1]} << 16) |
           (std::uint32_t{image.rgba[at + 2]} << 8) | std::uint32_t{image.rgba[at + 3]};
}

}  // namespace

TEST(png_decodes_to_rgba8_top_row_first) {
    Image image;
    std::string error;
    CHECK(decodePng(bytesOf(kRgba2x2), image, error));
    CHECK(error.empty());
    CHECK_EQ(image.width, 2);
    CHECK_EQ(image.height, 2);
    CHECK_EQ(image.rgba.size(), std::size_t{16});

    // Four distinct texels, so a transposed, flipped or mirrored decode cannot
    // pass by symmetry. The file's first row is red, green: it must come out
    // first, because createTexture and quadVertices both assume top row first.
    CHECK_EQ(texel(image, 0, 0), 0xFF0000FFu);
    CHECK_EQ(texel(image, 1, 0), 0x00FF00FFu);
    CHECK_EQ(texel(image, 0, 1), 0x0000FFFFu);
    CHECK_EQ(texel(image, 1, 1), 0xFFFFFF80u);  // alpha survives, unpremultiplied
}

TEST(png_decode_expands_rgb_and_palette_to_rgba) {
    // Art tools export RGB for anything without transparency and indexed
    // colour when asked to optimise. Both must arrive as the same RGBA8 the
    // renderer takes, with missing alpha opaque rather than zero.
    Image rgb;
    std::string error;
    CHECK(decodePng(bytesOf(kRgb2x1), rgb, error));
    CHECK_EQ(rgb.rgba.size(), std::size_t{8});
    CHECK_EQ(texel(rgb, 0, 0), 0x0A141EFFu);
    CHECK_EQ(texel(rgb, 1, 0), 0xC89664FFu);

    Image palette;
    CHECK(decodePng(bytesOf(kPalette2x1), palette, error));
    CHECK_EQ(palette.rgba.size(), std::size_t{8});
    CHECK_EQ(texel(palette, 0, 0), 0xFAF0E6FFu);  // index 1
    CHECK_EQ(texel(palette, 1, 0), 0x010203FFu);  // index 0
}

TEST(png_decode_rejects_garbage_with_a_reason) {
    // A previous image in `out`: a failed decode must leave it intact, not
    // half-overwritten.
    Image image;
    image.width = 7;
    image.height = 3;
    image.rgba.assign(84, 0xAB);

    const std::vector<Span<const std::uint8_t>> inputs = {
        {},                                    // nothing at all
        {kRgba2x2, 8},                         // a signature and no chunks
        {kRgba2x2, sizeof(kRgba2x2) - 20},     // cut off inside IDAT
        {kRgba2x2 + 8, sizeof(kRgba2x2) - 8},  // no signature
    };
    for (const Span<const std::uint8_t> input : inputs) {
        std::string error;
        CHECK(!decodePng(input, image, error));
        CHECK(!error.empty());
        CHECK_EQ(image.width, 7);
        CHECK_EQ(image.height, 3);
        CHECK_EQ(image.rgba.size(), std::size_t{84});
        CHECK_EQ(image.rgba.front(), std::uint8_t{0xAB});
    }

    // A broken zlib header on otherwise intact data. Not a flipped byte in the
    // middle of the stream: stb_image does not check chunk CRCs, so that can
    // decode "successfully" to wrong pixels. The header is what it validates.
    std::vector<std::uint8_t> corrupt(std::begin(kRgba2x2), std::end(kRgba2x2));
    CHECK_EQ(corrupt[41], std::uint8_t{0x78});  // first byte of IDAT's payload
    corrupt[41] = 0x00;
    std::string error;
    CHECK(!decodePng({corrupt.data(), corrupt.size()}, image, error));
    CHECK(!error.empty());
}

TEST(png_decode_refuses_oversized_dimensions) {
    // A 45-byte file whose header claims 20000x20000 RGBA -- 1.6 GB decoded.
    // It has no pixel data, so it would fail anyway; what matters is *why*.
    // The cap must reject it from the header, before stb gets as far as
    // looking for data to allocate for -- so the reason has to be the size.
    static_assert(20000 > kMaxImageDimension);
    Image image;
    std::string error;
    CHECK(!decodePng(bytesOf(kOversized), image, error));
    CHECK(error.find("Very large image") != std::string::npos);
    CHECK(image.rgba.empty());
}
