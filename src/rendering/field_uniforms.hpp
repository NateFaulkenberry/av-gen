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
// ADR-1116: the same table also holds the audio history's spectrogram ring, kAudioRingRows rows of
// kAudioBins floats starting at float kAudioRingOffset (right after the grids' region, which keeps
// its full kMaxGridTableFloats). `update` writes only the rows that entered the window since the
// last frame -- a few hundred bytes a frame in playback, the whole 384 KB ring after a seek -- and
// packs the newest onsets of each source into the FieldBlock's `audio` record. A scene whose fields
// have no AudioHistory packs `audio.ring.w = 0`, and every audio field samples 0.
//
// Slot assignment: slot i is Scene::fields.fields[i] for i < spatial::kMaxGpuFields, so compound
// children packed by spatial::packField (which resolves names through FieldSet::indexOf) point
// at the right slot. A disabled field keeps its slot but is packed as a zero-strength constant
// (every sample reads 0) and slotOf() reports -1 for it, so consumers skip it. Fields beyond the
// 16th are not uploaded (slotOf -1, warned once).

#include "spatial/field.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <limits>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>

namespace avgen::gpu {
class Context;
}

namespace avgen::rendering {

// Mirrors `FieldAudio` in shaders/fields.wgsl (ADR-1116, 288 bytes).
struct FieldAudioGpu {
    glm::uvec4 ring{0u};   // x = ring offset in the table (floats), y = rows, z = bins, w = 1 when audio is bound
    glm::vec4 timing{0.0f, -1.0f, 0.0f, 0.0f}; // x = rows per second, y = newest row (-1 none)
    glm::vec4 onsetAge[spatial::kOnsetSources * 2];      // source s: [2s], [2s + 1]; < 0 = none
    glm::vec4 onsetStrength[spatial::kOnsetSources * 2];
};
static_assert(sizeof(FieldAudioGpu) == 288);
static_assert(spatial::kOnsetHistory == 8, "FieldAudio packs eight onsets per source in two vec4s");

// Mirrors `FieldBlock` in shaders/fields.wgsl (6192 bytes).
struct FieldBlock {
    std::uint32_t count = 0;
    std::uint32_t pad[3] = {0, 0, 0};
    FieldAudioGpu audio;
    spatial::FieldGpu fields[spatial::kMaxGpuFields];
};
static_assert(sizeof(FieldBlock) == 16 + sizeof(FieldAudioGpu) + sizeof(spatial::FieldGpu) * spatial::kMaxGpuFields);

// What the audio ring cost this frame (ADR-1116; the live profile's gpuSystems block).
struct FieldAudioStats {
    bool bound = false;
    std::int64_t newestRow = -1;
    std::uint32_t rowsUploaded = 0;    // rows written into the ring this frame
    std::uint64_t ringBytes = 0;       // the ring's share of the table (fixed)
};

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
    [[nodiscard]] const FieldAudioStats& audioStats() const { return audioStats_; }
    static constexpr std::uint64_t kBufferSize = sizeof(FieldBlock);
    // ADR-1116: the audio ring starts where the grids' region ends.
    static constexpr std::uint32_t kAudioRingOffset = static_cast<std::uint32_t>(spatial::kMaxGridTableFloats);
    static constexpr std::uint64_t kGridBufferSize =
        static_cast<std::uint64_t>(spatial::kMaxGridTableFloats + spatial::kAudioRingFloats) * sizeof(float);

private:
    gpu::Context& context_;
    wgpu::Buffer buffer_;
    wgpu::Buffer gridBuffer_;
    FieldBlock block_{};
    std::unordered_map<std::string, int> slots_;
    bool warnedLimit_ = false;
    // ADR-1116: which rows the ring holds -- (ringNewest_ - kAudioRingRows, ringNewest_] of the history
    // `ringAudio_` at `ringRevision_`. A different history or revision refills it.
    void updateAudio(const spatial::FieldSet& fields, double time);
    const spatial::AudioHistory* ringAudio_ = nullptr;
    std::uint64_t ringRevision_ = 0;
    std::int64_t ringNewest_ = std::numeric_limits<std::int64_t>::min();
    std::vector<float> ringStaging_;
    FieldAudioStats audioStats_;
};

} // namespace avgen::rendering
