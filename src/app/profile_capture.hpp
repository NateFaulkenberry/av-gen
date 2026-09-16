#pragma once

// Profiling capture (ADR-031): records per-frame timings and counts to a JSON report so a session
// can be analyzed offline or compared between builds. The host feeds it one sample per frame; the
// capture keeps a bounded ring and writes a summary plus the raw samples on stop.

#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::app {

struct ProfileSample {
    std::uint64_t frame = 0;
    double renderTime = 0.0;      // seconds into the piece
    double cpuFrameMs = 0.0;      // application work
    double frameIntervalMs = 0.0; // wall time between frames
    double gpuFrameMs = -1.0;
    double modulationMicros = 0.0;
    double proceduralCpuMs = 0.0;
    double particleSimulateMs = -1.0;
    std::uint32_t drawCalls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t proceduralObjects = 0;
    std::uint64_t instances = 0;
    std::uint64_t effectorInstances = 0;
    std::uint32_t particleSystems = 0;
    std::uint32_t particleCapacity = 0;
    std::uint64_t instanceBufferBytes = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Percentile and mean summaries of one numeric column.
struct ProfileSummary {
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};
[[nodiscard]] ProfileSummary summarise(std::vector<double> values); // takes a copy; sorts it

class ProfileCapture {
public:
    // Starts recording (clears previous samples). `label` is stored in the report.
    void start(std::string label = {});
    void stop();
    [[nodiscard]] bool recording() const { return recording_; }
    void add(const ProfileSample& sample);

    [[nodiscard]] const std::vector<ProfileSample>& samples() const { return samples_; }
    [[nodiscard]] std::size_t size() const { return samples_.size(); }
    void clear();
    std::size_t maxSamples = 100000; // ~28 minutes at 60 fps; older samples are dropped

    // {"label", "frames", "duration", "summary": {"cpuFrameMs": {...}, …}, "samples": [...]}
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] Result<void> writeFile(const std::filesystem::path& path) const;

private:
    std::vector<ProfileSample> samples_;
    std::string label_;
    bool recording_ = false;
};

} // namespace avgen::app
