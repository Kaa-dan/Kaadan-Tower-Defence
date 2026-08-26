#include "renderer/Batch.h"

#include <algorithm>

namespace ktd::render {

void BatchPlanner::beginFrame() {
    items_.clear();
    cameras_.clear();
    order_.clear();
    drawCalls_.clear();
    planned_ = false;
}

void BatchPlanner::setCamera(Camera camera) {
    if (!cameras_.empty() && cameras_.back() == camera) {
        return;  // no span boundary for a camera that did not move
    }
    cameras_.push_back(camera);
    planned_ = false;
}

std::uint32_t BatchPlanner::submitQuad(std::uint32_t layer, std::uint32_t texture) {
    if (cameras_.empty()) {
        cameras_.push_back({});  // a quad before any setCamera gets the identity view
    }
    const auto submission = static_cast<std::uint32_t>(items_.size());
    items_.push_back(
        {sortKey(layer, texture), static_cast<std::uint32_t>(cameras_.size() - 1), submission});
    planned_ = false;
    return submission;
}

void BatchPlanner::plan() {
    if (planned_) {
        return;
    }
    order_.clear();
    drawCalls_.clear();

    std::vector<Item> sorted = items_;

    // Sort by span first so spans keep their submission order, then by key
    // within the span. Sorting the frame globally would let a screen-space HUD
    // quad sort in among world-space quads whenever their textures compared
    // that way -- the HUD would render under the board, and only on the frames
    // where the texture ids happened to line up.
    //
    // stable_sort, not sort: equal keys are alpha-blended sprites whose draw
    // order is visible, and introsort's pivot differs between libstdc++, libc++
    // and MSVC. An unstable sort here is a screenshot that differs per
    // platform.
    std::stable_sort(sorted.begin(), sorted.end(), [](const Item& a, const Item& b) {
        if (a.span != b.span) return a.span < b.span;
        return a.key < b.key;
    });

    order_.reserve(sorted.size());
    for (const Item& item : sorted) {
        order_.push_back(item.submission);
    }

    // Merge neighbours that agree on span and texture into one draw call.
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const std::uint32_t texture = textureOf(sorted[i].key);
        if (!drawCalls_.empty() && drawCalls_.back().texture == texture &&
            drawCalls_.back().camera == cameras_[sorted[i].span] &&
            sorted[i - 1].span == sorted[i].span) {
            ++drawCalls_.back().quadCount;
            continue;
        }
        drawCalls_.push_back({cameras_[sorted[i].span], texture, static_cast<std::uint32_t>(i), 1});
    }

    planned_ = true;
}

}  // namespace ktd::render
