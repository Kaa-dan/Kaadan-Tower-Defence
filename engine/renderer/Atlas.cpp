#include "renderer/Atlas.h"

#include <algorithm>

namespace ktd::render {

AtlasPacker::AtlasPacker(int width, int height, int padding)
    : width_(std::max(0, width)),
      height_(std::max(0, height)),
      padding_(std::max(0, padding)) {
    if (width_ > 0 && height_ > 0) {
        skyline_.push_back({0, 0, width_});
    }
}

int AtlasPacker::fitAt(std::size_t index, int w) const {
    const int x = skyline_[index].x;
    if (x + w > width_) {
        return -1;
    }

    // The run has to clear every segment it spans, so the answer is the tallest
    // of them -- not the height of the segment it starts on.
    int y = 0;
    int remaining = w;
    for (std::size_t i = index; i < skyline_.size() && remaining > 0; ++i) {
        y = std::max(y, skyline_[i].y);
        remaining -= skyline_[i].width;
    }
    if (remaining > 0) {
        return -1;  // ran off the end of the horizon
    }
    return y;
}

void AtlasPacker::addSegment(int x, int y, int w) {
    const Segment placed{x, y, w};

    std::vector<Segment> next;
    next.reserve(skyline_.size() + 2);

    for (const Segment& segment : skyline_) {
        const int segmentEnd = segment.x + segment.width;
        if (segmentEnd <= x || segment.x >= x + w) {
            next.push_back(segment);
            continue;
        }
        // Keep whatever pokes out either side of the new segment.
        if (segment.x < x) {
            next.push_back({segment.x, segment.y, x - segment.x});
        }
        if (segmentEnd > x + w) {
            next.push_back({x + w, segment.y, segmentEnd - (x + w)});
        }
    }

    const auto at = std::lower_bound(next.begin(), next.end(), x,
                                     [](const Segment& s, int value) { return s.x < value; });
    next.insert(at, placed);

    // Merge equal-height neighbours, or the horizon grows without bound over a
    // few hundred placements and every fitAt walk gets slower for nothing.
    skyline_.clear();
    for (const Segment& segment : next) {
        if (!skyline_.empty() && skyline_.back().y == segment.y) {
            skyline_.back().width += segment.width;
        } else {
            skyline_.push_back(segment);
        }
    }
}

bool AtlasPacker::place(int spriteWidth, int spriteHeight, AtlasRect& out) {
    if (spriteWidth <= 0 || spriteHeight <= 0 || skyline_.empty()) {
        return false;
    }

    // Reserve the gutter by packing a larger rectangle and reporting the
    // smaller one. Padding on the right and top only: a sprite flush against
    // the atlas's left or bottom edge is fine because sampling is clamped
    // there, but two sprites flush against each other are not.
    const int w = spriteWidth + padding_;
    const int h = spriteHeight + padding_;

    int bestIndex = -1;
    int bestY = 0;
    int bestX = 0;
    for (std::size_t i = 0; i < skyline_.size(); ++i) {
        const int y = fitAt(i, w);
        if (y < 0 || y + h > height_) {
            continue;
        }
        const int x = skyline_[i].x;
        // Bottom-left: lowest wins, leftmost breaks the tie. Deterministic, so
        // two runs of the same content produce byte-identical UVs.
        if (bestIndex < 0 || y < bestY || (y == bestY && x < bestX)) {
            bestIndex = static_cast<int>(i);
            bestY = y;
            bestX = x;
        }
    }

    if (bestIndex < 0) {
        return false;
    }

    addSegment(bestX, bestY + h, w);
    usedArea_ += static_cast<std::int64_t>(spriteWidth) * spriteHeight;
    out = {bestX, bestY, spriteWidth, spriteHeight};
    return true;
}

float AtlasPacker::occupancy() const {
    const std::int64_t total = static_cast<std::int64_t>(width_) * height_;
    if (total <= 0) {
        return 0.0f;
    }
    return static_cast<float>(static_cast<double>(usedArea_) / static_cast<double>(total));
}

}  // namespace ktd::render
