#include "Content.h"
#include "GameView.h"

#include "game/World.h"
#include "platform/Platform.h"
#include "renderer/Renderer.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ktd;

namespace {

std::string buildTitle(const World& world, content::TowerDefId selected, int speed,
                       bool contentBroken, RenderStats render) {
    std::string title = "kaadanTD  |  Wave " + std::to_string(world.waveNumber()) + "   Gold " +
                        std::to_string(world.gold()) + "   Lives " + std::to_string(world.lives()) +
                        "   Killed " + std::to_string(world.enemiesKilled());
    title += "   Building: " + world.content().tower(selected).name + " (" +
             std::to_string(world.content().tower(selected).cost) + "g)";
    if (speed > 1) {
        title += "   " + std::to_string(speed) + "x";
    }
#if !defined(NDEBUG)
    // The batching target is 5-15 draw calls a frame. Somewhere visible is the
    // only place a number like that gets noticed regressing.
    title += "   " + std::to_string(render.drawCalls) + " draws / " + std::to_string(render.quads) +
             " quads";
#else
    (void)render;
#endif
    if (contentBroken) {
        title += "   [CONTENT ERROR - see console, F5 to retry]";
    } else if (world.state() == GameState::Defeat) {
        title += "   [DEFEAT - press R to restart]";
    } else if (world.state() == GameState::Victory) {
        title += "   [VICTORY - press R to play again]";
    } else if (world.state() == GameState::Paused) {
        title += "   [PAUSED]";
    } else {
        title += "   [click to build, 1-9 tower, F1-F3 speed, F5 reload, space pause, R restart]";
    }
    return title;
}

}  // namespace

// Window size in logical points times the display scale, as whole pixels.
// Spelled out because int -> float is a lossy conversion the compiler is now
// right to complain about: above 2^24 an int no longer round-trips through a
// float. No window is that wide, but the cast documents that we checked.
int scaledPixels(int points, float scale) {
    return static_cast<int>(static_cast<float>(points) * scale);
}

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

    // Balance is data now, so the first thing the game does is read it. There
    // is no fallback set of numbers compiled in: content that does not load is
    // a broken build, and saying so at startup beats discovering it at wave
    // forty.
    app::ContentSource contentSource(KTD_CONTENT_DIR);
    content::ContentDb content;
    if (!contentSource.reload(*platform, content)) {
        for (const std::string& problem : contentSource.errors()) {
            std::fprintf(stderr, "content: %s\n", problem.c_str());
        }
        return 1;
    }

    World world(content);
    GameView view;
    InputState input;
    content::TowerDefId selectedTower = 0;
    bool contentBroken = false;
    // Simulation ticks per real-time tick. Fast-forward multiplies steps rather
    // than lengthening dt, so a run played at 3x is bit-identical to the same
    // run played at 1x.
    int speed = 1;

    // Every command the player has issued this run, with the tick it landed on.
    // This is what a hot reload replays; Phase 7's replay files are the same
    // log written to disk.
    std::vector<app::RecordedCommand> commands;

    // Fixed-timestep simulation decoupled from the render rate.
    //
    // The simulation always advances in exact 1/60s steps regardless of how
    // fast the machine draws. That is what makes runs reproducible across
    // platforms and what will let saves, replays and (later) console
    // certification behave predictably.
    // The accumulator is double and the simulation's dt is float, and they must
    // not meet implicitly. kFixedDt is converted once, here, into the type the
    // accumulator is compared against -- an implicit promotion inside the loop
    // condition would silently change which side of the boundary a frame falls
    // on. -Wdouble-promotion is on precisely to keep this from creeping back.
    constexpr double kStepSeconds = static_cast<double>(config::kFixedDt);

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
            world.setPaused(world.state() != GameState::Paused);
        }
        if (input.restartPressed) {
            world = World(content);
            commands.clear();
        }
        if (input.speedSelection >= 1 && input.speedSelection <= 3) {
            speed = input.speedSelection;
        }
        if (input.towerSelection >= 0 &&
            static_cast<std::size_t>(input.towerSelection) < content.towers().size()) {
            selectedTower = static_cast<content::TowerDefId>(input.towerSelection);
        }

        // --- hot reload ----------------------------------------------------
        //
        // Re-read the files into a fresh database and, if it is good, rebuild
        // the world and replay the whole run against the new numbers at max
        // speed. The point is not "new balance from here on" -- that would
        // leave a board built under the old numbers and answer nothing. It is
        // "show me this exact run, rebalanced".
        //
        // On a parse error the old database is kept and the game keeps running,
        // because half the times this key is pressed the file has a typo in it.
        if (input.reloadPressed) {
            content::ContentDb reloaded;
            if (contentSource.reload(*platform, reloaded)) {
                const std::uint64_t through = world.tick();
                content = std::move(reloaded);
                if (selectedTower >= content.towers().size()) {
                    selectedTower = 0;
                }
                world = World(content);
                app::resimulate(world, commands, through);
                contentBroken = false;
                std::printf("content reloaded; resimulated %llu ticks\n",
                            static_cast<unsigned long long>(through));
            } else {
                contentBroken = true;
                for (const std::string& problem : contentSource.errors()) {
                    std::fprintf(stderr, "content: %s\n", problem.c_str());
                }
            }
        }

        view.updateCamera(world, scaledPixels(input.windowWidth, input.pixelScale),
                          scaledPixels(input.windowHeight, input.pixelScale));
        const Cell hovered = view.pointerToCell(input.pointer, input);

        // Intent goes into the queue; the outcome comes back as an event after
        // the step. The UI no longer calls placeTower, which is what makes the
        // command log complete by construction -- there is no second path into
        // the simulation to remember to record.
        //
        // Stamped with the tick the world is about to step from, and recorded
        // whether or not it succeeds: a refused placement is part of what the
        // player did, and replaying it re-refuses it identically.
        if (input.pointerPressed && world.state() == GameState::Playing &&
            world.previewPlacement(hovered, selectedTower) == PlacementResult::Placed) {
            const Command command{CommandType::PlaceTower, hovered, selectedTower};
            if (world.enqueue(command)) {
                commands.push_back(app::RecordedCommand{world.tick(), command});
            }
        }

        while (accumulator >= kStepSeconds) {
            for (int i = 0; i < speed; ++i) {
                world.step();
                // Drained per step, not per frame: the queue holds exactly one
                // tick, so a fast-forwarded frame that stepped three times has
                // three ticks' worth of events to collect and only the last
                // would survive a per-frame read.
                for (const Event& event : world.events()) {
                    if (event.type == EventType::Victory) {
                        std::printf("victory on tick %llu\n",
                                    static_cast<unsigned long long>(world.tick()));
                    }
                }
            }
            accumulator -= kStepSeconds;
        }

        const int pixelWidth = scaledPixels(input.windowWidth, input.pixelScale);
        const int pixelHeight = scaledPixels(input.windowHeight, input.pixelScale);

        renderer->beginFrame(pixelWidth, pixelHeight, backgroundColor());
        view.apply(*renderer);
        view.draw(*renderer, world, hovered, selectedTower);
        renderer->endFrame();
        platform->present();

        // Setting the title every frame causes visible flicker on some window
        // managers, so only push it when the text actually changes.
        std::string title =
            buildTitle(world, selectedTower, speed, contentBroken, renderer->stats());
        if (title != lastTitle) {
            platform->setWindowTitle(title);
            lastTitle = std::move(title);
        }
    }

    return 0;
}
