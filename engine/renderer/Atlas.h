#pragma once

#include <cstdint>
#include <vector>

// Runtime texture atlas packing.
//
// Pure data in, pure data out: this file knows nothing about OpenGL, files, or
// pixels. It answers "where does a rectangle of this size go" and the backend
// uploads accordingly. That separation is what lets the packer be tested in the
// core preset, with no window and no driver (ADR 0013).

namespace ktd::render {

// A placement in atlas texels. Excludes the padding gutter reserved around it.
struct AtlasRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const AtlasRect&, const AtlasRect&) = default;
};

// Skyline bottom-left packer.
//
// Skyline rather than a shelf packer because pixel-art sprites in a tower
// defence are wildly uneven -- a 16x16 projectile next to a 64x96 boss -- and a
// shelf wastes the whole row height on the tallest member. Skyline is O(n) per
// placement against a horizon of at most n segments, which at a few hundred
// sprites loaded once at startup is not worth improving on.
class AtlasPacker {
public:
    // `padding` texels are reserved to the right of and above every placement,
    // so no two sprites are ever adjacent. Without it, a sampler at non-integer
    // zoom reads across the seam and neighbouring art bleeds into the edge.
    AtlasPacker(int width, int height, int padding);

    // Returns false and leaves `out` untouched when the sprite does not fit.
    // Callers must check: a packer that silently drops what it cannot place
    // renders a blank sprite and no error, which reads as an art bug rather
    // than a capacity one.
    [[nodiscard]] bool place(int spriteWidth, int spriteHeight, AtlasRect& out);

    int width() const { return width_; }
    int height() const { return height_; }
    int padding() const { return padding_; }

    // Fraction of the atlas covered by placed sprites, excluding padding.
    float occupancy() const;

private:
    // One horizon segment: everything from x to x+width is occupied up to y.
    struct Segment {
        int x = 0;
        int y = 0;
        int width = 0;
    };

    // Lowest y at which `w` texels starting at segment `index` are all clear,
    // or -1 if the run would leave the atlas.
    int fitAt(std::size_t index, int w) const;

    void addSegment(int x, int y, int w);

    int width_ = 0;
    int height_ = 0;
    int padding_ = 0;
    std::int64_t usedArea_ = 0;
    std::vector<Segment> skyline_;
};

}  // namespace ktd::render
