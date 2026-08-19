#pragma once

#include "game/Types.h"
#include "platform/Platform.h"

#include <memory>
#include <string>

namespace ktd {

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// A 2D immediate-mode drawing interface, batched internally.
//
// Like Platform, this is the seam a port replaces. A Metal, D3D12 or console
// backend implements the same six calls and the game's draw code is untouched.
class Renderer {
public:
    virtual ~Renderer() = default;

    // Starts a frame and clears. Sizes are in physical pixels.
    virtual void beginFrame(int pixelWidth, int pixelHeight, Color clear) = 0;

    // Defines the visible region in world units (tiles). Everything drawn
    // afterwards is specified in those same units, so the simulation never
    // deals in pixels.
    virtual void setCamera(float left, float right, float bottom, float top) = 0;

    virtual void drawQuad(Vec2 center, Vec2 size, Color color) = 0;
    virtual void drawLine(Vec2 from, Vec2 to, float thickness, Color color) = 0;

    // Flushes the batch.
    virtual void endFrame() = 0;

    // Convenience: an axis-aligned outline, four thin quads.
    void drawRectOutline(Vec2 center, Vec2 size, float thickness, Color color);
};

// Creates the renderer backend compiled into this build. Takes the platform
// because the graphics context, and therefore entry-point resolution, belongs
// to the window.
std::unique_ptr<Renderer> createRenderer(const Platform& platform, std::string& error);

} // namespace ktd
