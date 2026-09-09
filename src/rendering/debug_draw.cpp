#include "rendering/debug_draw.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::rendering {

namespace {
constexpr std::size_t kMinCapacity = 4096;

glm::vec3 perpendicular(const glm::vec3& axis) {
    const glm::vec3 reference = std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 u = glm::cross(reference, axis);
    const float length = glm::length(u);
    return length > 1e-6f ? u / length : glm::vec3(1.0f, 0.0f, 0.0f);
}
} // namespace

DebugDraw::DebugDraw(gpu::Context& context, gpu::ShaderLibrary& shaders) : context_(context), shaders_(shaders) {}
DebugDraw::~DebugDraw() = default;

Result<void> DebugDraw::init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                             wgpu::BindGroupLayout frameLayout) {
    auto module = shaders_.load("debug.wgsl");
    if (!module) {
        return fail("debug.wgsl: {}", module.error().message);
    }

    wgpu::BindGroupLayoutEntry entry{};
    entry.binding = 0;
    entry.visibility = wgpu::ShaderStage::Vertex;
    entry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.label = "debug-vertex-layout";
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &entry;
    vertexLayout_ = context_.device().CreateBindGroupLayout(&layoutDesc);

    const std::array<wgpu::BindGroupLayout, 2> layouts = {frameLayout, vertexLayout_};
    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.label = "debug-pipeline-layout";
    pipelineLayoutDesc.bindGroupLayoutCount = layouts.size();
    pipelineLayoutDesc.bindGroupLayouts = layouts.data();
    wgpu::PipelineLayout pipelineLayout = context_.device().CreatePipelineLayout(&pipelineLayoutDesc);

    // Premultiplied alpha over the HDR target: the shader multiplies rgb by alpha.
    wgpu::BlendState blend{};
    blend.color.srcFactor = wgpu::BlendFactor::One;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.alpha = blend.color;
    wgpu::ColorTargetState target{};
    target.format = colorFormat;
    target.blend = &blend;
    target.writeMask = wgpu::ColorWriteMask::All;

    const auto makePipeline = [&](const char* label, const char* entryPoint, wgpu::PrimitiveTopology topology,
                                  bool depthTest) {
        wgpu::FragmentState fragment{};
        fragment.module = *module;
        fragment.entryPoint = "fs_main";
        fragment.targetCount = 1;
        fragment.targets = &target;
        wgpu::DepthStencilState depth{};
        depth.format = depthFormat;
        depth.depthWriteEnabled = false; // debug geometry never occludes the scene
        depth.depthCompare = depthTest ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::Always;
        wgpu::RenderPipelineDescriptor desc{};
        desc.label = label;
        desc.layout = pipelineLayout;
        desc.vertex.module = *module;
        desc.vertex.entryPoint = entryPoint;
        desc.primitive.topology = topology;
        desc.primitive.cullMode = wgpu::CullMode::None;
        desc.depthStencil = &depth;
        desc.fragment = &fragment;
        return context_.device().CreateRenderPipeline(&desc);
    };
    linePipeline_ = makePipeline("debug-lines", "vs_lines", wgpu::PrimitiveTopology::LineList, true);
    linePipelineNoDepth_ = makePipeline("debug-lines-overlay", "vs_lines", wgpu::PrimitiveTopology::LineList, false);
    pointPipeline_ = makePipeline("debug-points", "vs_points", wgpu::PrimitiveTopology::TriangleList, true);
    pointPipelineNoDepth_ = makePipeline("debug-points-overlay", "vs_points", wgpu::PrimitiveTopology::TriangleList, false);
    if (linePipeline_ == nullptr || pointPipeline_ == nullptr) {
        return fail("debug draw: pipeline creation failed");
    }
    return ensureCapacity(kMinCapacity);
}

Result<void> DebugDraw::ensureCapacity(std::size_t vertices) {
    if (vertices <= capacity_ && buffer_ != nullptr) {
        return {};
    }
    std::size_t capacity = std::max<std::size_t>(kMinCapacity, capacity_ == 0 ? vertices : capacity_);
    while (capacity < vertices) {
        capacity *= 2;
    }
    wgpu::BufferDescriptor desc{};
    desc.label = "debug-vertices";
    desc.size = capacity * sizeof(DebugVertex);
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer buffer = context_.device().CreateBuffer(&desc);
    if (buffer == nullptr) {
        return fail("debug draw: cannot allocate {} vertices", capacity);
    }
    buffer_ = std::move(buffer);
    capacity_ = capacity;
    // One bind group per section; the point section starts at a dynamic offset of zero because the
    // shader indexes by instance, so both groups view the whole buffer and the draw picks the range.
    const auto makeGroup = [&](const char* label) {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = buffer_;
        entry.offset = 0;
        entry.size = desc.size;
        wgpu::BindGroupDescriptor groupDesc{};
        groupDesc.label = label;
        groupDesc.layout = vertexLayout_;
        groupDesc.entryCount = 1;
        groupDesc.entries = &entry;
        return context_.device().CreateBindGroup(&groupDesc);
    };
    lineGroup_ = makeGroup("debug-lines-group");
    pointGroup_ = lineGroup_;
    return {};
}

void DebugDraw::clear() {
    lines_.clear();
    points_.clear();
}

