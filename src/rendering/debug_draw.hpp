#pragma once

// Debug drawing (ADR-031): an immediate-mode line/point layer rendered after the lit pass
// (depth-tested or overlaid) for the inspection modes — point clouds, bounds, density/attribute
// colouring, field vectors and falloff, normals, splines, SDF slices, instance ids, LOD/culling.
// The CPU builds vertex lists each frame from the Scene (debug_visualizer.hpp); this class only
// uploads and draws them. Off by default; zero cost when no vertices are queued.

#include "gpu/context.hpp"
#include "rendering/debug_view_options.hpp"
#include "gpu/shader_library.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace avgen::rendering {

struct DebugVertex {
    glm::vec3 position;
    float size;          // points: pixel size (0 for lines)
    glm::vec4 color;     // linear rgb + alpha
};

class DebugDraw {
public:
    DebugDraw(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~DebugDraw();
    DebugDraw(const DebugDraw&) = delete;
    DebugDraw& operator=(const DebugDraw&) = delete;

    // `frameLayout` is the scene renderer's group-0 layout (FrameUniforms).
    [[nodiscard]] Result<void> init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                    wgpu::BindGroupLayout frameLayout);
    void clear();
    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);
    void point(const glm::vec3& p, float size, const glm::vec4& color);
    void box(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    void box(const glm::mat4& transform, const glm::vec3& halfExtent, const glm::vec4& color);
    void axis(const glm::mat4& transform, float length);
    void arrow(const glm::vec3& from, const glm::vec3& dir, float length, const glm::vec4& color);
    void circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, int segments = 32);
    void polyline(std::span<const glm::vec3> points, const glm::vec4& color, bool closed = false);
    // Uploads this frame's vertices; call once before the pass that draws them.
    void upload();
    // Draws inside a render pass that binds the frame uniforms at group 0.
    void render(wgpu::RenderPassEncoder& pass, wgpu::BindGroup frameBindGroup, bool depthTest);
    [[nodiscard]] std::size_t lineVertexCount() const { return lines_.size(); }
    [[nodiscard]] std::size_t pointVertexCount() const { return points_.size(); }
    // What was queued, for tests that need to check *what* was drawn rather than how much. A count
    // cannot tell a control that colours by id from one that draws every object the same, and the
    // plan's rule is that a diagnostic has to be checked against the case it is not for.
    [[nodiscard]] std::span<const DebugVertex> lineVertices() const { return lines_; }
    [[nodiscard]] std::span<const DebugVertex> pointVertices() const { return points_; }
    [[nodiscard]] bool empty() const { return lines_.empty() && points_.empty(); }

private:
    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::vector<DebugVertex> lines_;
    std::vector<DebugVertex> points_;
    wgpu::Buffer buffer_;          // lines then points, one storage buffer
    std::size_t capacity_ = 0;     // vertices the buffer can hold
    std::size_t uploadedLines_ = 0;
    std::size_t uploadedPoints_ = 0;
    wgpu::BindGroupLayout vertexLayout_;
    wgpu::BindGroup lineGroup_;
    wgpu::BindGroup pointGroup_;
    wgpu::RenderPipeline linePipeline_;      // depth-tested
    wgpu::RenderPipeline linePipelineNoDepth_;
    wgpu::RenderPipeline pointPipeline_;
    wgpu::RenderPipeline pointPipelineNoDepth_;
    [[nodiscard]] Result<void> ensureCapacity(std::size_t vertices);
};

} // namespace avgen::rendering
