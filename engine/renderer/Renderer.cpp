#include "renderer/Renderer.h"

namespace ktd {

void Renderer::drawRectOutline(Vec2 center, Vec2 size, float thickness, Color color) {
    const float halfW = size.x * 0.5f;
    const float halfH = size.y * 0.5f;

    drawQuad({center.x, center.y + halfH}, {size.x, thickness}, color);  // top
    drawQuad({center.x, center.y - halfH}, {size.x, thickness}, color);  // bottom
    drawQuad({center.x - halfW, center.y}, {thickness, size.y}, color);  // left
    drawQuad({center.x + halfW, center.y}, {thickness, size.y}, color);  // right
}

}  // namespace ktd
