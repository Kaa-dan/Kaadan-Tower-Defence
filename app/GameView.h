#pragma once

#include "game/World.h"
#include "game/content/Database.h"
#include "platform/Platform.h"
#include "renderer/Renderer.h"

#include <cstdint>
#include <vector>

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
    //
    // Not const: the board is baked into a static vertex buffer and rebuilt
    // here when the grid changes under it.
    void draw(Renderer& renderer, const World& world, Cell hovered, content::TowerDefId selected);

private:
    // Rebuilds and re-uploads the board if `world`'s grid has changed since the
    // last upload. 384 quads today and 40,000 on a 200x200 level, none of which
    // move between tower placements.
    void syncStaticLayer(Renderer& renderer, const World& world);

    float left_ = 0.0f;
    float right_ = 1.0f;
    float bottom_ = 0.0f;
    float top_ = 1.0f;

    std::vector<StaticQuad> staticQuads_;
    std::uint32_t staticVersion_ = 0;
    bool staticUploaded_ = false;
};

// The window clear colour, kept next to the rest of the palette.
Color backgroundColor();

}  // namespace ktd
