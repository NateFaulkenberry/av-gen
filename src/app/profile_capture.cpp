#include "app/profile_capture.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>

namespace avgen::app {

namespace {
double percentile(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) {
        return 0.0;
    }
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = static_cast<std::size_t>(std::ceil(pos));
    const double t = pos - static_cast<double>(lo);
    return sorted[lo] * (1.0 - t) + sorted[hi] * t;
}
nlohmann::json summaryJson(const ProfileSummary& s) {
    return nlohmann::json{{"mean", s.mean}, {"min", s.min}, {"max", s.max},
                          {"p50", s.p50},   {"p95", s.p95}, {"p99", s.p99}};
}
} // namespace

ProfileSummary summarise(std::vector<double> values) {
    ProfileSummary out;
    if (values.empty()) {
        return out;
    }
    std::sort(values.begin(), values.end());
    out.min = values.front();
    out.max = values.back();
    out.mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    out.p50 = percentile(values, 0.50);
    out.p95 = percentile(values, 0.95);
    out.p99 = percentile(values, 0.99);
    return out;
}

void ProfileCapture::start(std::string label) {
    samples_.clear();
    label_ = std::move(label);
    recording_ = true;
}

void ProfileCapture::stop() {
    recording_ = false;
}

void ProfileCapture::clear() {
    samples_.clear();
}

void ProfileCapture::add(const ProfileSample& sample) {
    if (!recording_) {
        return;
    }
    if (samples_.size() >= maxSamples) {
        samples_.erase(samples_.begin());
    }
    samples_.push_back(sample);
}

nlohmann::json ProfileCapture::toJson() const {
    nlohmann::json j;
    j["label"] = label_;
    j["frames"] = samples_.size();
    j["duration"] = samples_.empty() ? 0.0 : samples_.back().renderTime - samples_.front().renderTime;
    const auto column = [&](auto fn) {
        std::vector<double> values;
        values.reserve(samples_.size());
        for (const ProfileSample& s : samples_) {
            const double v = fn(s);
            if (v >= 0.0) {
                values.push_back(v);
            }
        }
        return summaryJson(summarise(std::move(values)));
    };
    nlohmann::json summary;
    summary["cpuFrameMs"] = column([](const ProfileSample& s) { return s.cpuFrameMs; });
    summary["frameIntervalMs"] = column([](const ProfileSample& s) { return s.frameIntervalMs; });
    summary["gpuFrameMs"] = column([](const ProfileSample& s) { return s.gpuFrameMs; });
    summary["proceduralCpuMs"] = column([](const ProfileSample& s) { return s.proceduralCpuMs; });
    summary["particleSimulateMs"] = column([](const ProfileSample& s) { return s.particleSimulateMs; });
    summary["instances"] = column([](const ProfileSample& s) { return static_cast<double>(s.instances); });
    summary["drawCalls"] = column([](const ProfileSample& s) { return static_cast<double>(s.drawCalls); });
    j["summary"] = std::move(summary);
    nlohmann::json arr = nlohmann::json::array();
    for (const ProfileSample& s : samples_) {
        arr.push_back(nlohmann::json{{"frame", s.frame},
                                     {"t", s.renderTime},
                                     {"cpuMs", s.cpuFrameMs},
                                     {"intervalMs", s.frameIntervalMs},
                                     {"gpuMs", s.gpuFrameMs},
                                     {"modulationUs", s.modulationMicros},
                                     {"proceduralCpuMs", s.proceduralCpuMs},
                                     {"particleSimMs", s.particleSimulateMs},
                                     {"draws", s.drawCalls},
                                     {"triangles", s.triangles},
                                     {"objects", s.proceduralObjects},
                                     {"instances", s.instances},
                                     {"effectorInstances", s.effectorInstances},
                                     {"particleSystems", s.particleSystems},
                                     {"particleCapacity", s.particleCapacity},
                                     {"instanceBytes", s.instanceBufferBytes},
                                     {"width", s.width},
                                     {"height", s.height}});
    }
    j["samples"] = std::move(arr);
    return j;
}

Result<void> ProfileCapture::writeFile(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write profile report '{}'", path.string());
    }
    out << toJson().dump(2) << '\n';
    if (!out) {
        return fail("failed while writing profile report '{}'", path.string());
    }
    return {};
}

} // namespace avgen::app
