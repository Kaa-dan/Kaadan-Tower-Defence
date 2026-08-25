#pragma once

#include "game/World.h"
#include "game/content/Database.h"
#include "platform/Platform.h"
#include "renderer/Renderer.h"

namespace ktd {

// Presentation for the World: camera fitting, drawing, and pixel-to-tile
// conversion. Kept out of game/ because it is about pixels, and out of
// engine/ because it is about this specific game.
class GameView {
public:
    // Recomputes the letterboxed camera for the current window size.
    void updateCamera(const World& world, int pixelWidth, int pixelHeight);

    void apply(Renderer& renderer) const;

    // Converts a pointer position in window points to a tile coordinate.
    Cell pointerToCell(Vec2 pointer, const InputState& input) const;

    // `selected` is the tower type the player is about to build: the preview
    // shows that type's range and affordability, so the two data-driven types
    // are distinguishable before anything is placed.
    void draw(Renderer& renderer, const World& world, Cell hovered, content::TowerDefId selected,
              bool paused) const;

private:
    float left_ = 0.0f;
    float right_ = 1.0f;
    float bottom_ = 0.0f;
    float top_ = 1.0f;
};

// The window clear colour, kept next to the rest of the palette.
Color backgroundColor();

}  // namespace ktd
