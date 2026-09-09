#pragma once

// The per-frame spline tables (ADR-026): every Scene::splines entry packed with
// spatial::packSplineTable (kSplineGpuSamples entries each) into one read-only storage buffer
// shared by the procedural renderer (Path deformer, shaders/procedural.wgsl) and the particle
// renderer (Spline emitter, shaders/particles.wgsl). Owned by SceneRenderer; re-packed only
// when the set's combined structural hash changes (splines are structural data, not animated
// per frame), so a static set costs nothing after its first upload.
//
// Slot assignment: slot i is Scene::splines.splines[i] for i < kMaxGpuSplines; slotOf(name)
// resolves a name to its slot (-1 when unknown or beyond the limit; the first of duplicate
// names wins). Buffer layout (shaders/spline.wgsl `SplineTable`): a header of kMaxGpuSplines
// vec4 (x = length, y = closed flag, z = sample count, w = 1 when the slot holds a spline)
// followed by kMaxGpuSplines x kSplineGpuSamples SplineSampleGpu entries.

#include "spatial/spline.hpp"

#include <glm/glm.hpp>
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

constexpr int kMaxGpuSplines = 16;

struct SplineInfoGpu {
    glm::vec4 info[kMaxGpuSplines]; // x = length, y = closed (1/0), z = sample count, w = valid (1/0)
};
static_assert(sizeof(SplineInfoGpu) == 16 * kMaxGpuSplines);

class SplineBuffers {
public:
    explicit SplineBuffers(gpu::Context& context); // creates the (zeroed) storage buffer
    SplineBuffers(const SplineBuffers&) = delete;
    SplineBuffers& operator=(const SplineBuffers&) = delete;

    // Packs and uploads `splines` when their combined hash changed. Call once per frame before
    // the renderers that sample splines run. Returns true when an upload happened.
    bool update(const spatial::SplineSet& splines);
    // Slot of an uploaded spline by name; -1 when unknown or beyond the limit.
    [[nodiscard]] int slotOf(std::string_view name) const;
    [[nodiscard]] std::uint32_t count() const { return count_; }
    [[nodiscard]] const wgpu::Buffer& buffer() const { return buffer_; }
    [[nodiscard]] std::uint32_t uploads() const { return uploads_; } // total uploads so far (tests)
    [[nodiscard]] const SplineInfoGpu& header() const { return header_; }
    // The last packed table of `slot` (empty when unused). Tests and tools.
    [[nodiscard]] const std::vector<spatial::SplineSampleGpu>& table(int slot) const;

    static constexpr std::uint64_t kHeaderBytes = sizeof(SplineInfoGpu);
    static constexpr std::uint64_t kTableBytes =
        static_cast<std::uint64_t>(spatial::kSplineGpuSamples) * sizeof(spatial::SplineSampleGpu);
    static constexpr std::uint64_t kBufferSize = kHeaderBytes + kTableBytes * kMaxGpuSplines;

private:
    gpu::Context& context_;
    wgpu::Buffer buffer_;
    SplineInfoGpu header_{};
    std::vector<spatial::SplineSampleGpu> tables_[kMaxGpuSplines];
    std::unordered_map<std::string, int> slots_;
    std::uint64_t hash_ = 0;
    std::uint32_t count_ = 0;
    std::uint32_t uploads_ = 0;
    bool warnedLimit_ = false;
};

} // namespace avgen::rendering
