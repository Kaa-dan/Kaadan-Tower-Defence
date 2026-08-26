#pragma once

#include <bit>
#include <cstdint>
#include <vector>

// Batch planning: the decision half of the renderer.
//
// Everything here is a decision about ordering and grouping, made over plain
// integers. No GL handle, no texture object, no context. `GLRenderer` executes
// what this produces (ADR 0013), which is also why a Metal or D3D12 backend
// would reuse this file unchanged.

namespace ktd::render {

// The visible world-space region. Compared by value: a camera that differs in
// any component starts a new span.
struct Camera {
    float left = 0.0f;
    float right = 1.0f;
    float bottom = 0.0f;
    float top = 1.0f;

    // Bitwise, deliberately -- which is why this is not `= default` and why it
    // does not trip -Wfloat-equal.
    //
    // The question being asked is "is this the same camera the caller already
    // set", not "are these two views numerically close". A tolerance would be
    // actively wrong: two cameras a hair apart are still two projections, and
    // treating them as one draws half the frame through the wrong matrix. The
    // cost of the strict answer is a redundant flush when a caller recomputes a
    // camera and lands one ulp off, which is a few draw calls, not a bug.
    friend bool operator==(const Camera& a, const Camera& b) {
        return bits(a.left) == bits(b.left) && bits(a.right) == bits(b.right) &&
               bits(a.bottom) == bits(b.bottom) && bits(a.top) == bits(b.top);
    }

private:
    static std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
};

// Draw order within a span, packed so one integer compare sorts by both keys.
//
// Layer dominates: a health bar must never sort under the enemy it belongs to
// just because it happens to live in a different texture. Texture breaks the
// tie, which is what collapses a frame's worth of quads into a handful of
// draw calls.
using SortKey = std::uint64_t;

inline constexpr SortKey sortKey(std::uint32_t layer, std::uint32_t texture) {
    return (static_cast<SortKey>(layer) << 32) | static_cast<SortKey>(texture);
}

inline constexpr std::uint32_t layerOf(SortKey key) {
    return static_cast<std::uint32_t>(key >> 32);
}

inline constexpr std::uint32_t textureOf(SortKey key) {
    return static_cast<std::uint32_t>(key & 0xFFFFFFFFu);
}

// One `glDrawElements`: a run of quads sharing a camera and a texture.
struct DrawCall {
    Camera camera;
    std::uint32_t texture = 0;
    std::uint32_t firstQuad = 0;  // index into the planned order, not submission order
    std::uint32_t quadCount = 0;
};

// Accumulates a frame's quads, then works out the cheapest legal order to draw
// them in.
class BatchPlanner {
public:
    void beginFrame();

    // Starts a new span if the camera actually changed. Quads submitted after
    // this cannot be reordered against quads submitted before it.
    void setCamera(Camera camera);

    // Records one quad. Returns its submission index -- the caller keeps its
    // four vertices at that slot and reorders them by `order()` at flush time.
    std::uint32_t submitQuad(std::uint32_t layer, std::uint32_t texture);

    // Sorts each span and merges equal-texture runs. Idempotent within a frame.
    void plan();

    // Submission indices in the order they must be drawn. Valid after plan().
    const std::vector<std::uint32_t>& order() const { return order_; }

    const std::vector<DrawCall>& drawCalls() const { return drawCalls_; }

    std::size_t quadCount() const { return items_.size(); }
    std::size_t drawCallCount() const { return drawCalls_.size(); }

private:
    struct Item {
        SortKey key = 0;
        std::uint32_t span = 0;
        std::uint32_t submission = 0;
    };

    std::vector<Item> items_;
    std::vector<Camera> cameras_;  // one per span, indexed by Item::span
    std::vector<std::uint32_t> order_;
    std::vector<DrawCall> drawCalls_;
    bool planned_ = false;
};

}  // namespace ktd::render
