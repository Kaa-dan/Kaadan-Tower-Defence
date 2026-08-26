#pragma once

#include "game/Span.h"
#include "game/Types.h"
#include "platform/Platform.h"
#include "renderer/Color.h"
#include "renderer/Vertex.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ktd {

// One entry of the static layer: a quad that does not move between frames.
//
// The board is 384 tiles today and 40,000 on a 200x200 level, resubmitted
// identically every frame for no reason. Handing the whole set over once, and
// again only when it changes, turns that into one upload per grid edit.
struct StaticQuad {
    Vec2 center{};
    Vec2 size{};
    render::UvRect uv{};
    Color tint{};
};

// What the last completed frame cost. The batching target is 5-15 draw calls;
// without a counter that is an aspiration rather than something anyone notices
// regressing.
struct RenderStats {
    std::size_t quads = 0;
    std::size_t drawCalls = 0;
};

// A 2D sprite drawing interface, batched internally.
//
// Like Platform, this is the seam a port replaces. A Metal, D3D12 or console
// backend implements the same handful of calls and the game's draw code is
// untouched -- and reuses ktd_render_core's packing, sorting and batching
// unchanged, because none of that knows what a GL handle is (ADR 0013).
class Renderer {
public:
    virtual ~Renderer() = default;

    // Starts a frame and clears. Sizes are in physical pixels.
    virtual void beginFrame(int pixelWidth, int pixelHeight, Color clear) = 0;

    // Defines the visible region in world units (tiles). Everything drawn
    // afterwards is specified in those same units, so the simulation never
    // deals in pixels.
    //
    // May be called mid-frame: the camera is batch state, and changing it ends
    // the current span. Quads submitted after it are never reordered against
    // quads submitted before it, which is what lets a world-space board and a
    // screen-space HUD share one frame without the HUD sorting under the board.
    virtual void setCamera(float left, float right, float bottom, float top) = 0;

    // Sort layer for everything submitted until the next call. Within a camera
    // span, draws are ordered by layer first and texture second; equal keys
    // keep submission order.
    virtual void setLayer(std::uint32_t layer) = 0;

    // Uploads `width * height` RGBA8 texels, top row first, and returns a
    // handle. Returns kWhiteTexture on failure, so a missing texture draws as
    // an untinted white quad rather than sampling garbage.
    virtual render::TextureId createTexture(int width, int height, const std::uint8_t* rgba) = 0;

    // Replaces the static layer, which draws before the batch's first span and
    // in that span's camera. Passing an empty span clears it.
    //
    // Its position in the layer order is fixed and deliberate: bottom. Sorting
    // it with the rest would mean uploading it with the rest, which is the cost
    // this exists to avoid, so "sorted by layer" would quietly have meant
    // "sorted by layer, except the tiles".
    virtual void setStaticQuads(Span<const StaticQuad> quads, render::TextureId texture) = 0;

    // The only drawing primitive. Everything below is a call to this one.
    //
    // `forward` is a unit direction, not an angle: the simulation already keeps
    // aim as a vector, and an angle API would round-trip it through atan2 and
    // sin/cos to arrive back where it started (ADR 0015). {1, 0} is unrotated.
    virtual void drawSprite(Vec2 center, Vec2 size, Vec2 forward, render::TextureId texture,
                            render::UvRect uv, Color tint) = 0;

    // Flushes the batch.
    virtual void endFrame() = 0;

    virtual RenderStats stats() const = 0;

    // An untextured quad: the 1x1 white texture tinted, so there is exactly one
    // draw path and untextured art costs no extra state change.
    void drawQuad(Vec2 center, Vec2 size, Color color);

    // A thick line, drawn as one rotated quad. A zero-length line draws
    // nothing, because `normalized` returns a zero vector and the quad
    // collapses -- which is the right answer for a line from a point to itself.
    void drawLine(Vec2 from, Vec2 to, float thickness, Color color);

    // An axis-aligned outline, four thin quads.
    void drawRectOutline(Vec2 center, Vec2 size, float thickness, Color color);
};

// Creates the renderer backend compiled into this build. Takes the platform
// because the graphics context, and therefore entry-point resolution, belongs
// to the window.
std::unique_ptr<Renderer> createRenderer(const Platform& platform, std::string& error);

}  // namespace ktd
