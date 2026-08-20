#include "backends/gl/GLLoader.h"
#include "renderer/Renderer.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace ktd {
namespace {

using namespace ktd::gl;

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uProjection;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

struct Vertex {
    float x, y;
    float r, g, b, a;
};

bool compileShader(GLenum type, const char* source, GLuint& out, std::string& error) {
    out = glCreateShader(type);
    glShaderSource(out, 1, &source, nullptr);
    glCompileShader(out);

    GLint ok = 0;
    glGetShaderiv(out, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[1024] = {};
        glGetShaderInfoLog(out, sizeof(log), nullptr, log);
        error = std::string("shader compile failed: ") + log;
        glDeleteShader(out);
        out = 0;
        return false;
    }
    return true;
}

// Batched sprite renderer. Every drawQuad/drawLine appends six vertices to one
// CPU buffer; endFrame uploads it and issues a single draw call. For a 2D game
// of this scale that is the whole performance story.
class GLRenderer final : public Renderer {
public:
    bool init(const Platform& platform, std::string& error) {
        char loadError[256] = {};
        const auto getProc = [](const char* name) -> void* {
            return sActivePlatform->getGraphicsProcAddress(name);
        };
        sActivePlatform = &platform;
        if (!loadFunctions(getProc, loadError, sizeof(loadError))) {
            error = loadError;
            return false;
        }

        GLuint vertexShader = 0;
        GLuint fragmentShader = 0;
        if (!compileShader(GL_VERTEX_SHADER, kVertexShader, vertexShader, error)) return false;
        if (!compileShader(GL_FRAGMENT_SHADER, kFragmentShader, fragmentShader, error))
            return false;

        program_ = glCreateProgram();
        glAttachShader(program_, vertexShader);
        glAttachShader(program_, fragmentShader);
        glLinkProgram(program_);

        GLint linked = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (linked == 0) {
            char log[1024] = {};
            glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
            error = std::string("shader link failed: ") + log;
            return false;
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        projectionUniform_ = glGetUniformLocation(program_, "uProjection");

        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, r)));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        vertices_.reserve(8192);
        return true;
    }

    ~GLRenderer() override {
        if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
        if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
        if (program_ != 0) glDeleteProgram(program_);
    }

    void beginFrame(int pixelWidth, int pixelHeight, Color clear) override {
        glViewport(0, 0, pixelWidth, pixelHeight);
        glClearColor(clear.r, clear.g, clear.b, clear.a);
        glClear(GL_COLOR_BUFFER_BIT);
        vertices_.clear();
    }

    void setCamera(float left, float right, float bottom, float top) override {
        // Column-major orthographic projection; no depth, this is 2D.
        for (float& value : projection_)
            value = 0.0f;
        projection_[0] = 2.0f / (right - left);
        projection_[5] = 2.0f / (top - bottom);
        projection_[10] = -1.0f;
        projection_[12] = -(right + left) / (right - left);
        projection_[13] = -(top + bottom) / (top - bottom);
        projection_[15] = 1.0f;
    }

    void drawQuad(Vec2 center, Vec2 size, Color color) override {
        const float halfW = size.x * 0.5f;
        const float halfH = size.y * 0.5f;
        const float x0 = center.x - halfW;
        const float x1 = center.x + halfW;
        const float y0 = center.y - halfH;
        const float y1 = center.y + halfH;
        pushQuad({x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, color);
    }

    void drawLine(Vec2 from, Vec2 to, float thickness, Color color) override {
        const Vec2 direction = normalized(to - from);
        const Vec2 normal{-direction.y * thickness * 0.5f, direction.x * thickness * 0.5f};
        pushQuad(from - normal, to - normal, to + normal, from + normal, color);
    }

    void endFrame() override {
        if (vertices_.empty()) {
            return;
        }
        glUseProgram(program_);
        glUniformMatrix4fv(projectionUniform_, 1, GL_FALSE, projection_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
                     vertices_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    }

private:
    void pushVertex(Vec2 p, Color c) { vertices_.push_back({p.x, p.y, c.r, c.g, c.b, c.a}); }

    void pushQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color color) {
        pushVertex(a, color);
        pushVertex(b, color);
        pushVertex(c, color);
        pushVertex(a, color);
        pushVertex(c, color);
        pushVertex(d, color);
    }

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint projectionUniform_ = -1;
    float projection_[16] = {};
    std::vector<Vertex> vertices_;

    // The GL loader takes a plain function pointer, which cannot capture, so
    // the platform is handed over through this file-local pointer during init.
    static const Platform* sActivePlatform;
};

const Platform* GLRenderer::sActivePlatform = nullptr;

}  // namespace

std::unique_ptr<Renderer> createRenderer(const Platform& platform, std::string& error) {
    auto renderer = std::make_unique<GLRenderer>();
    if (!renderer->init(platform, error)) {
        return nullptr;
    }
    return renderer;
}

}  // namespace ktd
