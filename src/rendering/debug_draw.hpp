#pragma once

// Debug drawing (ADR-031): an immediate-mode line/point layer rendered after the lit pass
// (depth-tested or overlaid) for the inspection modes — point clouds, bounds, density/attribute
// colouring, field vectors and falloff, normals, splines, SDF slices, instance ids, LOD/culling.
// The CPU builds vertex lists each frame from the Scene (debug_visualizer.hpp); this class only
// uploads and draws them. Off by default; zero cost when no vertices are queued.

#include "gpu/context.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

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
    [[nodiscard]] Result<void> init(gpu::Context& context, WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat,
                                    WGPUBindGroupLayout frameLayout);
    void clear();
    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);
    void point(const glm::vec3& p, float size, const glm::vec4& color);
    void box(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    void box(const glm::mat4& transform, const glm::vec3& halfExtent, const glm::vec4& color);
    void axis(const glm::mat4& transform, float length);
    void arrow(const glm::vec3& from, const glm::vec3& dir, float length, const glm::vec4& color);
    void circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, int segments = 32);
    void polyline(std::span<const glm::vec3> points, const glm::vec4& color, bool closed = false);
    // Uploads and draws inside a render pass that has the frame bind group at group 0.
    void upload(gpu::Context& context);
    void render(WGPURenderPassEncoder pass, WGPUBindGroup frameBindGroup, bool depthTest);
    [[nodiscard]] std::size_t lineVertexCount() const { return lines_.size(); }
    [[nodiscard]] std::size_t pointVertexCount() const { return points_.size(); }
    [[nodiscard]] bool empty() const { return lines_.empty() && points_.empty(); }

private:
    std::vector<DebugVertex> lines_;
    std::vector<DebugVertex> points_;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// Which visualisations to build from the scene each frame.
struct DebugViewOptions {
    bool points = false;          // instance origins of procedural objects
    bool bounds = false;          // per-object bounds and SDF bounds
    bool density = false;         // colour points by density
    std::string attribute;        // colour points by this attribute (min..max → blue..red)
    bool fields = false;          // field frames, falloff radii
    bool fieldVectors = false;    // sampled vector arrows on a grid (fieldGrid^3 over the field bounds)
    int fieldGrid = 8;
    bool normals = false;         // instance frames (y axis) as short lines
    bool splines = false;         // spline polylines + frames
    bool sdfSlice = false;        // SDF distance iso-lines on a horizontal slice at sliceHeight
    float sliceHeight = 0.0f;
    bool instanceIds = false;     // colour points by id hash
    bool lod = false;             // colour by LOD level (ADR-029)
    bool culling = false;         // draw culled instances in red
    bool depthTest = true;
    float pointSize = 3.0f;
    int maxPoints = 200000;       // safety cap per frame
};

} // namespace avgen::rendering
