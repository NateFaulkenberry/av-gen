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
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace avgen::directing {
struct Compilation;
struct Plan;
} // namespace avgen::directing

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

// ADR-765: recording a live performance, requested by the assistant and done by the HOST. The
// recorder is application code (it loads scratch copies of the project on a thread of its own), so
// the core library sees only this seam: the host installs a hook that starts a recording and hands
// back a handle to watch. The recording's result is proposed like any plan -- the person approves.
class RecordingHandle {
public:
    virtual ~RecordingHandle() = default;
    [[nodiscard]] virtual bool done() const = 0;
    [[nodiscard]] virtual std::string phase() const = 0;
    // The recorded plan, once, after `done()`; or why there is none.
    [[nodiscard]] virtual Result<directing::Plan> take() = 0;
    virtual void cancel() = 0;
};
// ADR-767: the same seam for work that ANSWERS rather than proposes -- watching the film for what
// happens in it. The orchestrator waits for it off the main thread and returns its JSON as the
// tool's result.
class DeferredResult {
public:
    virtual ~DeferredResult() = default;
    [[nodiscard]] virtual bool done() const = 0;
    [[nodiscard]] virtual std::string phase() const = 0;
    [[nodiscard]] virtual Result<nlohmann::json> take() = 0;
    virtual void cancel() = 0;
    // Main thread, once, after `take` succeeded: what the answer means for the project. A watch's
    // means nothing (it is only an answer); an evaluation's is a record the project keeps (ADR-931).
    virtual void settle(app::Engine& engine, const nlohmann::json& value) {
        (void)engine;
        (void)value;
    }
};
// Main thread: starts watching a play of the engine's project from zero to `untilSeconds`.
using WatchHook = std::function<Result<std::shared_ptr<DeferredResult>>(app::Engine&, double untilSeconds)>;

// ADR-931: evaluating a plan -- a revision in the project, or a candidate nobody has approved -- by
// rendering a scratch copy of a span of the film and asking the evaluator (the Creative Critic). The
// host's, like the watcher: the core library only asks. The candidate is compiled into the SCRATCH
// copy; the person's project is not touched and nothing is proposed (the autonomy policy: iterate in
// scratch, propose only the winner, so the approval gate stops once rather than every iteration).
struct EvaluationRequest {
    nlohmann::json plan;    // the plan document evaluated, as the tool compiled it
    std::string planId;
    int revision = 0;       // the revision it is, or would become
    std::string candidate;  // the document's fingerprint: two candidates of one revision differ
    bool installed = false; // the plan is the project's own revision: render the project as it is
    double from = 0.0;      // the film-time span rendered and evaluated
    double until = 0.0;
    std::string mode = "preview"; // the Critic's depth: fast | preview | deep
    std::string label;      // what the caller calls this iteration
};
// Main thread: starts one evaluation. Its answer is the `EvaluationReport` as JSON; `settle` stores it
// in `Engine::directingEvaluations()`.
using EvaluationHook =
    std::function<Result<std::shared_ptr<DeferredResult>>(app::Engine&, const EvaluationRequest& request)>;

// ADR-1106: profiling the project for live performance -- `avgen --live-profile` on a scratch copy, in a child
// process, off the main thread (the way an evaluation runs the Critic). Its answer is the avgen.liveprofile/1 record.
struct ProfileRequest {
    std::string mode = "headless"; // headless | live
    double targetFps = 60.0;
    std::uint32_t width = 1920, height = 1080;
    double start = 0.0;
    std::string quality = "auto";
    bool deep = false;
    int verifyCandidates = 0;
};
using ProfileHook = std::function<Result<std::shared_ptr<DeferredResult>>(app::Engine&, const ProfileRequest& request)>;

