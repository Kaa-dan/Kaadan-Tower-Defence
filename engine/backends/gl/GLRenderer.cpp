#include "backends/gl/GLLoader.h"
#include "renderer/Batch.h"
#include "renderer/Projection.h"
#include "renderer/Renderer.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace ktd {
namespace {

using namespace ktd::gl;
using render::Vertex;

constexpr const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vUv;
out vec4 vColor;
void main() {
    vUv = aUv;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 fragColor;
void main() {
    fragColor = texture(uTexture, vUv) * vColor;
}
)";

// --- RAII ----------------------------------------------------------------
//
// Every GL object below used to be a raw GLuint deleted by hand in one
// destructor, which is why two of the three early returns in init() leaked. A
// handle that deletes itself makes the failure paths correct by construction
// rather than by remembering.

void destroyBuffer(GLuint id) { glDeleteBuffers(1, &id); }
void destroyVertexArray(GLuint id) { glDeleteVertexArrays(1, &id); }
void destroyTexture(GLuint id) { glDeleteTextures(1, &id); }
void destroyProgram(GLuint id) { glDeleteProgram(id); }
void destroyShader(GLuint id) { glDeleteShader(id); }

template <void (*Destroy)(GLuint)>
class GlHandle {
public:
    GlHandle() = default;
    explicit GlHandle(GLuint id) : id_(id) {}
    ~GlHandle() { reset(); }

    GlHandle(const GlHandle&) = delete;
    GlHandle& operator=(const GlHandle&) = delete;

    GlHandle(GlHandle&& other) noexcept : id_(other.release()) {}
    GlHandle& operator=(GlHandle&& other) noexcept {
        if (this != &other) {
            reset();
            id_ = other.release();
        }
        return *this;
    }

    GLuint get() const { return id_; }
    explicit operator bool() const { return id_ != 0; }

    GLuint release() {
        const GLuint id = id_;
        id_ = 0;
        return id;
    }

    void reset() {
        if (id_ != 0) {
            Destroy(id_);
            id_ = 0;
        }
    }

private:
    GLuint id_ = 0;
};

using BufferHandle = GlHandle<&destroyBuffer>;
using VertexArrayHandle = GlHandle<&destroyVertexArray>;
using TextureHandle = GlHandle<&destroyTexture>;
using ProgramHandle = GlHandle<&destroyProgram>;
using ShaderHandle = GlHandle<&destroyShader>;

// GL's generators write through a GLuint*, so they cannot fill a handle
// directly. Wrapping them here keeps every generated name owned from the
// moment it exists, with no window in which a raw id is live.
BufferHandle genBuffer() {
    GLuint id = 0;
    glGenBuffers(1, &id);
    return BufferHandle{id};
}

VertexArrayHandle genVertexArray() {
    GLuint id = 0;
    glGenVertexArrays(1, &id);
    return VertexArrayHandle{id};
}

TextureHandle genTexture() {
    GLuint id = 0;
    glGenTextures(1, &id);
    return TextureHandle{id};
}

bool compileShader(GLenum type, const char* source, ShaderHandle& out, std::string& error) {
    ShaderHandle shader{glCreateShader(type)};
    glShaderSource(shader.get(), 1, &source, nullptr);
    glCompileShader(shader.get());

    GLint ok = 0;
    glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[1024] = {};
        glGetShaderInfoLog(shader.get(), sizeof(log), nullptr, log);
        error = std::string("shader compile failed: ") + log;
        return false;  // the handle deletes it on the way out
    }
    out = std::move(shader);
    return true;
}

#if !defined(NDEBUG)
void KTD_GLAPI debugCallback(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar* message,
                             const void*) {
    std::fprintf(stderr, "[gl] %s\n", message);
}
#endif

// Describes the vertex layout for whichever VBO is bound. Written once and
// called from both the dynamic and the static VAO, because a layout that is
// spelled out twice is a layout that will disagree with itself.
void declareVertexFormat() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, u)));
    // Normalized unsigned bytes: four bytes in the buffer, four floats in the
    // shader. The colour is the only attribute that is not already a float.
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, rgba)));
}

