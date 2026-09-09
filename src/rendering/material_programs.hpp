#pragma once

// The per-frame material program block (ADR-030): Scene::materialPrograms packed into one uniform
// buffer shared by every shading pass (entities, procedural instances, SDF surfaces). Owned by
// SceneRenderer; re-packed every frame because programs are hot-editable parameters and their
// field references resolve through the frame's FieldUniforms slot map.
//
// Slot assignment: slot i is Scene::materialPrograms[i] for i < kMaxGpuMaterialPrograms, so
// Material::program resolves through slotOf(name). Programs beyond the eighth are not uploaded
// (slotOf -1, warned once) and their materials shade with their own values.
//
// The select buffer is the second half of the story: the shader reads its program index from a
// 16-byte uniform (`MaterialSelect`, group 2 binding 7) rather than a per-object lane, so the
// procedural and SDF renderers need no plumbing. It holds one region per possible slot value
// (-1 .. 7) at 256-byte (dynamic-offset-aligned) stride, written once; a material bind group binds
// the region for its own slot.

#include "scene/material_program.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avgen::gpu {
class Context;
}

namespace avgen::rendering {

class FieldUniforms;

constexpr int kMaxGpuMaterialPrograms = 8;

// Mirrors `MaterialProgramBlock` in shaders/material.wgsl (14736 bytes).
struct MaterialProgramBlock {
    std::uint32_t count = 0;
    std::uint32_t pad[3] = {0, 0, 0};
    scene::MaterialProgramGpu programs[kMaxGpuMaterialPrograms];
};
static_assert(sizeof(MaterialProgramBlock) == 16 + sizeof(scene::MaterialProgramGpu) * kMaxGpuMaterialPrograms);

// Mirrors `MaterialSelect` in shaders/pbr_shade.wgsl.
struct MaterialSelect {
    std::int32_t program = -1;
    std::int32_t pad[3] = {0, 0, 0};
};
static_assert(sizeof(MaterialSelect) == 16);

class MaterialPrograms {
public:
    explicit MaterialPrograms(gpu::Context& context); // creates the (zeroed) program and select buffers
    MaterialPrograms(const MaterialPrograms&) = delete;
    MaterialPrograms& operator=(const MaterialPrograms&) = delete;

    // Packs and uploads `programs`, resolving each Field op's field name through `fields` (null =
    // every field reference is unresolved). Call once per frame, after FieldUniforms::update.
    void update(const std::vector<scene::MaterialProgram>& programs, const FieldUniforms* fields);
    // Slot of an uploaded program by name; -1 when unknown, empty or beyond the limit.
    [[nodiscard]] int slotOf(std::string_view name) const;
    [[nodiscard]] std::uint32_t count() const { return block_.count; }
    [[nodiscard]] const wgpu::Buffer& buffer() const { return buffer_; }
    // The select buffer and the byte offset of the 16-byte region holding `slot` (-1 .. 7).
    [[nodiscard]] const wgpu::Buffer& selectBuffer() const { return select_; }
    [[nodiscard]] static std::uint64_t selectOffset(int slot);
    [[nodiscard]] const MaterialProgramBlock& block() const { return block_; } // the last packed block (tests)
    static constexpr std::uint64_t kBufferSize = sizeof(MaterialProgramBlock);
    static constexpr std::uint64_t kSelectStride = 256; // minUniformBufferOffsetAlignment
    static constexpr std::uint64_t kSelectSize = sizeof(MaterialSelect);
    static constexpr std::uint64_t kSelectBufferSize = kSelectStride * (kMaxGpuMaterialPrograms + 1);

private:
    gpu::Context& context_;
    wgpu::Buffer buffer_;
    wgpu::Buffer select_;
    MaterialProgramBlock block_{};
    std::unordered_map<std::string, int> slots_;
    bool warnedLimit_ = false;
};

} // namespace avgen::rendering