// Main thread: starts recording `compilation`'s live performances against the engine's project.
using RecordingHook =
    std::function<Result<std::shared_ptr<RecordingHandle>>(app::Engine&, const directing::Compilation&)>;

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

    // Directories the assistant may *read* content from: the example and recipe folders, the open
    // project's own folder, and wherever the host decides a person keeps files they might import.
    //
    // Reading is not writing, and the two get different answers on purpose. `projectsRoot()` is one
    // directory the tools write into; these are several the tools read from, and a path that is not
    // under one of them is refused. Without this an `import` tool would be an arbitrary-file-read
    // primitive handed to a language model.
    void setContentRoots(std::vector<std::filesystem::path> roots) { contentRoots_ = std::move(roots); }
    [[nodiscard]] const std::vector<std::filesystem::path>& contentRoots() const { return contentRoots_; }

    // Resolves `path` against the content roots, or nothing when it escapes all of them. Symlinks
    // and `..` are resolved *before* the check (`weakly_canonical`), because a prefix test on an
    // unresolved path is not a containment test -- "<root>/../../etc/passwd" starts with the root.
    [[nodiscard]] std::optional<std::filesystem::path> resolveContent(const std::filesystem::path& path) const {
        std::error_code ec;
        const std::filesystem::path full = std::filesystem::weakly_canonical(path, ec);
        if (ec) {
            return std::nullopt;
        }
        for (const std::filesystem::path& root : contentRoots_) {
            const std::filesystem::path base = std::filesystem::weakly_canonical(root, ec);
            if (ec) {
                continue;
            }
            const auto rel = full.lexically_relative(base);
            if (!rel.empty() && *rel.begin() != "..") {
                return full;
            }
        }
        return std::nullopt;
    }

    void setPerformanceSource(PerformanceSource source) { performance_ = std::move(source); }

    // ADR-765. Null when the host records nothing (a headless session with no recorder installed).
    void setRecordingHook(RecordingHook hook) { recordingHook_ = std::move(hook); }
    [[nodiscard]] const RecordingHook& recordingHook() const { return recordingHook_; }
    // A tool that started a recording hands it back here; the orchestrator waits for it OFF the main
    // thread and proposes its result, so the editor keeps drawing for the seconds it takes.
    void deferProposal(std::shared_ptr<RecordingHandle> handle) { deferred_ = std::move(handle); }
    [[nodiscard]] const std::shared_ptr<RecordingHandle>& deferredProposal() const { return deferred_; }

    // ADR-767: watching the film. The hook is the host's; a tool that started a watch hands the
    // handle back here and the orchestrator returns its answer.
    void setWatchHook(WatchHook hook) { watchHook_ = std::move(hook); }
    [[nodiscard]] const WatchHook& watchHook() const { return watchHook_; }
    // ADR-931: evaluating. Null when the host installed no evaluator; `director.evaluate` then says so
    // rather than passing anything silently.
    void setEvaluationHook(EvaluationHook hook) { evaluationHook_ = std::move(hook); }
    [[nodiscard]] const EvaluationHook& evaluationHook() const { return evaluationHook_; }
    void setProfileHook(ProfileHook hook) { profileHook_ = std::move(hook); } // ADR-1106
    [[nodiscard]] const ProfileHook& profileHook() const { return profileHook_; }
    void deferResult(std::shared_ptr<DeferredResult> handle) { deferredResult_ = std::move(handle); }
    [[nodiscard]] const std::shared_ptr<DeferredResult>& deferredResult() const { return deferredResult_; }
    // The last observation this task's watch returned (its `observation` object), attached to a plan
    // that places items on events and carries none of its own.
    void setObservation(nlohmann::json observation) { observation_ = std::move(observation); }
    [[nodiscard]] const nlohmann::json& observation() const { return observation_; }
    [[nodiscard]] PerformanceSnapshot performance() const {
        return performance_ ? performance_() : PerformanceSnapshot{};
    }
    [[nodiscard]] bool hasPerformanceSource() const { return static_cast<bool>(performance_); }

    // A change proposed for approval (spec §19): the Director's `propose_plan` leaves its plan here
    // rather than changing anything, and the orchestrator carries it to the task, which then waits
    // in `AwaitingApproval`. One per call; a later proposal in the same task replaces an earlier one.
    struct Proposal {
        nlohmann::json plan;  // the plan document, as proposed
        std::string planId;
        std::string diff;     // the dry run's diff, exactly as the person will read it
        nlohmann::json issues = nlohmann::json::array();
    };
    void propose(Proposal proposal) { proposal_ = std::move(proposal); }
    [[nodiscard]] const std::optional<Proposal>& proposal() const { return proposal_; }

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
    std::vector<std::filesystem::path> contentRoots_;
    PerformanceSource performance_;
    RecordingHook recordingHook_;
    std::shared_ptr<RecordingHandle> deferred_;
    WatchHook watchHook_;
    EvaluationHook evaluationHook_;
    ProfileHook profileHook_;
    std::shared_ptr<DeferredResult> deferredResult_;
    nlohmann::json observation_;
    ChangeLog changes_;
    std::optional<Proposal> proposal_;
};

} // namespace avgen::ai
