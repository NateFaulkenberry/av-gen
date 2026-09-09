#include "rendering/field_uniforms.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"

#include <algorithm>

namespace avgen::rendering {

FieldUniforms::FieldUniforms(gpu::Context& context) : context_(context) {
    wgpu::BufferDescriptor desc{};
    desc.label = "field-block";
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    desc.size = kBufferSize;
    buffer_ = context_.device().CreateBuffer(&desc);
    context_.queue().WriteBuffer(buffer_, 0, &block_, sizeof(block_));
}

void FieldUniforms::update(const spatial::FieldSet& fields, double time) {
    block_ = FieldBlock{};
    slots_.clear();
    const std::size_t count = std::min<std::size_t>(fields.fields.size(), spatial::kMaxGpuFields);
    if (fields.fields.size() > count && !warnedLimit_) {
        log::warn("scene has {} fields; only the first {} are available on the GPU", fields.fields.size(),
                  spatial::kMaxGpuFields);
        warnedLimit_ = true;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const spatial::FieldSpec& field = fields.fields[i];
        spatial::FieldGpu& g = block_.fields[i];
        if (field.enabled) {
            g = spatial::packField(field, time, &fields);
            slots_.emplace(field.name, static_cast<int>(i));
        } else {
            // A disabled field samples as 0 everywhere: a zero-strength scalar constant.
            g = spatial::FieldGpu{};
            g.kind = static_cast<std::uint32_t>(spatial::FieldKind::Constant);
            g.type = static_cast<std::uint32_t>(spatial::FieldType::Scalar);
            g.falloffKind = static_cast<std::uint32_t>(spatial::FalloffKind::None);
            g.worldToLocal = glm::mat4(1.0f);
            g.localToWorldRow0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            g.localToWorldRow1 = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
            g.localToWorldRow2 = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
            g.axisRadius = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
            g.children = glm::ivec4(-1);
        }
    }
    block_.count = static_cast<std::uint32_t>(count);
    context_.queue().WriteBuffer(buffer_, 0, &block_, sizeof(block_));
}

int FieldUniforms::slotOf(std::string_view name) const {
    const auto it = slots_.find(std::string(name));
    return it == slots_.end() ? -1 : it->second;
}

} // namespace avgen::rendering
