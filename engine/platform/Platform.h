#pragma once

#include "game/Types.h"

#include <memory>
#include <string>

namespace ktd {

// What the game learns about the outside world each frame. Deliberately
// abstract: no SDL types, no OS types. A console backend fills in the same
// struct from a gamepad instead of a mouse and nothing upstream changes.
struct InputState {
    bool quitRequested = false;

    Vec2 pointer{};               // in window pixels, origin top-left
    bool pointerDown = false;     // held this frame
    bool pointerPressed = false;  // went down this frame (edge)

    bool restartPressed = false;
    bool pausePressed = false;

    int windowWidth = 0;
    int windowHeight = 0;
    float pixelScale = 1.0f;  // 2.0 on a retina display
};

// The boundary between the game and the operating system: a window, an input
// stream, a clock, and a buffer swap.
//
// Everything OS-specific lives behind this interface. Porting to a new
// platform means writing one more implementation of it and changing no game
// code whatsoever.
class Platform {
public:
    virtual ~Platform() = default;

    // Drains the OS event queue into `out`. Returns false when the app should
    // exit.
    virtual bool pumpEvents(InputState& out) = 0;

    // Presents the frame that was just rendered.
    virtual void present() = 0;

    // Monotonic seconds since startup. Not wall-clock time.
    virtual double now() const = 0;

    virtual void setWindowTitle(const std::string& title) = 0;

    // Resolves a graphics-API entry point. The platform owns the graphics
    // context (it belongs to the window), so it also owns symbol lookup. The
    // renderer backend asks through here instead of linking the OS GL library
    // itself.
    virtual void* getGraphicsProcAddress(const char* name) const = 0;
};

// Creates the platform backend compiled into this build. Returns nullptr on
// failure, with the reason written to `error`.
std::unique_ptr<Platform> createPlatform(const std::string& title, int width, int height,
                                         std::string& error);

}  // namespace ktd
