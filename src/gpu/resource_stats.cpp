#include "gpu/resource_stats.hpp"

#include <dawn/native/DawnNative.h>

#include "core/log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string_view>

namespace avgen::gpu {

namespace {
std::atomic<std::uint64_t> gRenderPipelines{0};
std::atomic<std::uint64_t> gComputePipelines{0};
std::atomic<std::uint64_t> gShaderModules{0};

// Collects Dawn's per-object dump: one entry per object name ("<prefix>/texture_<id>", ...), its size and strings.
class Collector final : public dawn::native::MemoryDump {
public:
    struct Entry {
        std::uint64_t bytes = 0;
        std::map<std::string, std::string, std::less<>> strings;
    };
    void AddScalar(const char* name, const char* key, const char* units, std::uint64_t value) override {
        if (std::string_view(key) == kNameSize && std::string_view(units) == kUnitsBytes) {
            entries[name].bytes += value;
        }
    }
    void AddString(const char* name, const char* key, const std::string& value) override {
        entries[name].strings[key] = value;
    }
    std::map<std::string, Entry, std::less<>> entries;
};
} // namespace

PipelineCounters pipelineCounters() {
    return {gRenderPipelines.load(std::memory_order_relaxed), gComputePipelines.load(std::memory_order_relaxed),
            gShaderModules.load(std::memory_order_relaxed)};
}

void noteShaderModuleCreated() { gShaderModules.fetch_add(1, std::memory_order_relaxed); }

std::uint64_t pipelineCountersNoteAsync() { return gRenderPipelines.fetch_add(1, std::memory_order_relaxed) + 1; }

namespace {
// AVGEN_PIPELINE_TIMING=1 logs every pipeline's creation time (the Metal compile is inside it).
bool timingAll() {
    static const bool on = [] {
        const char* v = std::getenv("AVGEN_PIPELINE_TIMING");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return on;
}
void noteTime(const char* kind, wgpu::StringView label, std::chrono::steady_clock::time_point start) {
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const std::string_view name = label.data != nullptr
                                      ? std::string_view(label.data, label.length == WGPU_STRLEN ? std::strlen(label.data)
                                                                                                  : label.length)
                                      : std::string_view("(unlabelled)");
    if (timingAll()) {
        log::info("pipeline-timing {} '{}' {:.1f} ms", kind, name, ms);
    }
}
} // namespace

wgpu::RenderPipeline createRenderPipeline(const wgpu::Device& device, const wgpu::RenderPipelineDescriptor* desc) {
    gRenderPipelines.fetch_add(1, std::memory_order_relaxed);
    const auto start = std::chrono::steady_clock::now();
    wgpu::RenderPipeline p = device.CreateRenderPipeline(desc);
    noteTime("render", desc->label, start);
    return p;
}

wgpu::ComputePipeline createComputePipeline(const wgpu::Device& device, const wgpu::ComputePipelineDescriptor* desc) {
    gComputePipelines.fetch_add(1, std::memory_order_relaxed);
    const auto start = std::chrono::steady_clock::now();
    wgpu::ComputePipeline p = device.CreateComputePipeline(desc);
    noteTime("compute", desc->label, start);
    return p;
}

MemoryReport memoryReport(const wgpu::Device& device, std::size_t largestCount) {
    MemoryReport r;
    if (!device) {
        return r;
    }
    const dawn::native::MemoryUsageInfo info = dawn::native::ComputeEstimatedMemoryUsageInfo(device.Get());
    r.available = true;
    r.totalBytes = info.totalUsage;
    r.textureBytes = info.texturesUsage;
    r.depthStencilBytes = info.depthStencilTexturesUsage;
    r.bufferBytes = info.buffersUsage;
    Collector dump;
    dawn::native::DumpMemoryStatistics(device.Get(), &dump);
    for (const auto& [name, entry] : dump.entries) {
        const bool texture = name.find("texture") != std::string::npos;
        const bool buffer = name.find("buffer") != std::string::npos;
        if (texture) {
            ++r.textures;
            std::string detail;
            std::string label;
            bool attachment = false;
            for (const auto& [key, value] : entry.strings) {
                if (key == "label") {
                    label = value;
                } else {
                    detail += (detail.empty() ? "" : " ") + key + "=" + value;
                    if (key == "usage" && value.find("RenderAttachment") != std::string::npos) {
                        attachment = true;
                    }
                }
            }
            if (attachment) {
                r.renderTargetBytes += entry.bytes;
            }
            r.largest.push_back({label.empty() ? name : label, entry.bytes, detail});
        } else if (buffer) {
            ++r.buffers;
        }
    }
    std::stable_sort(r.largest.begin(), r.largest.end(), [](const auto& a, const auto& b) { return a.bytes > b.bytes; });
    if (r.largest.size() > largestCount) {
        r.largest.resize(largestCount);
    }
    return r;
}

} // namespace avgen::gpu
