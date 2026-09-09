#pragma once

// The per-frame field block (ADR-025): Scene::fields packed into one uniform buffer shared by
// the procedural renderer (effector pass, Field deformer, emissive field), the particle renderer
// (field forces) and, later, materials. Owned by SceneRenderer; re-packed every frame because
// fields animate (tau = speed * t + phase, waves travel).
//
// FieldUniforms also owns the simulated-grid table (ADR-032): one storage buffer every module
// that includes fields.wgsl binds at group 0 binding 15. It is allocated once at a fixed size
// (spatial::kMaxGridTableFloats floats = 8 MB) so no bind group ever has to be rebuilt;
// rendering::Simulation fills it, spatial::gridTableOffset says where each grid lives, and a
// scene without grids simply never reads it.
//
// Slot assignment: slot i is Scene::fields.fields[i] for i < spatial::kMaxGpuFields, so compound
// children packed by spatial::packField (which resolves names through FieldSet::indexOf) point
// at the right slot. A disabled field keeps its slot but is packed as a zero-strength constant
// (every sample reads 0) and slotOf() reports -1 for it, so consumers skip it. Fields beyond the
// 16th are not uploaded (slotOf -1, warned once).

#include "spatial/field.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace avgen::gpu {
class Context;
}

namespace avgen::rendering {

// Mirrors `FieldBlock` in shaders/fields.wgsl (5904 bytes).
struct FieldBlock {
    std::uint32_t count = 0;
    std::uint32_t pad[3] = {0, 0, 0};
    spatial::FieldGpu fields[spatial::kMaxGpuFields];
};
static_assert(sizeof(FieldBlock) == 16 + sizeof(spatial::FieldGpu) * spatial::kMaxGpuFields);

class FieldUniforms {
public:
    explicit FieldUniforms(gpu::Context& context); // creates the (zeroed) uniform buffer
    FieldUniforms(const FieldUniforms&) = delete;
    FieldUniforms& operator=(const FieldUniforms&) = delete;

    // Packs and uploads `fields` at `time` (seconds). Call once per frame before the renderers
    // that sample fields run.
    void update(const spatial::FieldSet& fields, double time);
    // Slot of an enabled, uploaded field by name; -1 when unknown, disabled or beyond the limit.
    [[nodiscard]] int slotOf(std::string_view name) const;
    [[nodiscard]] std::uint32_t count() const { return block_.count; }
    [[nodiscard]] const wgpu::Buffer& buffer() const { return buffer_; }
    [[nodiscard]] const FieldBlock& block() const { return block_; } // the last packed block (tests)
    // The shared simulated-grid table (group 0 binding 15 of every fields.wgsl consumer).
    [[nodiscard]] const wgpu::Buffer& gridBuffer() const { return gridBuffer_; }
    static constexpr std::uint64_t kBufferSize = sizeof(FieldBlock);
    static constexpr std::uint64_t kGridBufferSize =
        static_cast<std::uint64_t>(spatial::kMaxGridTableFloats) * sizeof(float);

private:
    gpu::Context& context_;
    wgpu::Buffer buffer_;
    wgpu::Buffer gridBuffer_;
    FieldBlock block_{};
    std::unordered_map<std::string, int> slots_;
    bool warnedLimit_ = false;
};

} // namespace avgen::rendering
