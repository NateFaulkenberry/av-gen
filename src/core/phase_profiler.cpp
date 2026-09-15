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

PhaseProfiler::AllocScope::AllocScope(PhaseProfiler& profiler, int msIndex, int allocIndex)
    : profiler_(&profiler), msIndex_(msIndex), allocIndex_(allocIndex),
      allocs_(allocCounters().allocations), start_(std::chrono::steady_clock::now()) {}

PhaseProfiler::AllocScope::~AllocScope() {
    profiler_->add(msIndex_,
                   std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count());
    profiler_->count(allocIndex_, static_cast<double>(allocCounters().allocations - allocs_));
}

void PhaseProfiler::beginFrame() { current_.fill(0.0); }

void PhaseProfiler::endFrame(double frameMs) {
    if (history_.size() < kHistory) {
        history_.push_back(current_);
        frameMs_.push_back(frameMs);
        group_.push_back(currentGroup_);
    } else {
        const std::size_t slot = frames_ % kHistory;
        history_[slot] = current_;
        frameMs_[slot] = frameMs;
        group_[slot] = currentGroup_;
    }
    ++frames_;
}

void PhaseProfiler::nameGroup(int group, std::string_view name) {
    if (group >= 0 && static_cast<std::size_t>(group) < kMaxGroups) {
        groupNames_[static_cast<std::size_t>(group)] = std::string(name);
    }
}

std::string_view PhaseProfiler::groupName(int group) const {
    if (group >= 0 && static_cast<std::size_t>(group) < kMaxGroups &&
        !groupNames_[static_cast<std::size_t>(group)].empty()) {
        return groupNames_[static_cast<std::size_t>(group)];
    }
    return group == kAllGroups ? std::string_view("all") : std::string_view("(unnamed)");
}

std::size_t PhaseProfiler::retained() const { return frameMs_.size(); }

std::size_t PhaseProfiler::retained(int group) const {
    if (group == kAllGroups) {
        return frameMs_.size();
    }
    return static_cast<std::size_t>(std::count(group_.begin(), group_.end(), group));
}

std::vector<double> PhaseProfiler::gather(std::size_t phaseIndex, int group) const {
    std::vector<double> out;
    out.reserve(history_.size());
    for (std::size_t f = 0; f < history_.size(); ++f) {
        if (!inGroup(group_[f], group)) {
            continue;
        }
        out.push_back(history_[f][phaseIndex]);
    }
    return out;
}

PhaseProfiler::Summary PhaseProfiler::summary(int phaseIndex, int group) const {
    Summary s;
    if (phaseIndex < 0 || static_cast<std::size_t>(phaseIndex) >= count_ || history_.empty()) {
        return s;
    }
    std::vector<double> values = gather(static_cast<std::size_t>(phaseIndex), group);
    if (values.empty()) {
        return s;
    }
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

PhaseProfiler::Summary PhaseProfiler::frameSummary(int group) const {
    Summary s;
    if (frameMs_.empty()) {
        return s;
    }
    std::vector<double> values;
    values.reserve(frameMs_.size());
    for (std::size_t f = 0; f < frameMs_.size(); ++f) {
        if (inGroup(group_[f], group)) {
            values.push_back(frameMs_[f]);
        }
    }
    if (values.empty()) {
        return s;
    }
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

std::size_t PhaseProfiler::spikes(double thresholdMs, int group) const {
    std::size_t n = 0;
    for (std::size_t f = 0; f < frameMs_.size(); ++f) {
        if (inGroup(group_[f], group) && frameMs_[f] > thresholdMs) {
            ++n;
        }
    }
    return n;
}

std::string PhaseProfiler::report(std::string_view title, int group) const {
    const Summary frame = frameSummary(group);
    std::string out = fmt::format(
        "\n=== {} ===\n{} frames retained ({} seen)\n"
        "{:<22} {:>8} {:>8} {:>8} {:>8} {:>8} {:>8}\n",
        title, retained(group), frames_, "phase (ms)", "min", "median", "mean", "p95", "p99", "max");
    out += std::string(78, '-') + "\n";
    double medianSum = 0.0;
    for (std::size_t i = 0; i < count_; ++i) {
        const Summary s = summary(static_cast<int>(i), group);
        medianSum += s.median;
        out += fmt::format("{:<22} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f}\n", names_[i], s.min,
                           s.median, s.mean, s.p95, s.p99, s.max);
    }
    out += std::string(78, '-') + "\n";
    out += fmt::format("{:<22} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f} {:>8.3f}\n", "FRAME", frame.min,
                       frame.median, frame.mean, frame.p95, frame.p99, frame.max);
    out += fmt::format("{:<22} {:>8.3f}   (median frame minus the median of every phase)\n", "unaccounted",
                       frame.median - medianSum);
    out += fmt::format("spikes: >16.7ms {}   >33.3ms {}   >50ms {}\n", spikes(16.7, group), spikes(33.3, group),
                       spikes(50.0, group));
#ifndef AVGEN_ALLOC_COUNTERS
    // Said plainly, because a row of zeros that means "not measured" and a row of zeros that means
    // "allocated nothing" look identical, and one of them is a lie.
    out += "allocation rows read zero: build with -DAVGEN_ALLOC_COUNTERS=ON to attribute them\n";
#endif
    return out;
}

std::string PhaseProfiler::compare(std::string_view title, std::span<const int> groups) const {
    // Medians side by side, and the count each rests on. A column with too few frames behind it is
    // shown with its count rather than quietly averaged into confidence it has not earned.
    std::string out = fmt::format("\n=== {} ===\n{:<22}", title, "phase (ms), median");
    for (const int g : groups) {
        out += fmt::format(" {:>13}", groupName(g));
    }
    out += "\n" + std::string(22 + 14 * groups.size(), '-') + "\n";
    for (std::size_t i = 0; i < count_; ++i) {
        out += fmt::format("{:<22}", names_[i]);
        for (const int g : groups) {
            out += fmt::format(" {:>13.3f}", summary(static_cast<int>(i), g).median);
        }
        out += "\n";
    }
    out += std::string(22 + 14 * groups.size(), '-') + "\n";
    for (const char* row : {"FRAME median", "FRAME p95", "FRAME max", "frames"}) {
        out += fmt::format("{:<22}", row);
        for (const int g : groups) {
            const Summary s = frameSummary(g);
            const std::string_view key(row);
            if (key == "FRAME median") {
                out += fmt::format(" {:>13.3f}", s.median);
            } else if (key == "FRAME p95") {
                out += fmt::format(" {:>13.3f}", s.p95);
            } else if (key == "FRAME max") {
                out += fmt::format(" {:>13.3f}", s.max);
            } else {
                out += fmt::format(" {:>13}", retained(g));
            }
        }
        out += "\n";
    }
    return out;
}

std::string PhaseProfiler::csv() const {
    std::string out = "group,frame_ms";
    for (std::size_t i = 0; i < count_; ++i) {
        out += "," + names_[i];
    }
    out += "\n";
    for (std::size_t f = 0; f < history_.size(); ++f) {
        out += fmt::format("{},{:.4f}", group_[f], frameMs_[f]);
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
    group_.clear();
    frames_ = 0;
    current_.fill(0.0);
}

} // namespace avgen::core
