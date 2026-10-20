#pragma once

#include "platform/Platform.h"
#include "renderer/Renderer.h"

#include <string>

namespace ktd {

// Reads a PNG through the platform, decodes it, and uploads it.
//
// The three steps live in three places on purpose: bytes enter through
// Platform::readFile, decoding is ktd_image, and the upload is the backend. This
// file is only the join, which is why it is compiled into ktd_engine rather
// than either library -- it is the one place that needs a Platform and a
// Renderer at once.
//
// Follows createTexture's contract: on failure it returns kWhiteTexture with the
// reason in `error`, so a missing sprite draws as a white quad, visibly wrong,
// instead of sampling garbage.
render::TextureId loadTexture(Renderer& renderer, const Platform& platform, const std::string& path,
                              std::string& error);

}  // namespace ktd