// Six indices per quad over four vertices, which is the whole point of the
// index buffer: 4 vertices instead of 6 is a third less upload bandwidth for
// every quad in the frame.
void buildQuadIndices(std::vector<GLuint>& out, std::size_t quads) {
    out.clear();
    out.reserve(quads * 6);
    for (std::size_t quad = 0; quad < quads; ++quad) {
        const auto base = static_cast<GLuint>(quad * 4);
        for (const std::uint32_t offset : render::kQuadIndices) {
            out.push_back(base + offset);
        }
    }
}

// Batched sprite renderer.
//
// The decisions -- what order to draw in, where to break a draw call, what the
// projection is -- belong to ktd_render_core and are tested there. This class
// executes them: it uploads what the planner ordered and issues one
// glDrawElements per DrawCall it was handed (ADR 0013).
class GLRenderer final : public Renderer {
public:
    bool init(const Platform& platform, std::string& error) {
        char loadError[256] = {};
        if (!loadFunctions(platform, loadError, sizeof(loadError))) {
            error = loadError;
            return false;
        }

#if !defined(NDEBUG)
        // Optional by design: this is 4.3, and the 3.3 drivers whose problems
        // it would report are exactly the ones that do not have it.
        if (loadDebugFunctions(platform)) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(&debugCallback, nullptr);
        }
#endif

        if (!buildProgram(error)) {
            return false;
        }

