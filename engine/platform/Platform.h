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
    bool reloadPressed = false;  // F5: re-read the content files

    // Which tower type to build, chosen with the number keys. -1 means the
    // selection did not change this frame.
    int towerSelection = -1;

    // How many simulation ticks to run per real-time tick: 1, 2 or 3, chosen
    // with the F1-F3 keys. -1 means the choice did not change this frame.
    //
    // Fast-forward multiplies steps rather than lengthening dt, so the
    // simulation still only ever advances in exact 1/60s increments and a run
    // played at 3x is bit-identical to the same run played at 1x.
    int speedSelection = -1;

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

    // Reads a whole file into `out`. Returns false with a reason in `error`.
    //
    // The game core parses content but never opens anything -- parsing a string
    // is not file I/O -- so this is the seam where bytes enter. A console
    // backend replaces it with that platform's package reader and nothing
    // upstream changes.
    virtual bool readFile(const std::string& path, std::string& out, std::string& error) const = 0;

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
