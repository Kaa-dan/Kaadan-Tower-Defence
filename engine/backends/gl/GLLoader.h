#pragma once

#include "platform/Platform.h"

#include <cstddef>

// A minimal OpenGL 3.3 core loader.
//
// Rather than pull in glad/GLEW, we declare only the ~30 entry points this
// renderer actually uses and resolve them through a caller-supplied
// getProcAddress. That keeps the dependency list at exactly one (SDL) and
// works unchanged on macOS, Windows and Linux -- the Windows port needs no
// extra loader library.

namespace ktd::gl {

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

// Constants (values are fixed by the OpenGL spec).
inline constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
inline constexpr GLenum GL_TRIANGLES = 0x0004;
inline constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
inline constexpr GLenum GL_STREAM_DRAW = 0x88E0;
inline constexpr GLenum GL_FLOAT = 0x1406;
inline constexpr GLenum GL_FALSE = 0;
inline constexpr GLenum GL_TRUE = 1;
inline constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
inline constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
inline constexpr GLenum GL_LINK_STATUS = 0x8B82;
inline constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
inline constexpr GLenum GL_BLEND = 0x0BE2;
inline constexpr GLenum GL_SRC_ALPHA = 0x0302;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum GL_UNSIGNED_INT = 0x1405;
inline constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum GL_STATIC_DRAW = 0x88E4;
inline constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
inline constexpr GLenum GL_TEXTURE0 = 0x84C0;
inline constexpr GLenum GL_RGBA = 0x1908;
inline constexpr GLenum GL_RGBA8 = 0x8058;
inline constexpr GLenum GL_NEAREST = 0x2600;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
inline constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
inline constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
inline constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
inline constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr GLenum GL_DEBUG_OUTPUT = 0x92E0;
inline constexpr GLenum GL_DEBUG_OUTPUT_SYNCHRONOUS = 0x8242;

// Windows requires __stdcall for GL entry points; every other platform uses
// the default convention.
#if defined(_WIN32)
#define KTD_GLAPI __stdcall
#else
#define KTD_GLAPI
#endif

#define KTD_GL_FUNCS(X)                                                                    \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                              \
    X(void, Clear, (GLbitfield))                                                           \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                    \
    X(void, Enable, (GLenum))                                                              \
    X(void, BlendFunc, (GLenum, GLenum))                                                   \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                           \
    X(void, BindVertexArray, (GLuint))                                                     \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                  \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                \
    X(void, BindBuffer, (GLenum, GLuint))                                                  \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                       \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                         \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, EnableVertexAttribArray, (GLuint))                                             \
    X(GLuint, CreateShader, (GLenum))                                                      \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))           \
    X(void, CompileShader, (GLuint))                                                       \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                         \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                        \
    X(void, DeleteShader, (GLuint))                                                        \
    X(GLuint, CreateProgram, ())                                                           \
    X(void, AttachShader, (GLuint, GLuint))                                                \
    X(void, LinkProgram, (GLuint))                                                         \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                        \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                       \
    X(void, DeleteProgram, (GLuint))                                                       \
    X(void, UseProgram, (GLuint))                                                          \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                  \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                 \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                          \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void*))                          \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))                    \
    X(void, GenTextures, (GLsizei, GLuint*))                                               \
    X(void, BindTexture, (GLenum, GLuint))                                                 \
    X(void, DeleteTextures, (GLsizei, const GLuint*))                                      \
    X(void, TexImage2D,                                                                    \
      (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))        \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                        \
    X(void, ActiveTexture, (GLenum))                                                       \
    X(void, PixelStorei, (GLenum, GLint))                                                  \
    X(void, Uniform1i, (GLint, GLint))

#define KTD_DECLARE_GL(ret, name, args) extern ret(KTD_GLAPI* gl##name) args;
KTD_GL_FUNCS(KTD_DECLARE_GL)
#undef KTD_DECLARE_GL

// Resolves every entry point above. Returns false and names the first missing
// function in `error` if the driver does not provide it.
//
// Takes the Platform directly rather than a plain function pointer. The plain
// pointer could not capture, which forced the caller to stash the platform in a
// file-local static and hope nothing else was initialising a renderer at the
// same time. backends/ is already permitted to see Platform.h, and Platform.h
// is itself OS-free, so there was never anything to gain by routing around it.
bool loadFunctions(const Platform& platform, char* error, std::size_t errorSize);

// --- OpenGL 4.3, resolved separately -------------------------------------
//
// KHR_debug is not part of 3.3, and loadFunctions fails hard on the first
// symbol it cannot find. Putting this in the list above would mean the renderer
// refuses to start on exactly the old drivers whose problems the debug output
// exists to diagnose. So it is resolved optionally and left null when absent,
// and every use is guarded.
using GLDebugProc = void(KTD_GLAPI*)(GLenum source, GLenum type, GLuint id, GLenum severity,
                                     GLsizei length, const GLchar* message, const void* userParam);

extern void(KTD_GLAPI* glDebugMessageCallback)(GLDebugProc, const void*);

// Resolves the debug entry points if the driver has them. Returns whether it
// did; failure is not an error.
bool loadDebugFunctions(const Platform& platform);

}  // namespace ktd::gl