        vao_ = genVertexArray();
        glBindVertexArray(vao_.get());
        vbo_ = genBuffer();
        glBindBuffer(GL_ARRAY_BUFFER, vbo_.get());
        declareVertexFormat();
        // The element array binding is VAO state, so this is bound once here
        // and never again. Growing it later re-uploads through the same name,
        // which leaves the binding alone.
        ibo_ = genBuffer();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_.get());

        staticVao_ = genVertexArray();
        glBindVertexArray(staticVao_.get());
        staticVbo_ = genBuffer();
        glBindBuffer(GL_ARRAY_BUFFER, staticVbo_.get());
        declareVertexFormat();
        staticIbo_ = genBuffer();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, staticIbo_.get());

        glBindVertexArray(0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Rows of an RGBA8 image are 4-byte aligned by definition, but the
        // default of 4 would misread a future single-channel font atlas. Set
        // it once, here, rather than discovering it later as sheared glyphs.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        if (!createWhiteTexture()) {
            error = "failed to create the default white texture";
            return false;
        }

        vertices_.reserve(8192 * 4);
        ordered_.reserve(8192 * 4);
        return true;
    }

    void beginFrame(int pixelWidth, int pixelHeight, Color clear) override {
        glViewport(0, 0, pixelWidth, pixelHeight);
        glClearColor(clear.r, clear.g, clear.b, clear.a);
        glClear(GL_COLOR_BUFFER_BIT);

        planner_.beginFrame();
        vertices_.clear();
        layer_ = 0;
        planner_.setCamera(camera_);  // the camera survives the frame boundary
    }

    void setCamera(float left, float right, float bottom, float top) override {
        camera_ = render::Camera{left, right, bottom, top};
        planner_.setCamera(camera_);
    }

    void setLayer(std::uint32_t layer) override { layer_ = layer; }

    render::TextureId createTexture(int width, int height, const std::uint8_t* rgba) override {
        if (width <= 0 || height <= 0 || rgba == nullptr) {
            return render::kWhiteTexture;
        }
        TextureHandle texture = genTexture();
        if (!texture) {
            return render::kWhiteTexture;
        }
        glBindTexture(GL_TEXTURE_2D, texture.get());
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, rgba);
        // Pixel art: nearest, and clamped so a sprite at the atlas edge cannot
        // wrap around and sample the opposite side.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));

        const auto id = static_cast<render::TextureId>(textures_.size());
        textures_.push_back(std::move(texture));
        return id;
    }

    void setStaticQuads(Span<const StaticQuad> quads, render::TextureId texture) override {
        staticTexture_ = texture;
        staticQuadCount_ = quads.size();
        if (staticQuadCount_ == 0) {
            return;
        }

        std::vector<Vertex> vertices;
        vertices.reserve(staticQuadCount_ * 4);
        for (std::size_t i = 0; i < quads.size(); ++i) {
            const StaticQuad& quad = quads[i];
            // Static means it does not move, not that it cannot be rotated --
            // but nothing authored so far needs a rotated tile, and an
            // unrotated quad is the one case where the corners are exact.
            for (const Vertex& vertex :
                 render::quadVertices(quad.center, quad.size, {1.0f, 0.0f}, quad.uv, quad.tint)) {
                vertices.push_back(vertex);
            }
        }

        glBindVertexArray(staticVao_.get());
        glBindBuffer(GL_ARRAY_BUFFER, staticVbo_.get());
        glBufferData(GL_ARRAY_BUFFER, byteSize(vertices.size() * sizeof(Vertex)), vertices.data(),
                     GL_STATIC_DRAW);

        buildQuadIndices(indexScratch_, staticQuadCount_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, staticIbo_.get());
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, byteSize(indexScratch_.size() * sizeof(GLuint)),
                     indexScratch_.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
    }

    void drawSprite(Vec2 center, Vec2 size, Vec2 forward, render::TextureId texture,
                    render::UvRect uv, Color tint) override {
        // The planner hands back the submission slot; the vertices go there, and
        // plan() decides later what order the slots are drawn in.
        const std::uint32_t slot = planner_.submitQuad(layer_, texture);
        const std::size_t base = static_cast<std::size_t>(slot) * 4;
        vertices_.resize(base + 4);

        const std::array<Vertex, 4> quad = render::quadVertices(center, size, forward, uv, tint);
        for (std::size_t i = 0; i < quad.size(); ++i) {
            vertices_[base + i] = quad[i];
        }
    }

    void endFrame() override {
        planner_.plan();
        stats_ = {planner_.quadCount(), planner_.drawCallCount()};

        glUseProgram(program_.get());
        glUniform1i(textureUniform_, 0);
        glActiveTexture(GL_TEXTURE0);

        drawStaticLayer();

        if (planner_.drawCalls().empty()) {
            return;
        }

        // Reorder the frame's vertices into the order the planner chose. The
        // copy is four vertices per quad of contiguous memory; the alternative
        // is sorting the vertices themselves, which would move four times as
        // many bytes to reach the same order.
        ordered_.clear();
        ordered_.reserve(vertices_.size());
        for (const std::uint32_t slot : planner_.order()) {
            const std::size_t base = static_cast<std::size_t>(slot) * 4;
            ordered_.insert(ordered_.end(), vertices_.begin() + static_cast<std::ptrdiff_t>(base),
                            vertices_.begin() + static_cast<std::ptrdiff_t>(base + 4));
        }

        glBindVertexArray(vao_.get());
        glBindBuffer(GL_ARRAY_BUFFER, vbo_.get());
        glBufferData(GL_ARRAY_BUFFER, byteSize(ordered_.size() * sizeof(Vertex)), ordered_.data(),
                     GL_STREAM_DRAW);
        ensureIndexCapacity(ordered_.size() / 4);

        bool cameraUploaded = false;
        render::Camera uploaded{};
        for (const render::DrawCall& call : planner_.drawCalls()) {
            if (!cameraUploaded || !(uploaded == call.camera)) {
                const std::array<float, 16> projection = render::orthographic(call.camera);
                glUniformMatrix4fv(projectionUniform_, 1, GL_FALSE, projection.data());
                uploaded = call.camera;
                cameraUploaded = true;
            }
            glBindTexture(GL_TEXTURE_2D, glNameOf(call.texture));
            drawQuadRange(call.firstQuad, call.quadCount);
        }
        glBindVertexArray(0);
    }

    RenderStats stats() const override { return stats_; }

