#pragma once

// Debug drawing (ADR-031): an immediate-mode line/point layer rendered after the lit pass
// (depth-tested or overlaid) for the inspection modes — point clouds, bounds, density/attribute
// colouring, field vectors and falloff, normals, splines, SDF slices, instance ids, LOD/culling.
// The CPU builds vertex lists each frame from the Scene (debug_visualizer.hpp); this class only
// uploads and draws them. Off by default; zero cost when no vertices are queued.

#include "gpu/context.hpp"
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
    bool entityBounds = false;    // world-space bounds of ordinary mesh entities
    bool entityOrigins = false;   // world-space origins and axes of ordinary mesh entities
    std::string selectedEntity;   // restrict entity diagnostics when non-empty
    bool lod = false;             // colour by LOD level (ADR-029)
    bool culling = false;         // draw culled instances in red
    // Renderer forensics, Phase 4.3. Each of these isolates or shows one thing, and each is checked
    // in `[debug]` to draw only for the case it names -- the plan's rule is that a diagnostic which
    // shows the same picture whatever the state is worse than none, because somebody trusts it.
    bool worldAxes = false;       // the world origin and its three axes, so "where is zero" is answerable
    bool entityIds = false;       // colour entity bounds by the pick id the identifier target writes
    bool submittedOnly = false;   // restrict entity diagnostics to what the frame actually submits
    // The camera frustum and basis. Drawn from `scene.camera` at `frustumAspect`, which is a
    // separate number because the scene's camera does not carry one -- the viewport supplies it, and
    // an overlay drawn at the wrong aspect is a box that does not match the screen it is over. On a
    // live camera this is exactly the screen edge and tells you nothing; it is worth drawing when
    // the camera is frozen (Phase 4.2's arm), because then it is the volume the cull used.
    bool frustum = false;
    float frustumAspect = 16.0f / 9.0f;
    bool transformTrail = false;  // the recorded world path of the selected object (transform_history.hpp)
    // Renderer forensics, Phase 4.5. Every skinned entity's joints, in world space: a point per
    // joint and a line to its parent. Drawn from the rig's *model-space* matrices rather than from
    // the GPU palette, because the palette is `model * inverseBind` and its translation is not
    // where the joint is -- reading a bone position out of it is the kind of plausible-looking
    // mistake a skeleton overlay exists to catch, not to make.
    bool skeletons = false;
    // ADR-260. Every particle system's **emitter disc and centreline**, drawn from the flattened
    // scene: a ring of `extent.x` at the emitter's world point, the vertical axis the column
    // actually fires along (`direction` is not transformed by `applyParameters`, so it is world
    // down however the emitter's node is rotated), and a second ring where the column *ends* --
    // `speedMin x lifetimeMin`, integrated with the system's own gravity and drag.
    //
    // The bottom ring is the point of this. Every diagnostic ever pointed at the tractor beam
    // treated it as an axis, which is a line of infinite length, and asked only how far the animal
    // was from it sideways; the beam that is drawn is a column with a bottom, and for the whole
    // first half of every lift the animal was underneath it. A centreline with an end on it is a
    // picture of that; the beam volume is not.
    //
    // Pair it with `entityBounds` + `entityOrigins`: the origin marker is the point the director
    // aims, the box is what a viewer sees, and the gap between them is the other half of ADR-260.
    bool beams = false;
    bool depthTest = true;
    float pointSize = 3.0f;
    int maxPoints = 200000;       // safety cap per frame
};

} // namespace avgen::rendering
