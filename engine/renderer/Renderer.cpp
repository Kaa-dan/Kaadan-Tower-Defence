#include "renderer/Renderer.h"

namespace ktd {
namespace {

constexpr Vec2 kUnrotated{1.0f, 0.0f};

}  // namespace

void Renderer::drawQuad(Vec2 center, Vec2 size, Color color) {
    drawSprite(center, size, kUnrotated, render::kWhiteTexture, {}, color);
}

void Renderer::drawLine(Vec2 from, Vec2 to, float thickness, Color color) {
    const Vec2 delta = to - from;
    const Vec2 forward = normalized(delta);
    const Vec2 center{(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f};
    drawSprite(center, {length(delta), thickness}, forward, render::kWhiteTexture, {}, color);
}

void Renderer::drawRectOutline(Vec2 center, Vec2 size, float thickness, Color color) {
    const float halfW = size.x * 0.5f;
    const float halfH = size.y * 0.5f;

    drawQuad({center.x, center.y + halfH}, {size.x, thickness}, color);  // top
    drawQuad({center.x, center.y - halfH}, {size.x, thickness}, color);  // bottom
    drawQuad({center.x - halfW, center.y}, {thickness, size.y}, color);  // left
    drawQuad({center.x + halfW, center.y}, {thickness, size.y}, color);  // right
}

}  // namespace ktd
