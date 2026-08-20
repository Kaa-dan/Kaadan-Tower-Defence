#pragma once

#include "game/Types.h"

#include <vector>

namespace ktd {

enum class Tile : std::uint8_t {
    Open,   // walkable and buildable
    Wall,   // permanent level geometry, never walkable
    Tower,  // occupied by a tower: blocks walking, not buildable
};

// The level's static playfield. Owns tile state only; it knows nothing about
// enemies, towers-as-entities, or rendering.
class Grid {
public:
    Grid(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    bool inBounds(Cell c) const { return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_; }

    Tile at(Cell c) const { return tiles_[index(c)]; }
    void set(Cell c, Tile t) { tiles_[index(c)] = t; }

    bool walkable(Cell c) const { return inBounds(c) && tiles_[index(c)] == Tile::Open; }

    // Centre of a tile in world units, where one tile is 1.0 x 1.0.
    static Vec2 centerOf(Cell c) {
        return {static_cast<float>(c.x) + 0.5f, static_cast<float>(c.y) + 0.5f};
    }

    static Cell cellOf(Vec2 p) { return {math::floorToInt(p.x), math::floorToInt(p.y)}; }

    int index(Cell c) const { return c.y * width_ + c.x; }

private:
    int width_;
    int height_;
    std::vector<Tile> tiles_;
};

}  // namespace ktd
