#include "game/Grid.h"

namespace ktd {

Grid::Grid(int width, int height)
    : width_(width), height_(height), tiles_(static_cast<std::size_t>(width * height), Tile::Open) {}

} // namespace ktd
