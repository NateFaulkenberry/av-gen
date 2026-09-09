#include "rendering/material_programs.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "rendering/field_uniforms.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace avgen::rendering {

MaterialPrograms::MaterialPrograms(gpu::Context& context) : context_(context) {
    wgpu::BufferDescriptor desc{};
    desc.label = "material-program-block";
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    desc.size = kBufferSize;
    buffer_ = context_.device().CreateBuffer(&desc);
    context_.queue().WriteBuffer(buffer_, 0, &block_, sizeof(block_));

    wgpu::BufferDescriptor sdesc{};
    sdesc.label = "material-select";
    sdesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    sdesc.size = kSelectBufferSize;
    select_ = context_.device().CreateBuffer(&sdesc);
    // One region per slot value (-1 = no program, then slots 0..7); written once and never again.
    std::array<std::uint8_t, kSelectBufferSize> staging{};
    for (int slot = -1; slot < kMaxGpuMaterialPrograms; ++slot) {
        MaterialSelect s{};
        s.program = slot;
        std::memcpy(staging.data() + selectOffset(slot), &s, sizeof(s));
    }
    context_.queue().WriteBuffer(select_, 0, staging.data(), staging.size());
}

std::uint64_t MaterialPrograms::selectOffset(int slot) {
    const int clamped = std::clamp(slot, -1, kMaxGpuMaterialPrograms - 1);
    return static_cast<std::uint64_t>(clamped + 1) * kSelectStride;
}

void MaterialPrograms::update(const std::vector<scene::MaterialProgram>& programs, const FieldUniforms* fields) {
    block_ = MaterialProgramBlock{};
    slots_.clear();
    const std::size_t count = std::min<std::size_t>(programs.size(), kMaxGpuMaterialPrograms);
    if (programs.size() > count && !warnedLimit_) {
        log::warn("scene has {} material programs; only the first {} are available on the GPU", programs.size(),
                  kMaxGpuMaterialPrograms);
        warnedLimit_ = true;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const scene::MaterialProgram& program = programs[i];
        block_.programs[i] = scene::packMaterialProgram(program, [fields](const std::string& name) {
            return fields != nullptr ? fields->slotOf(name) : -1;
        });
        slots_.emplace(program.name, static_cast<int>(i));
    }
    block_.count = static_cast<std::uint32_t>(count);
    context_.queue().WriteBuffer(buffer_, 0, &block_, sizeof(block_));
}

int MaterialPrograms::slotOf(std::string_view name) const {
    if (name.empty()) {
        return -1;
    }
    const auto it = slots_.find(std::string(name));
    return it == slots_.end() ? -1 : it->second;
}

} // namespace avgen::rendering
