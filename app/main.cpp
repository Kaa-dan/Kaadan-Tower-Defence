#include "GameView.h"

#include "game/World.h"
#include "platform/Platform.h"
#include "renderer/Renderer.h"

#include <cstdio>
#include <string>

using namespace ktd;

namespace {

std::string buildTitle(const World& world, bool paused) {
    std::string title = "kaadanTD  |  Wave " + std::to_string(world.waveNumber()) + "   Gold " +
                        std::to_string(world.gold()) + "   Lives " + std::to_string(world.lives()) +
                        "   Killed " + std::to_string(world.enemiesKilled());
    if (world.state() == GameState::Defeat) {
        title += "   [DEFEAT - press R to restart]";
    } else if (paused) {
        title += "   [PAUSED]";
    } else {
        title += "   [click to build, space to pause, R to restart]";
    }
    return title;
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    std::string error;

    auto platform = createPlatform("kaadanTD", 1280, 800, error);
    if (platform == nullptr) {
        std::fprintf(stderr, "platform init failed: %s\n", error.c_str());
        return 1;
    }

    auto renderer = createRenderer(*platform, error);
    if (renderer == nullptr) {
        std::fprintf(stderr, "renderer init failed: %s\n", error.c_str());
        return 1;
    }

    World world;
    GameView view;
    InputState input;
    bool paused = false;

    // Fixed-timestep simulation decoupled from the render rate.
    //
    // The simulation always advances in exact 1/60s steps regardless of how
    // fast the machine draws. That is what makes runs reproducible across
    // platforms and what will let saves, replays and (later) console
    // certification behave predictably.
    double previousTime = platform->now();
    double accumulator = 0.0;
    std::string lastTitle;

    while (platform->pumpEvents(input)) {
        const double currentTime = platform->now();
        double frameTime = currentTime - previousTime;
        previousTime = currentTime;

        // Clamp so a stall (window drag, breakpoint, laptop sleep) cannot make
        // the simulation try to catch up with hundreds of steps at once.
        constexpr double kMaxFrameTime = 0.25;
        if (frameTime > kMaxFrameTime) {
            frameTime = kMaxFrameTime;
        }
        accumulator += frameTime;

        if (input.pausePressed) {
            paused = !paused;
        }
        if (input.restartPressed) {
            world = World();
            paused = false;
        }

        view.updateCamera(world, static_cast<int>(input.windowWidth * input.pixelScale),
                          static_cast<int>(input.windowHeight * input.pixelScale));
        const Cell hovered = view.pointerToCell(input.pointer, input);

        if (input.pointerPressed && !paused && world.state() == GameState::Playing) {
            world.placeTower(hovered);
        }

        while (accumulator >= config::kFixedDt) {
            if (!paused) {
                world.step();
            }
            accumulator -= static_cast<double>(config::kFixedDt);
        }

        const int pixelWidth = static_cast<int>(input.windowWidth * input.pixelScale);
        const int pixelHeight = static_cast<int>(input.windowHeight * input.pixelScale);

        renderer->beginFrame(pixelWidth, pixelHeight, backgroundColor());
        view.apply(*renderer);
        view.draw(*renderer, world, hovered, paused);
        renderer->endFrame();
        platform->present();

        // Setting the title every frame causes visible flicker on some window
        // managers, so only push it when the text actually changes.
        std::string title = buildTitle(world, paused);
        if (title != lastTitle) {
            platform->setWindowTitle(title);
            lastTitle = std::move(title);
        }
    }

    return 0;
}
