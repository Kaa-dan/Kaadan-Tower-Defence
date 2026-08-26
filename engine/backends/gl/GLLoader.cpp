#include "backends/gl/GLLoader.h"

#include <cstdio>

namespace ktd::gl {

#define KTD_DEFINE_GL(ret, name, args) ret(KTD_GLAPI* gl##name) args = nullptr;
KTD_GL_FUNCS(KTD_DEFINE_GL)
#undef KTD_DEFINE_GL

void(KTD_GLAPI* glDebugMessageCallback)(GLDebugProc, const void*) = nullptr;

bool loadFunctions(const Platform& platform, char* error, std::size_t errorSize) {
#define KTD_LOAD_GL(ret, name, args)                                                         \
    gl##name =                                                                               \
        reinterpret_cast<ret(KTD_GLAPI*) args>(platform.getGraphicsProcAddress("gl" #name)); \
    if (gl##name == nullptr) {                                                               \
        std::snprintf(error, errorSize, "OpenGL entry point missing: gl%s", #name);          \
        return false;                                                                        \
    }

    KTD_GL_FUNCS(KTD_LOAD_GL)
#undef KTD_LOAD_GL

    return true;
}

bool loadDebugFunctions(const Platform& platform) {
    // Deliberately not part of the list above and deliberately not an error.
    // A 3.3 driver has neither of these, and the renderer must still run.
    glDebugMessageCallback = reinterpret_cast<void(KTD_GLAPI*)(GLDebugProc, const void*)>(
        platform.getGraphicsProcAddress("glDebugMessageCallback"));
    return glDebugMessageCallback != nullptr;
}

}  // namespace ktd::gl
