#include "app/live_drop_reason.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace avgen::app {

namespace {

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    if (v.size() % 2 == 1) {
        return *mid;
    }
    const double hi = *mid;
    const double lo = *std::max_element(v.begin(), mid);
    return 0.5 * (lo + hi);
}

} // namespace

PassGrowthTracker::PassGrowthTracker(std::size_t recentFrames, std::size_t capacity)
    : recentFrames_(std::max<std::size_t>(recentFrames, 1)), capacity_(std::max(capacity, 2 * recentFrames_)) {}

void PassGrowthTracker::reset() {
    frames_ = 0;
    series_.clear();
}

void PassGrowthTracker::note(const std::vector<gpu::TimelineInterval>& passes) {
    const std::size_t length = std::min(frames_, capacity_); // values each series holds before this frame
    for (auto& [label, values] : series_) {
        values.push_back(0.0);
    }
    for (const auto& p : passes) {
        auto it = std::find_if(series_.begin(), series_.end(), [&](const auto& s) { return s.first == p.label; });
        if (it == series_.end()) {
            series_.emplace_back(p.label, std::vector<double>(length + 1, 0.0));
            it = series_.end() - 1;
        }
        it->second.back() += p.ms;
    }
    ++frames_;
    for (auto& [label, values] : series_) {
        if (values.size() > capacity_) {
            values.erase(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(values.size() - capacity_));
        }
    }
}

PassGrowthTracker::Growth PassGrowthTracker::largestGrowth() const {
    Growth best;
    const std::size_t n = std::min(frames_, capacity_);
    if (n < 8) {
        return best;
    }
    const std::size_t recent = std::min(recentFrames_, n / 2);
    for (const auto& [label, values] : series_) {
        const auto split = values.end() - static_cast<std::ptrdiff_t>(recent);
        const double before = median(std::vector<double>(values.begin(), split));
        const double now = median(std::vector<double>(split, values.end()));
        if (now - before > (best.valid ? best.grownMs() : 0.0)) {
            best = {true, label, before, now};
        }
    }
    return best;
}

std::string LiveDropReason::text() const {
    std::string s = fmt::format("{}->{}{}:", fromLevel, toLevel, revert ? " (a raise reverted)" : "");
    if (missWindow > 0) {
        s += fmt::format(" {} miss{} in {} frames", misses, misses == 1 ? "" : "es", missWindow);
    } else if (gpuCostMs >= 0.0) {
        s += fmt::format(" GPU {:.2f} ms over budget", gpuCostMs);
    }
    if (growth.valid) {
        s += fmt::format("; {} +{:.2f} ms ({:.2f}->{:.2f})", growth.label, growth.grownMs(), growth.beforeMs,
                         growth.nowMs);
    } else {
        s += "; no pass grew";
    }
    return s;
}

} // namespace avgen::app
