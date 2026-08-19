#pragma once

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

// Windows requires __stdcall for GL entry points; every other platform uses
// the default convention.
#if defined(_WIN32)
#define KTD_GLAPI __stdcall
#else
#define KTD_GLAPI
#endif

#define KTD_GL_FUNCS(X)                                                                        \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                  \
    X(void, Clear, (GLbitfield))                                                               \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                        \
    X(void, Enable, (GLenum))                                                                  \
    X(void, BlendFunc, (GLenum, GLenum))                                                       \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                               \
    X(void, BindVertexArray, (GLuint))                                                         \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                      \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                    \
    X(void, BindBuffer, (GLenum, GLuint))                                                      \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                           \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                             \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))     \
    X(void, EnableVertexAttribArray, (GLuint))                                                 \
    X(GLuint, CreateShader, (GLenum))                                                          \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))               \
    X(void, CompileShader, (GLuint))                                                           \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                             \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                            \
    X(void, DeleteShader, (GLuint))                                                            \
    X(GLuint, CreateProgram, ())                                                               \
    X(void, AttachShader, (GLuint, GLuint))                                                    \
    X(void, LinkProgram, (GLuint))                                                             \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                            \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                           \
    X(void, DeleteProgram, (GLuint))                                                           \
    X(void, UseProgram, (GLuint))                                                              \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                      \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                     \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))

#define KTD_DECLARE_GL(ret, name, args) extern ret(KTD_GLAPI* gl##name) args;
KTD_GL_FUNCS(KTD_DECLARE_GL)
#undef KTD_DECLARE_GL

using GetProcAddressFn = void* (*)(const char*);

// Resolves every entry point above. Returns false and names the first missing
// function in `error` if the driver does not provide it.
bool loadFunctions(GetProcAddressFn getProc, char* error, std::size_t errorSize);

} // namespace ktd::gl
