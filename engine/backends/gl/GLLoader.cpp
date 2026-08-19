#include "backends/gl/GLLoader.h"

#include <cstdio>

namespace ktd::gl {

#define KTD_DEFINE_GL(ret, name, args) ret(KTD_GLAPI* gl##name) args = nullptr;
KTD_GL_FUNCS(KTD_DEFINE_GL)
#undef KTD_DEFINE_GL

bool loadFunctions(GetProcAddressFn getProc, char* error, std::size_t errorSize) {
#define KTD_LOAD_GL(ret, name, args)                                              \
    gl##name = reinterpret_cast<ret(KTD_GLAPI*) args>(getProc("gl" #name));       \
    if (gl##name == nullptr) {                                                    \
        std::snprintf(error, errorSize, "OpenGL entry point missing: gl%s", #name); \
        return false;                                                             \
    }

    KTD_GL_FUNCS(KTD_LOAD_GL)
#undef KTD_LOAD_GL

    return true;
}

} // namespace ktd::gl
