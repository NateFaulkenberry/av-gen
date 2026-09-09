#include "rendering/spline_buffers.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"

#include <algorithm>
#include <cstring>

namespace avgen::rendering {

namespace {

// FNV-1a over the set: order, names and every spline's structural hash.
std::uint64_t setHash(const spatial::SplineSet& splines, std::size_t count) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    const auto mix = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h = (h ^ ((v >> (8 * i)) & 0xFFu)) * 0x100000001b3ULL;
        }
    };
    mix(count);
    for (std::size_t i = 0; i < count; ++i) {
        const spatial::Spline& s = splines.splines[i];
        mix(s.name.size());
        for (const char c : s.name) {
            mix(static_cast<unsigned char>(c));
        }
        mix(s.structuralHash());
    }
    return h;
}

} // namespace

SplineBuffers::SplineBuffers(gpu::Context& context) : context_(context) {
    wgpu::BufferDescriptor desc{};
    desc.label = "spline-tables";
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    desc.size = kBufferSize;
    buffer_ = context_.device().CreateBuffer(&desc);
    context_.queue().WriteBuffer(buffer_, 0, &header_, sizeof(header_));
}

bool SplineBuffers::update(const spatial::SplineSet& splines) {
    const std::size_t count = std::min<std::size_t>(splines.splines.size(), static_cast<std::size_t>(kMaxGpuSplines));
    if (splines.splines.size() > count && !warnedLimit_) {
        log::warn("scene has {} splines; only the first {} are available on the GPU", splines.splines.size(),
                  kMaxGpuSplines);
        warnedLimit_ = true;
    }
    const std::uint64_t hash = setHash(splines, count);
    if (hash == hash_ && uploads_ > 0) {
        return false;
    }
    hash_ = hash;
    header_ = SplineInfoGpu{};
    slots_.clear();
    for (int slot = 0; slot < kMaxGpuSplines; ++slot) {
        tables_[slot].clear();
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kHeaderBytes + kTableBytes * count));
    for (std::size_t i = 0; i < count; ++i) {
        const spatial::Spline& s = splines.splines[i];
        std::vector<spatial::SplineSampleGpu>& table = tables_[i];
        table = spatial::packSplineTable(s, spatial::kSplineGpuSamples);
        table.resize(static_cast<std::size_t>(spatial::kSplineGpuSamples)); // pad (never shrinks in practice)
        header_.info[i] = glm::vec4(s.length(), s.closed ? 1.0f : 0.0f, static_cast<float>(spatial::kSplineGpuSamples), 1.0f);
        slots_.emplace(s.name, static_cast<int>(i));
        std::memcpy(bytes.data() + kHeaderBytes + kTableBytes * i, table.data(), static_cast<std::size_t>(kTableBytes));
    }
    std::memcpy(bytes.data(), &header_, sizeof(header_));
    count_ = static_cast<std::uint32_t>(count);
    context_.queue().WriteBuffer(buffer_, 0, bytes.data(), bytes.size());
    ++uploads_;
    return true;
}

int SplineBuffers::slotOf(std::string_view name) const {
    const auto it = slots_.find(std::string(name));
    return it == slots_.end() ? -1 : it->second;
}

const std::vector<spatial::SplineSampleGpu>& SplineBuffers::table(int slot) const {
    static const std::vector<spatial::SplineSampleGpu> empty;
    return slot >= 0 && slot < kMaxGpuSplines ? tables_[slot] : empty;
}

} // namespace avgen::rendering