private:
    static GLsizeiptr byteSize(std::size_t bytes) { return static_cast<GLsizeiptr>(bytes); }

    bool buildProgram(std::string& error) {
        ShaderHandle vertexShader;
        ShaderHandle fragmentShader;
        if (!compileShader(GL_VERTEX_SHADER, kVertexShader, vertexShader, error)) return false;
        if (!compileShader(GL_FRAGMENT_SHADER, kFragmentShader, fragmentShader, error)) {
            return false;  // the vertex shader is freed by its handle, not forgotten
        }

        ProgramHandle program{glCreateProgram()};
        glAttachShader(program.get(), vertexShader.get());
        glAttachShader(program.get(), fragmentShader.get());
        glLinkProgram(program.get());

        GLint linked = 0;
        glGetProgramiv(program.get(), GL_LINK_STATUS, &linked);
        if (linked == 0) {
            char log[1024] = {};
            glGetProgramInfoLog(program.get(), sizeof(log), nullptr, log);
            error = std::string("shader link failed: ") + log;
            return false;  // program and both shaders all released here
        }

        projectionUniform_ = glGetUniformLocation(program.get(), "uProjection");
        textureUniform_ = glGetUniformLocation(program.get(), "uTexture");
        program_ = std::move(program);
        return true;
    }

    // Texture id 0 is the 1x1 opaque white texture, which is what makes an
    // untextured quad and a sprite the same draw path: drawQuad is a tinted
    // white texel.
    bool createWhiteTexture() {
        const std::array<std::uint8_t, 4> white{0xFF, 0xFF, 0xFF, 0xFF};
        TextureHandle texture = genTexture();
        if (!texture) {
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, texture.get());
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, white.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
        textures_.push_back(std::move(texture));
        return true;
    }

    GLuint glNameOf(render::TextureId id) const {
        return id < textures_.size() ? textures_[id].get() : textures_[render::kWhiteTexture].get();
    }

    void ensureIndexCapacity(std::size_t quads) {
        if (quads <= indexedQuads_) {
            return;
        }
        // Grow in powers of two so a frame that creeps upward does not
        // reallocate the index buffer on every one of them.
        std::size_t target = indexedQuads_ == 0 ? 1024 : indexedQuads_;
        while (target < quads) {
            target *= 2;
        }
        buildQuadIndices(indexScratch_, target);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_.get());
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, byteSize(indexScratch_.size() * sizeof(GLuint)),
                     indexScratch_.data(), GL_STATIC_DRAW);
        indexedQuads_ = target;
    }

    void drawQuadRange(std::uint32_t firstQuad, std::uint32_t quadCount) {
        const auto offset = static_cast<std::size_t>(firstQuad) * 6 * sizeof(GLuint);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(quadCount * 6), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(offset));
    }

    // Drawn before the batch's first span, so its place in the layer order is
    // explicit rather than an accident of texture ids: the board is the bottom.
    void drawStaticLayer() {
        if (staticQuadCount_ == 0) {
            return;
        }
        // The camera of the frame's first span, which is the world camera by
        // construction -- the board is submitted before any HUD.
        const render::Camera camera =
            planner_.drawCalls().empty() ? camera_ : planner_.drawCalls().front().camera;
        const std::array<float, 16> projection = render::orthographic(camera);
        glUniformMatrix4fv(projectionUniform_, 1, GL_FALSE, projection.data());

        glBindVertexArray(staticVao_.get());
        glBindTexture(GL_TEXTURE_2D, glNameOf(staticTexture_));
        drawQuadRange(0, static_cast<std::uint32_t>(staticQuadCount_));
        glBindVertexArray(0);

        stats_.quads += staticQuadCount_;
        stats_.drawCalls += 1;
    }

    ProgramHandle program_;
    VertexArrayHandle vao_;
    BufferHandle vbo_;
    BufferHandle ibo_;
    VertexArrayHandle staticVao_;
    BufferHandle staticVbo_;
    BufferHandle staticIbo_;
    std::vector<TextureHandle> textures_;

    GLint projectionUniform_ = -1;
    GLint textureUniform_ = -1;

    render::BatchPlanner planner_;
    render::Camera camera_{};
    std::uint32_t layer_ = 0;
    std::vector<Vertex> vertices_;  // submission order
    std::vector<Vertex> ordered_;   // draw order
    std::vector<GLuint> indexScratch_;
    std::size_t indexedQuads_ = 0;

    render::TextureId staticTexture_ = render::kWhiteTexture;
    std::size_t staticQuadCount_ = 0;

    RenderStats stats_{};
};

}  // namespace

std::unique_ptr<Renderer> createRenderer(const Platform& platform, std::string& error) {
    auto renderer = std::make_unique<GLRenderer>();
    if (!renderer->init(platform, error)) {
        return nullptr;
    }
    return renderer;
}

}  // namespace ktd
