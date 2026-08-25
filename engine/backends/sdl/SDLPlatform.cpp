#include "platform/Platform.h"

#include <SDL3/SDL.h>

namespace ktd {
namespace {

// The macOS/Windows/Linux platform backend.
//
// This file is the ONLY place in the project that knows an operating system
// exists. Everything it does -- window, events, GL context, clock -- is
// expressed upstream through the Platform interface, so a Cocoa, Win32 or
// console backend can replace it without touching a line of game code.
class SDLPlatform final : public Platform {
public:
    bool init(const std::string& title, int width, int height, std::string& error) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            error = std::string("SDL_Init failed: ") + SDL_GetError();
            return false;
        }

        // OpenGL 3.3 core: the highest common denominator across macOS
        // (capped at 4.1) and every desktop Windows/Linux driver.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        window_ = SDL_CreateWindow(
            title.c_str(), width, height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (window_ == nullptr) {
            error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
            return false;
        }

        context_ = SDL_GL_CreateContext(window_);
        if (context_ == nullptr) {
            error = std::string("SDL_GL_CreateContext failed: ") + SDL_GetError();
            return false;
        }

        SDL_GL_SetSwapInterval(1);  // vsync
        startCounter_ = SDL_GetPerformanceCounter();
        counterFrequency_ = static_cast<double>(SDL_GetPerformanceFrequency());
        return true;
    }

    ~SDLPlatform() override {
        if (context_ != nullptr) SDL_GL_DestroyContext(context_);
        if (window_ != nullptr) SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    bool pumpEvents(InputState& out) override {
        // Edge-triggered flags describe this frame only, so clear them first.
        out.pointerPressed = false;
        out.restartPressed = false;
        out.pausePressed = false;
        out.reloadPressed = false;
        out.towerSelection = -1;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    out.quitRequested = true;
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    out.pointer = {event.motion.x, event.motion.y};
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        out.pointer = {event.button.x, event.button.y};
                        out.pointerDown = true;
                        out.pointerPressed = true;
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        out.pointerDown = false;
                    }
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.repeat) break;
                    if (event.key.key == SDLK_ESCAPE) out.quitRequested = true;
                    if (event.key.key == SDLK_R) out.restartPressed = true;
                    if (event.key.key == SDLK_SPACE) out.pausePressed = true;
                    if (event.key.key == SDLK_F5) out.reloadPressed = true;
                    // Number keys pick the tower type to build. 1 is the first
                    // tower in the content file, so adding a type to the data adds
                    // a hotkey for it with no code change.
                    if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
                        out.towerSelection = static_cast<int>(event.key.key - SDLK_1);
                    }
                    break;

                default:
                    break;
            }
        }

        // Window size in logical points, drawable size in physical pixels.
        // They differ on retina displays, and the renderer needs the latter
        // while mouse coordinates arrive in the former.
        int logicalW = 0, logicalH = 0;
        SDL_GetWindowSize(window_, &logicalW, &logicalH);
        int pixelW = 0, pixelH = 0;
        SDL_GetWindowSizeInPixels(window_, &pixelW, &pixelH);

        out.windowWidth = logicalW;
        out.windowHeight = logicalH;
        out.pixelScale =
            logicalW > 0 ? static_cast<float>(pixelW) / static_cast<float>(logicalW) : 1.0f;

        return !out.quitRequested;
    }

    void present() override { SDL_GL_SwapWindow(window_); }

    double now() const override {
        return static_cast<double>(SDL_GetPerformanceCounter() - startCounter_) / counterFrequency_;
    }

    void setWindowTitle(const std::string& title) override {
        SDL_SetWindowTitle(window_, title.c_str());
    }

    bool readFile(const std::string& path, std::string& out, std::string& error) const override {
        std::size_t size = 0;
        void* bytes = SDL_LoadFile(path.c_str(), &size);
        if (bytes == nullptr) {
            error = "cannot read " + path + ": " + SDL_GetError();
            return false;
        }
        out.assign(static_cast<const char*>(bytes), size);
        SDL_free(bytes);
        return true;
    }

    void* getGraphicsProcAddress(const char* name) const override {
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
    }

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    std::uint64_t startCounter_ = 0;
    double counterFrequency_ = 1.0;
};

}  // namespace

std::unique_ptr<Platform> createPlatform(const std::string& title, int width, int height,
                                         std::string& error) {
    auto platform = std::make_unique<SDLPlatform>();
    if (!platform->init(title, width, height, error)) {
        return nullptr;
    }
    return platform;
}

}  // namespace ktd
