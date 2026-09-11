#pragma once

// What a tool is handed when it runs (ADR-094).
//
// A tool never reaches for a global. Everything it may touch arrives here, which is what makes the
// registry testable against a bare `app::Engine` with no window, no device and no provider.
//
// Two things in here exist because of §54 ("do not create mock or fake AV Gen APIs"):
//
//   - `PerformanceSnapshot` is a plain value, not `rendering::RenderStats`. This file compiles into
//     `avgen_core`, which cannot see `avgen_gpu` and should not: the tool layer is engine-semantic,
//     not renderer-specific. Whoever owns the renderer installs a `PerformanceSource` that fills
//     one in. Nobody installs one in a headless test, so `available` is false and the tool says
//     "no renderer in this session" -- which is the truth, rather than a plausible zero.
//
//   - `ChangeLog` records what actually changed, read back from the engine *after* the write.
//     A tool that reports the value it was asked for rather than the value that landed is how a
//     clamp, an automated override or a Replace-mode route becomes invisible.

#include "core/error.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace avgen::app {
class Engine;
} // namespace avgen::app

namespace avgen::ai {

// ---- cancellation (§28) -------------------------------------------------------------------------

// Copyable by design: the orchestrator, the provider request and every queued tool call hold the
// same flag, so one Cancel reaches all of them. Cancellation that only stops the text arriving
// while background mutation continues is not cancellation.
class CancelToken {
public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel() const { flag_->store(true, std::memory_order_relaxed); }
    [[nodiscard]] bool cancelled() const { return flag_->load(std::memory_order_relaxed); }
    void reset() const { flag_->store(false, std::memory_order_relaxed); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

// ---- performance (§23) ---------------------------------------------------------------------------

struct PassTime {
    std::string label;
    double milliseconds = 0.0;
};

struct PerformanceSnapshot {
    bool available = false;
    std::string unavailableReason = "no renderer is attached to this session";

    double fps = 0.0;
    double cpuFrameMs = 0.0;      // the main thread's own frame cost
    double frameIntervalMs = 0.0; // wall clock between presents
    double gpuFrameMs = -1.0;     // -1 when the device has no timestamp queries

    std::uint32_t drawCalls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t visibleInstances = 0;
    std::uint64_t culledInstances = 0;
    std::uint32_t lights = 0;
    std::uint32_t entities = 0;
    std::uint32_t shadowDraws = 0;
    std::uint32_t particleSystems = 0;
    std::uint32_t proceduralObjects = 0;

    std::vector<PassTime> gpuPasses; // largest first; empty without timestamp queries
};

using PerformanceSource = std::function<PerformanceSnapshot()>;

// ---- what a tool changed --------------------------------------------------------------------------

struct ChangeRecord {
    std::string target;   // parameter path, track target, route target, ...
    std::string detail;   // "0.5 -> 2.0", "route audio.bass -> nodes/glow/emissiveBoost"
    bool clamped = false; // the engine did not take the value as asked
};

class ChangeLog {
public:
    void note(std::string target, std::string detail, bool clamped = false) {
        records_.push_back(ChangeRecord{std::move(target), std::move(detail), clamped});
    }
    [[nodiscard]] const std::vector<ChangeRecord>& records() const { return records_; }
    [[nodiscard]] std::size_t size() const { return records_.size(); }
    void clear() { records_.clear(); }

private:
    std::vector<ChangeRecord> records_;
};

// ---- the context ------------------------------------------------------------------------------------

class ToolContext {
public:
    explicit ToolContext(app::Engine& engine) : engine_(&engine) {}

    [[nodiscard]] app::Engine& engine() const { return *engine_; }

    [[nodiscard]] const CancelToken& cancel() const { return cancel_; }
    void setCancelToken(CancelToken token) { cancel_ = std::move(token); }
    [[nodiscard]] bool cancelled() const { return cancel_.cancelled(); }

    // Where projects live. Supplied by the host rather than derived here, for two reasons: this
    // file is in `avgen_core` and the real answer comes from SDL's preferences directory, which is
    // in the platform layer; and a test needs to point it at a temporary directory.
    //
    // It exists at all because a tool that took a path would let an agent write anywhere on the
    // machine. The project tools take a *name* and resolve it under this root, so the blast radius
    // of a bad argument is one directory the user already owns.
    void setProjectsRoot(std::filesystem::path root) { projectsRoot_ = std::move(root); }
    [[nodiscard]] const std::filesystem::path& projectsRoot() const { return projectsRoot_; }
    [[nodiscard]] bool hasProjectsRoot() const { return !projectsRoot_.empty(); }

    void setPerformanceSource(PerformanceSource source) { performance_ = std::move(source); }
    [[nodiscard]] PerformanceSnapshot performance() const {
        return performance_ ? performance_() : PerformanceSnapshot{};
    }
    [[nodiscard]] bool hasPerformanceSource() const { return static_cast<bool>(performance_); }

    [[nodiscard]] ChangeLog& changes() { return changes_; }
    [[nodiscard]] const ChangeLog& changes() const { return changes_; }

    // Progress for a long tool (addendum §17). Optional: a tool that cannot measure itself simply
    // never calls it, exactly as `app::JobContext` does, rather than inventing a fraction.
    std::function<void(float fraction, const std::string& note)> onProgress;
    void progress(float fraction, std::string note) const {
        if (onProgress) {
            onProgress(fraction, note);
        }
    }

private:
    app::Engine* engine_ = nullptr;
    CancelToken cancel_;
    std::filesystem::path projectsRoot_;
    PerformanceSource performance_;
    ChangeLog changes_;
};

} // namespace avgen::ai
