#include "rendering/field_uniforms.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"

#include <algorithm>
#include <array>

namespace avgen::rendering {

FieldUniforms::FieldUniforms(gpu::Context& context) : context_(context) {
    wgpu::BufferDescriptor desc{};
    desc.label = "field-block";
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    desc.size = kBufferSize;
    buffer_ = context_.device().CreateBuffer(&desc);
    context_.queue().WriteBuffer(buffer_, 0, &block_, sizeof(block_));
    // The simulated-grid table: allocated once, zeroed, never resized (see the header).
    wgpu::BufferDescriptor gridDesc{};
    gridDesc.label = "grid-table";
    gridDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
    gridDesc.size = kGridBufferSize;
    gridBuffer_ = context_.device().CreateBuffer(&gridDesc);
}

FieldBlock FieldUniforms::pack(const spatial::FieldSet& fields, double time) {
    FieldBlock block{};
    const std::size_t count = std::min<std::size_t>(fields.fields.size(), spatial::kMaxGpuFields);
    for (std::size_t i = 0; i < count; ++i) {
        const spatial::FieldSpec& field = fields.fields[i];
        spatial::FieldGpu& g = block.fields[i];
        // ADR-906: a triggered field before its first event is silent, and silence is exactly
        // what a disabled field already packs to.
        if (field.enabled && !field.silent()) {
            g = spatial::packField(field, time, &fields);
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
    block.count = static_cast<std::uint32_t>(count);
    // ADR-1116: the audio record -- the ring's place in the table, the newest row at this second, and the
    // newest onsets of each source at this second.
    FieldAudioGpu& a = block.audio;
    for (glm::vec4& v : a.onsetAge) {
        v = glm::vec4(-1.0f);
    }
    if (fields.audio) {
        const spatial::AudioHistory& audio = *fields.audio;
        const double clock = audio.now(time);
        a.ring = glm::uvec4(kAudioRingOffset, static_cast<std::uint32_t>(spatial::kAudioRingRows),
                            static_cast<std::uint32_t>(spatial::kAudioBins), 1u);
        a.timing = glm::vec4(static_cast<float>(audio.rowRate()), static_cast<float>(audio.newestRow(clock)),
                             static_cast<float>(spatial::kAudioMaxDelayRows), 0.0f);
        for (int s = 0; s < spatial::kOnsetSources; ++s) {
            std::array<float, spatial::kOnsetHistory> ages{};
            std::array<float, spatial::kOnsetHistory> strengths{};
            ages.fill(-1.0f);
            audio.lastOnsets(static_cast<spatial::OnsetSource>(s), clock, ages, strengths);
            for (int k = 0; k < spatial::kOnsetHistory; ++k) {
                a.onsetAge[s * 2 + k / 4][k % 4] = ages[static_cast<std::size_t>(k)];
                a.onsetStrength[s * 2 + k / 4][k % 4] = strengths[static_cast<std::size_t>(k)];
            }
        }
    }
    return block;
}

void FieldUniforms::update(const spatial::FieldSet& fields, double time) {
    slots_.clear();
    const std::size_t count = std::min<std::size_t>(fields.fields.size(), spatial::kMaxGpuFields);
    if (fields.fields.size() > count && !warnedLimit_) {
        log::warn("scene has {} fields; only the first {} are available on the GPU", fields.fields.size(),
                  spatial::kMaxGpuFields);
        warnedLimit_ = true;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const spatial::FieldSpec& field = fields.fields[i];
        if (field.enabled && !field.silent()) {
            slots_.emplace(field.name, static_cast<int>(i));
        }
    }
    block_ = pack(fields, time);
    audioStats_ = FieldAudioStats{};
    audioStats_.ringBytes = spatial::kAudioRingFloats * sizeof(float);
    if (fields.audio) {
        audioStats_.bound = true;
        audioStats_.newestRow = static_cast<std::int64_t>(block_.audio.timing.y);
        audioStats_.rowsUploaded = holdAudioRows(*fields.audio, audioStats_.newestRow);
    }
    context_.queue().WriteBuffer(buffer_, 0, &block_, sizeof(block_));
}

std::uint32_t FieldUniforms::holdAudioRows(const spatial::AudioHistory& audio, std::int64_t newest) {
    using spatial::kAudioBins;
    using spatial::kAudioRingRows;
    if (newest < 0) {
        return 0;
    }
    // The rows the window needs, minus the rows the ring already holds. Both are kAudioRingRows long,
    // so the difference is one interval.
    const std::int64_t needLo = newest - kAudioRingRows + 1;
    std::int64_t lo = needLo;
    std::int64_t hi = newest;
    const bool sameSource = ringAudio_ == &audio && ringRevision_ == audio.revision() &&
                            ringNewest_ != std::numeric_limits<std::int64_t>::min();
    if (sameSource) {
        const std::int64_t heldLo = ringNewest_ - kAudioRingRows + 1;
        const std::int64_t heldHi = ringNewest_;
        if (newest == heldHi) {
            return 0;
        }
        if (newest > heldHi && heldHi >= needLo) {
            lo = heldHi + 1; // playing forwards: only the new rows
        } else if (newest < heldHi && newest >= heldLo) {
            hi = heldLo - 1; // stepped back inside the window: only the older rows
        }
    }
    ringAudio_ = &audio;
    ringRevision_ = audio.revision();
    ringNewest_ = newest;
    std::uint32_t written = 0;
    // Written as runs of contiguous slots (at most two: the ring wraps once).
    std::int64_t row = lo;
    while (row <= hi) {
        const auto slot = static_cast<std::uint32_t>(((row % kAudioRingRows) + kAudioRingRows) % kAudioRingRows);
        const std::int64_t run = std::min<std::int64_t>(hi - row + 1, kAudioRingRows - slot);
        ringStaging_.assign(static_cast<std::size_t>(run) * kAudioBins, 0.0f);
        for (std::int64_t k = 0; k < run; ++k) {
            for (int b = 0; b < kAudioBins; ++b) {
                ringStaging_[static_cast<std::size_t>(k) * kAudioBins + static_cast<std::size_t>(b)] =
                    audio.value(row + k, b);
            }
        }
        const std::uint64_t offset =
            (static_cast<std::uint64_t>(kAudioRingOffset) + static_cast<std::uint64_t>(slot) * kAudioBins) * sizeof(float);
        context_.queue().WriteBuffer(gridBuffer_, offset, ringStaging_.data(), ringStaging_.size() * sizeof(float));
        written += static_cast<std::uint32_t>(run);
        row += run;
    }
    return written;
}

int FieldUniforms::slotOf(std::string_view name) const {
    const auto it = slots_.find(std::string(name));
    return it == slots_.end() ? -1 : it->second;
}

} // namespace avgen::rendering
