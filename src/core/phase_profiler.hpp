#pragma once

// CPU frame-phase profiler: what the main thread spends a frame doing, phase by phase.
//
// This is deliberately NOT gpu::FrameTimeline, and the two must never be confused. FrameTimeline
// measures GPU passes with GPU timestamps. This measures CPU wall-clock on one thread. The wall
// clock around an asynchronous GPU call is the CPU's cost of *issuing* that call plus whatever it
// waited for -- it is not GPU time, and no number here is ever labelled as such. The one phase that
// is mostly a wait (`gpu.acquire`) is named as a wait and reported separately from work.
//
// What it keeps: for each frame, a total per named phase, in a ring of the last kHistory frames.
// Summaries are computed on demand, never per frame, because the useful statistics are order
// statistics (median, P95, P99) and sorting 1024 samples for twenty phases every frame would make
// the instrument the thing it is measuring.
//
// Why order statistics and not a mean: 16,16,16,120,16,16,80,16 has a fine average and is a
// terrible experience. Frame *pacing* is the subject; the mean cannot see it.
//
// Why `min` matters too: this machine is usually running several builds. Contention is never
// negative, so on a contended machine the minimum over a long run is the honest estimate of the
// work itself and the median walks with whatever else is running (docs/performance.md reaches the
// same conclusion for the GPU side).
//
// Thread affinity: a PhaseProfiler instance belongs to one thread. No locks, no atomics.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::core {

class PhaseProfiler {
public:
    static constexpr std::size_t kMaxPhases = 40;
    static constexpr std::size_t kHistory = 2048;

    // Resolves a phase name to a stable index. Call once (a function-local static) and reuse it;
    // the lookup is a linear scan over the registered names and is not meant for the hot path.
    // Returns -1 when the table is full.
    [[nodiscard]] int phase(std::string_view name);

    void beginFrame();
    // Closes the frame and files it. `frameMs` is the caller's own measure of the whole frame, so
    // the unaccounted remainder (frame minus the sum of the phases) can be reported rather than
    // quietly distributed over the phases that happen to be instrumented.
    void endFrame(double frameMs);

    void add(int phaseIndex, double ms) {
        if (phaseIndex >= 0 && static_cast<std::size_t>(phaseIndex) < count_) {
            current_[static_cast<std::size_t>(phaseIndex)] += ms;
        }
    }
    // Per-frame counter rather than a duration (allocations, draw calls, events). Reported the same
    // way; a distribution of counts answers "does this spike allocate?" directly.
    void count(int phaseIndex, double n) { add(phaseIndex, n); }

    class Scope {
    public:
        Scope(PhaseProfiler& profiler, int phaseIndex)
            : profiler_(&profiler), index_(phaseIndex), start_(std::chrono::steady_clock::now()) {}
        ~Scope() {
            profiler_->add(index_, std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - start_)
                                       .count());
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        PhaseProfiler* profiler_;
        int index_;
        std::chrono::steady_clock::time_point start_;
    };

    struct Summary {
        double min = 0.0;
        double median = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
        double max = 0.0;
        double mean = 0.0;
        double total = 0.0;   // sum over the retained frames
        std::size_t samples = 0;
    };

    [[nodiscard]] Summary summary(int phaseIndex) const;
    [[nodiscard]] Summary summary(std::string_view name) const;
    [[nodiscard]] Summary frameSummary() const;
    // Frames whose total exceeded `thresholdMs`. The spike count, which is the number the pacing
    // question actually turns on.
    [[nodiscard]] std::size_t spikes(double thresholdMs) const;
    [[nodiscard]] std::size_t frames() const { return frames_; }
    [[nodiscard]] std::size_t retained() const;
    [[nodiscard]] const std::vector<std::string>& names() const { return names_; }

    // A table: one row per phase, plus the frame total and the unaccounted remainder. `title` is
    // printed above it. Allocates freely; call it at the end of a run or on a keystroke, never in
    // the loop being measured.
    [[nodiscard]] std::string report(std::string_view title) const;
    // One line per frame, phases in registration order: for feeding a histogram or a plot.
    [[nodiscard]] std::string csv() const;

    void reset();

private:
    [[nodiscard]] std::vector<double> gather(std::size_t phaseIndex) const;

    std::vector<std::string> names_;
    std::size_t count_ = 0;
    std::array<double, kMaxPhases> current_{};
    // history_[frame][phase]; frame slot is (frames_ - 1) % kHistory once a frame is filed.
    std::vector<std::array<double, kMaxPhases>> history_;
    std::vector<double> frameMs_;
    std::size_t frames_ = 0;
};

// Per-thread allocation counters. Interposed on the global operator new/delete in
// core/alloc_counters.cpp, so they see every heap allocation in the process -- including the ones
// inside libc++, ImGui, Dawn and nlohmann::json, which is exactly the point: the allocations that
// matter here are the ones nobody wrote on purpose.
//
// The counters are thread_local and non-atomic: an increment costs a load, an add and a store to
// a hot line, and no thread ever reads another's. That is what makes it acceptable to leave them
// compiled in on the audio thread, where the useful reading is "this callback allocated at all".
struct AllocCounters {
    std::uint64_t allocations = 0;
    std::uint64_t frees = 0;
    std::uint64_t bytes = 0; // requested bytes, summed over allocations
};
[[nodiscard]] const AllocCounters& allocCounters() noexcept;

} // namespace avgen::core
