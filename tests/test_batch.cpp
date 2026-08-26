#include "TestFramework.h"
#include "renderer/Batch.h"

#include <vector>

using namespace ktd::render;

namespace {

constexpr Camera kWorld{-1.0f, 21.0f, -1.0f, 13.0f};
constexpr Camera kHud{0.0f, 1920.0f, 0.0f, 1080.0f};

enum Layer : std::uint32_t { kTiles = 0, kEntities = 10, kOverlay = 20 };

}  // namespace

TEST(sort_key_orders_by_layer_then_texture) {
    // Layer has to dominate. A health bar sorting under the enemy it belongs to
    // because their textures compared that way is the bug this key exists to
    // make impossible.
    CHECK(sortKey(kTiles, 999) < sortKey(kEntities, 0));
    CHECK(sortKey(kEntities, 1) < sortKey(kEntities, 2));
    CHECK(sortKey(kOverlay, 0) > sortKey(kEntities, 0xFFFFFFFFu));

    // Round-trips, including a texture id with the high bit set -- which a
    // signed shift would mangle into the layer field.
    const SortKey key = sortKey(kEntities, 0x80000001u);
    CHECK_EQ(layerOf(key), std::uint32_t{kEntities});
    CHECK_EQ(textureOf(key), 0x80000001u);
}

TEST(sort_within_camera_span_never_reorders_across_it) {
    // The frame-global sort the first design pass specified would have let a
    // screen-space HUD quad sort in among world-space quads whenever their
    // texture ids compared that way -- HUD under the board, intermittently.
    BatchPlanner planner;
    planner.beginFrame();

    planner.setCamera(kWorld);
    const std::uint32_t worldA = planner.submitQuad(kOverlay, 7);  // high layer, world span
    const std::uint32_t worldB = planner.submitQuad(kTiles, 3);

    planner.setCamera(kHud);
    const std::uint32_t hudA = planner.submitQuad(kTiles, 3);  // low layer, HUD span
    const std::uint32_t hudB = planner.submitQuad(kOverlay, 7);

    planner.plan();
    const std::vector<std::uint32_t>& order = planner.order();
    CHECK_EQ(order.size(), std::size_t{4});

    // Within the world span the low layer sorted first...
    CHECK_EQ(order[0], worldB);
    CHECK_EQ(order[1], worldA);
    // ...but neither HUD quad climbed over either world quad, despite hudA
    // carrying the lowest key in the frame.
    CHECK_EQ(order[2], hudA);
    CHECK_EQ(order[3], hudB);
}

TEST(camera_change_mid_frame_flushes_batch) {
    // World-space board plus a screen-space HUD in one frame. Today the
    // projection uploads once in endFrame, so the last setCamera silently wins
    // for everything; this is the property that replaces that.
    BatchPlanner planner;
    planner.beginFrame();

    planner.setCamera(kWorld);
    planner.submitQuad(kEntities, 1);
    planner.submitQuad(kEntities, 1);

    planner.setCamera(kHud);
    planner.submitQuad(kEntities, 1);  // same layer AND texture, different camera

    planner.plan();
    // Identical keys, so only the camera change can be splitting these.
    CHECK_EQ(planner.drawCallCount(), std::size_t{2});
    CHECK(planner.drawCalls()[0].camera == kWorld);
    CHECK(planner.drawCalls()[1].camera == kHud);
    CHECK_EQ(planner.drawCalls()[0].quadCount, 2u);
    CHECK_EQ(planner.drawCalls()[1].quadCount, 1u);

    // Setting the same camera again is not a boundary -- otherwise a caller
    // that re-applies the camera per entity would flush per entity.
    planner.beginFrame();
    planner.setCamera(kWorld);
    planner.submitQuad(kEntities, 1);
    planner.setCamera(kWorld);
    planner.submitQuad(kEntities, 1);
    planner.plan();
    CHECK_EQ(planner.drawCallCount(), std::size_t{1});
}

TEST(draw_calls_stay_under_20_for_a_full_frame) {
    // A recorded command list standing in for a populated board: every tile,
    // the route polyline, ~40 enemies with health bars, ~25 towers with
    // barrels, ~60 projectiles, a hover preview and a HUD. Submitted in the
    // interleaved order GameView actually walks its entities, which is the
    // order that would produce hundreds of draw calls unsorted.
    BatchPlanner planner;
    planner.beginFrame();
    planner.setCamera(kWorld);

    constexpr std::uint32_t kTileTex = 1;
    constexpr std::uint32_t kUnitTex = 2;
    constexpr std::uint32_t kFxTex = 3;
    constexpr std::uint32_t kUiTex = 4;

    for (int i = 0; i < 24 * 16; ++i) {
        planner.submitQuad(kTiles, kTileTex);
    }
    for (int i = 0; i < 40; ++i) {
        planner.submitQuad(kEntities, kUnitTex);  // enemy
        planner.submitQuad(kOverlay, kUiTex);     // health bar back
        planner.submitQuad(kOverlay, kUiTex);     // health bar fill
    }
    for (int i = 0; i < 25; ++i) {
        planner.submitQuad(kEntities, kUnitTex);  // tower body
        planner.submitQuad(kEntities, kFxTex);    // barrel
    }
    for (int i = 0; i < 60; ++i) {
        planner.submitQuad(kEntities, kFxTex);  // projectile
    }
    planner.submitQuad(kOverlay, kUiTex);  // hover preview

    planner.setCamera(kHud);
    for (int i = 0; i < 32; ++i) {
        planner.submitQuad(kOverlay, kUiTex);
    }

    planner.plan();
    CHECK(planner.quadCount() > 500);
    CHECK(planner.drawCallCount() < 20);

    // Every quad is drawn exactly once, and the calls tile the order with no
    // gap -- a batcher that loses a sprite would still pass the count above.
    std::vector<int> drawn(planner.quadCount(), 0);
    std::uint32_t cursor = 0;
    for (const DrawCall& call : planner.drawCalls()) {
        CHECK_EQ(call.firstQuad, cursor);
        for (std::uint32_t i = 0; i < call.quadCount; ++i) {
            ++drawn[planner.order()[call.firstQuad + i]];
        }
        cursor += call.quadCount;
    }
    CHECK_EQ(std::size_t{cursor}, planner.quadCount());
    for (int count : drawn) {
        CHECK_EQ(count, 1);
    }
}
