#include "core/phase_profiler.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace avgen::core {

namespace {

// Nearest-rank percentile on an already sorted sample. With fewer than two samples every
// percentile is the one value there is, which is honest: a distribution needs a population.
double percentile(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto n = static_cast<double>(sorted.size());
    auto rank = static_cast<std::size_t>(std::ceil(q * n));
    if (rank == 0) {
        rank = 1;
    }
    if (rank > sorted.size()) {
        rank = sorted.size();
    }
    return sorted[rank - 1];
}

} // namespace

int PhaseProfiler::phase(std::string_view name) {
    for (std::size_t i = 0; i < count_; ++i) {
        if (names_[i] == name) {
            return static_cast<int>(i);
        }
    }
    if (count_ >= kMaxPhases) {
        return -1;
    }
    names_.emplace_back(name);
    return static_cast<int>(count_++);
}

void PhaseProfiler::beginFrame() { current_.fill(0.0); }

void PhaseProfiler::endFrame(double frameMs) {
    if (history_.size() < kHistory) {
        history_.push_back(current_);
        frameMs_.push_back(frameMs);
    } else {
        const std::size_t slot = frames_ % kHistory;
        history_[slot] = current_;
        frameMs_[slot] = frameMs;
    }
    ++frames_;
}

std::size_t PhaseProfiler::retained() const { return frameMs_.size(); }

std::vector<double> PhaseProfiler::gather(std::size_t phaseIndex) const {
    std::vector<double> out;
    out.reserve(history_.size());
    for (const auto& frame : history_) {
        out.push_back(frame[phaseIndex]);
    }
    return out;
}

PhaseProfiler::Summary PhaseProfiler::summary(int phaseIndex) const {
    Summary s;
    if (phaseIndex < 0 || static_cast<std::size_t>(phaseIndex) >= count_ || history_.empty()) {
        return s;
    }
    std::vector<double> values = gather(static_cast<std::size_t>(phaseIndex));
    std::sort(values.begin(), values.end());
    s.samples = values.size();
    s.min = values.front();
    s.max = values.back();
    s.median = percentile(values, 0.50);
    s.p95 = percentile(values, 0.95);
    s.p99 = percentile(values, 0.99);
    s.total = std::accumulate(values.begin(), values.end(), 0.0);
    s.mean = s.total / static_cast<double>(values.size());
    return s;
}

PhaseProfiler::Summary PhaseProfiler::summary(std::string_view name) const {
    for (std::size_t i = 0; i < count_; ++i) {
        if (names_[i] == name) {
            return summary(static_cast<int>(i));
        }
    }
    return {};
}

PhaseProfiler::Summary PhaseProfiler::frameSummary() const {
    Summary s;
    if (frameMs_.empty()) {
        return s;
    }
    std::vector<double> values = frameMs_;
    std::sort(values.begin(), values.end());
    s.samples = values.size();
    s.min = values.front();
    s.max = values.back();
    s.median = percentile(values, 0.50);
    s.p95 = percentile(values, 0.95);
    s.p99 = percentile(values, 0.99);
    s.total = std::accumulate(values.begin(), values.end(), 0.0);
    s.mean = s.total / static_cast<double>(values.size());
    return s;
}

std::size_t PhaseProfiler::spikes(double thresholdMs) const {
    return static_cast<std::size_t>(
        std::count_if(frameMs_.begin(), frameMs_.end(), [thresholdMs](double v) { return v > thresholdMs; }));
}

std::string PhaseProfiler::report(std::string_view title) const {
    const Summary frame = frameSummary();
    std::string out = fmt::format(
        "\n=== {} ===\n{} frames retained ({} seen)\n"
        "{:<22} {:>8} {:>8} {:>8} {:>8} {:>8} {:>8}\n",
        title, retained(), frames_, "phase (ms)", "min", "median", "mean", "p95", "p99", "max");
    out += std::string(78, '-') + "\n";
    double medianSum = 0.0;
    for (std::size_t i = 0; i < count_; ++i) {
        const Summary s = summary(static_cast<int>(i));
        medianSum += s.median;
        out += fmt::format("{:<22} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f}\n", names_[i], s.min,
                           s.median, s.mean, s.p95, s.p99, s.max);
    }
    out += std::string(78, '-') + "\n";
    out += fmt::format("{:<22} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f}\n", "FRAME", frame.min,
                       frame.median, frame.mean, frame.p95, frame.p99, frame.max);
    out += fmt::format("{:<22} {:>8.3f}   (median frame minus the median of every phase)\n", "unaccounted",
                       frame.median - medianSum);
    out += fmt::format("spikes: >16.7ms {}   >33.3ms {}   >50ms {}\n", spikes(16.7), spikes(33.3), spikes(50.0));
    return out;
}

std::string PhaseProfiler::csv() const {
    std::string out = "frame_ms";
    for (std::size_t i = 0; i < count_; ++i) {
        out += "," + names_[i];
    }
    out += "\n";
    for (std::size_t f = 0; f < history_.size(); ++f) {
        out += fmt::format("{:.4f}", frameMs_[f]);
        for (std::size_t i = 0; i < count_; ++i) {
            out += fmt::format(",{:.4f}", history_[f][i]);
        }
        out += "\n";
    }
    return out;
}

void PhaseProfiler::reset() {
    history_.clear();
    frameMs_.clear();
    frames_ = 0;
    current_.fill(0.0);
}

} // namespace avgen::core