void DebugDraw::line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
    lines_.push_back(DebugVertex{a, 0.0f, color});
    lines_.push_back(DebugVertex{b, 0.0f, color});
}

void DebugDraw::point(const glm::vec3& p, float size, const glm::vec4& color) {
    points_.push_back(DebugVertex{p, size, color});
}

void DebugDraw::box(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color) {
    const glm::vec3 c[8] = {{min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z},
                            {min.x, min.y, max.z}, {max.x, min.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, max.z}};
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                              {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) {
        line(c[e[0]], c[e[1]], color);
    }
}

void DebugDraw::box(const glm::mat4& transform, const glm::vec3& halfExtent, const glm::vec4& color) {
    glm::vec3 c[8];
    int index = 0;
    for (int z = -1; z <= 1; z += 2) {
        for (int y = -1; y <= 1; y += 2) {
            for (int x = -1; x <= 1; x += 2) {
                const glm::vec3 local(static_cast<float>(x) * halfExtent.x, static_cast<float>(y) * halfExtent.y,
                                      static_cast<float>(z) * halfExtent.z);
                c[index++] = glm::vec3(transform * glm::vec4(local, 1.0f));
            }
        }
    }
    // Corner order above is x fastest, then y, then z.
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                              {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) {
        line(c[e[0]], c[e[1]], color);
    }
}

void DebugDraw::axis(const glm::mat4& transform, float length) {
    const glm::vec3 o = glm::vec3(transform[3]);
    line(o, o + glm::vec3(transform[0]) * length, glm::vec4(1.0f, 0.25f, 0.25f, 1.0f));
    line(o, o + glm::vec3(transform[1]) * length, glm::vec4(0.25f, 1.0f, 0.35f, 1.0f));
    line(o, o + glm::vec3(transform[2]) * length, glm::vec4(0.35f, 0.5f, 1.0f, 1.0f));
}

void DebugDraw::arrow(const glm::vec3& from, const glm::vec3& dir, float length, const glm::vec4& color) {
    const float len = glm::length(dir);
    if (len < 1e-6f || length <= 0.0f) {
        return;
    }
    const glm::vec3 axis = dir / len;
    const glm::vec3 tip = from + axis * length;
    line(from, tip, color);
    const glm::vec3 u = perpendicular(axis);
    const glm::vec3 v = glm::cross(axis, u);
    const float head = length * 0.22f;
    line(tip, tip - axis * head + u * head * 0.5f, color);
    line(tip, tip - axis * head - u * head * 0.5f, color);
    line(tip, tip - axis * head + v * head * 0.5f, color);
    line(tip, tip - axis * head - v * head * 0.5f, color);
}

void DebugDraw::circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color,
                       int segments) {
    const float len = glm::length(normal);
    if (len < 1e-6f || radius <= 0.0f || segments < 3) {
        return;
    }
    const glm::vec3 axis = normal / len;
    const glm::vec3 u = perpendicular(axis) * radius;
    const glm::vec3 v = glm::cross(axis, u / radius) * radius;
    glm::vec3 previous = center + u;
    for (int i = 1; i <= segments; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        const glm::vec3 next = center + u * std::cos(angle) + v * std::sin(angle);
        line(previous, next, color);
        previous = next;
    }
}

void DebugDraw::polyline(std::span<const glm::vec3> points, const glm::vec4& color, bool closed) {
    for (std::size_t i = 1; i < points.size(); ++i) {
        line(points[i - 1], points[i], color);
    }
    if (closed && points.size() > 2) {
        line(points.back(), points.front(), color);
    }
}

void DebugDraw::upload() {
    uploadedLines_ = lines_.size();
    uploadedPoints_ = points_.size();
    const std::size_t total = uploadedLines_ + uploadedPoints_;
    if (total == 0) {
        return;
    }
    if (auto ok = ensureCapacity(total); !ok) {
        log::warn("debug draw: {}", ok.error().message);
        uploadedLines_ = 0;
        uploadedPoints_ = 0;
        return;
    }
    if (uploadedLines_ > 0) {
        context_.queue().WriteBuffer(buffer_, 0, lines_.data(), uploadedLines_ * sizeof(DebugVertex));
    }
    if (uploadedPoints_ > 0) {
        context_.queue().WriteBuffer(buffer_, uploadedLines_ * sizeof(DebugVertex), points_.data(),
                                     uploadedPoints_ * sizeof(DebugVertex));
    }
}

void DebugDraw::render(wgpu::RenderPassEncoder& pass, wgpu::BindGroup frameBindGroup, bool depthTest) {
    if (uploadedLines_ == 0 && uploadedPoints_ == 0) {
        return;
    }
    pass.SetBindGroup(0, frameBindGroup);
    pass.SetBindGroup(1, lineGroup_);
    if (uploadedLines_ > 0) {
        pass.SetPipeline(depthTest ? linePipeline_ : linePipelineNoDepth_);
        pass.Draw(static_cast<std::uint32_t>(uploadedLines_));
    }
    if (uploadedPoints_ > 0) {
        pass.SetPipeline(depthTest ? pointPipeline_ : pointPipelineNoDepth_);
        // The point section starts after the lines: the shader reads by instance index, so the
        // first instance is offset by the line count.
        pass.Draw(6, static_cast<std::uint32_t>(uploadedPoints_), 0, static_cast<std::uint32_t>(uploadedLines_));
    }
}

} // namespace avgen::rendering
