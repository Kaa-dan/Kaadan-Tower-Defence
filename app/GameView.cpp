#include "GameView.h"

#include <algorithm>

namespace ktd {
namespace {

constexpr Color kBackground{0.07f, 0.08f, 0.11f, 1.0f};
constexpr Color kTileOpen{0.15f, 0.17f, 0.22f, 1.0f};
constexpr Color kTileWall{0.28f, 0.29f, 0.33f, 1.0f};
constexpr Color kTowerBody{0.30f, 0.62f, 0.95f, 1.0f};
constexpr Color kTowerBarrel{0.85f, 0.90f, 1.00f, 1.0f};
constexpr Color kSpawn{0.35f, 0.80f, 0.45f, 1.0f};
constexpr Color kGoal{0.90f, 0.35f, 0.35f, 1.0f};
constexpr Color kEnemy{0.95f, 0.45f, 0.35f, 1.0f};
constexpr Color kHealthBack{0.10f, 0.10f, 0.12f, 0.85f};
constexpr Color kHealthFill{0.45f, 0.90f, 0.45f, 1.0f};
constexpr Color kProjectile{1.00f, 0.85f, 0.35f, 1.0f};
constexpr Color kHoverOk{0.40f, 0.90f, 0.50f, 0.35f};
constexpr Color kHoverBad{0.90f, 0.30f, 0.30f, 0.35f};
constexpr Color kRouteHint{0.35f, 0.45f, 0.65f, 0.55f};

constexpr float kMargin = 0.6f;  // tiles of empty space around the board

}  // namespace

void GameView::updateCamera(const World& world, int pixelWidth, int pixelHeight) {
    const float boardW = static_cast<float>(world.grid().width()) + kMargin * 2.0f;
    const float boardH = static_cast<float>(world.grid().height()) + kMargin * 2.0f;

    if (pixelWidth <= 0 || pixelHeight <= 0) {
        return;
    }

    // Letterbox: fit the board without distorting tiles, whatever the window
    // aspect happens to be.
    const float windowAspect = static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
    const float boardAspect = boardW / boardH;

    float viewW = boardW;
    float viewH = boardH;
    if (windowAspect > boardAspect) {
        viewW = boardH * windowAspect;
    } else {
        viewH = boardW / windowAspect;
    }

    const float centerX = static_cast<float>(world.grid().width()) * 0.5f;
    const float centerY = static_cast<float>(world.grid().height()) * 0.5f;

    left_ = centerX - viewW * 0.5f;
    right_ = centerX + viewW * 0.5f;
    bottom_ = centerY - viewH * 0.5f;
    top_ = centerY + viewH * 0.5f;
}

void GameView::apply(Renderer& renderer) const { renderer.setCamera(left_, right_, bottom_, top_); }

Cell GameView::pointerToCell(Vec2 pointer, const InputState& input) const {
    if (input.windowWidth <= 0 || input.windowHeight <= 0) {
        return {-1, -1};
    }
    const float u = pointer.x / static_cast<float>(input.windowWidth);
    // Window coordinates run downward, world coordinates upward.
    const float v = 1.0f - pointer.y / static_cast<float>(input.windowHeight);

    const Vec2 world{left_ + u * (right_ - left_), bottom_ + v * (top_ - bottom_)};
    return Grid::cellOf(world);
}

void GameView::draw(Renderer& renderer, const World& world, Cell hovered,
                    content::TowerDefId selected, bool paused) const {
    const Grid& grid = world.grid();

    // --- board -------------------------------------------------------------
    for (int y = 0; y < grid.height(); ++y) {
        for (int x = 0; x < grid.width(); ++x) {
            const Cell cell{x, y};
            const Tile tile = grid.at(cell);
            if (tile == Tile::Tower) {
                continue;  // drawn with the tower entities below
            }
            const Color color = (tile == Tile::Wall) ? kTileWall : kTileOpen;
            renderer.drawQuad(Grid::centerOf(cell), {0.94f, 0.94f}, color);
        }
    }

    renderer.drawQuad(Grid::centerOf(world.spawn()), {0.94f, 0.94f}, kSpawn);
    renderer.drawQuad(Grid::centerOf(world.goal()), {0.94f, 0.94f}, kGoal);

    // --- the route enemies will take --------------------------------------
    // Reading the flow field back as a polyline shows the player exactly what
    // their next tower placement will change.
    {
        Cell cursor = world.spawn();
        for (int step = 0; step < grid.width() * grid.height(); ++step) {
            const Cell next = world.flowField().next(cursor);
            if (next == cursor) {
                break;
            }
            renderer.drawLine(Grid::centerOf(cursor), Grid::centerOf(next), 0.09f, kRouteHint);
            cursor = next;
        }
    }

    // --- placement preview -------------------------------------------------
    if (grid.inBounds(hovered)) {
        // world.canPlaceAt rather than the free canPlaceTower: this runs every
        // frame, and the World version memoizes on the hover cell and a grid
        // version so a stationary mouse costs two integer compares instead of a
        // Grid copy and a BFS.
        const bool placeable = world.canAfford(selected) && world.canPlaceAt(hovered);
        renderer.drawQuad(Grid::centerOf(hovered), {0.94f, 0.94f},
                          placeable ? kHoverOk : kHoverBad);
        if (placeable) {
            const float range = world.content().tower(selected).range;
            renderer.drawRectOutline(Grid::centerOf(hovered), {range * 2.0f, range * 2.0f}, 0.05f,
                                     kHoverOk);
        }
    }

    // --- towers ------------------------------------------------------------
    for (const Tower& tower : world.towers()) {
        const Vec2 center = Grid::centerOf(tower.cell);
        renderer.drawQuad(center, {0.78f, 0.78f}, kTowerBody);
        // Barrel showing what the tower is tracking.
        renderer.drawLine(center, center + tower.aim * 0.55f, 0.18f, kTowerBarrel);
    }

    // --- enemies -----------------------------------------------------------
    for (const Enemy& enemy : world.enemies()) {
        const float size = world.content().enemy(enemy.type).radius * 2.0f;
        renderer.drawQuad(enemy.position, {size, size}, kEnemy);

        // Health bar, only once damaged.
        if (enemy.health < enemy.maxHealth) {
            const float fraction = std::clamp(enemy.health / enemy.maxHealth, 0.0f, 1.0f);
            const Vec2 barCenter{enemy.position.x, enemy.position.y + 0.34f};
            renderer.drawQuad(barCenter, {0.5f, 0.09f}, kHealthBack);
            renderer.drawQuad({barCenter.x - 0.5f * (1.0f - fraction) * 0.5f, barCenter.y},
                              {0.5f * fraction, 0.07f}, kHealthFill);
        }
    }

    // --- projectiles -------------------------------------------------------
    for (const Projectile& shot : world.projectiles()) {
        renderer.drawQuad(shot.position, {0.14f, 0.14f}, kProjectile);
    }

    // --- state overlays ----------------------------------------------------
    // No text renderer yet, so status is conveyed by a tint plus the window
    // title. A bitmap font is the natural next addition.
    if (world.state() == GameState::Defeat) {
        renderer.drawQuad({(left_ + right_) * 0.5f, (bottom_ + top_) * 0.5f},
                          {right_ - left_, top_ - bottom_}, {0.60f, 0.10f, 0.10f, 0.35f});
    } else if (paused) {
        renderer.drawQuad({(left_ + right_) * 0.5f, (bottom_ + top_) * 0.5f},
                          {right_ - left_, top_ - bottom_}, {0.0f, 0.0f, 0.0f, 0.35f});
    }
}

Color backgroundColor() { return kBackground; }

}  // namespace ktd
