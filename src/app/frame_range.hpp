#pragma once

// A frame range, and the loop that walks one (ADR-382, `docs/offline-backend-audit.md` step 5).
//
// ## What was missing, and what was not
//
// The audit's finding was that this project's output layer is ALREADY renderer-agnostic --
// `assets::VideoWriter` and `assets::writeExr` take plain pixel spans and know nothing about who
// produced them. What is welded to the rasteriser is the thing *above* them: the loop that walks a
// time range, asks for frame f, hashes it in order and hands it to a writer. All of that lives
// inside `app::RenderJob`, wrapped around a `SceneRenderer` and two `gpu::ReadbackRing`s, and it is
// why `pathtrace::TraceJob` renders exactly one frame -- the Render panel's tooltip says so in as
// many words.
//
// So this file is not an abstraction over output sinks. It is the frame-range driver, and it is
// deliberately free of the GPU, of ImGui and of any renderer: the arithmetic and the ordering are
// the parts that go quietly wrong, and a rule that needs a device to check is a rule nobody checks.
//
// ## Two pieces, and why they are separate
//
// `FrameRange` is the arithmetic alone -- resolve an end, count frames, name the time of frame f.
// `app::RenderSettings` now delegates to it rather than carrying its own copy, so the two renderers
// cannot disagree about what "frames 0..n at 60 fps from 2.5 s" means. That is the whole point of
// having it: a second renderer with its own idea of the range is a second deliverable.
//
// `FrameSequenceDriver` is the loop: bounded stepping, cancellation, the FNV-1a hash chain in frame
// order, elapsed/ETA progress, and a bounded producer/consumer of finished frames with real
// backpressure. It talks to a renderer only through `FrameSource`.

#include "core/error.hpp"
#include "core/time.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace avgen::app {

// ---- the arithmetic ---------------------------------------------------------------------------

struct FrameRange {
    double startSeconds = 0.0;
    // < 0 means "ask the sources": the audio's duration, else the timeline's, else ten seconds.
    double endSeconds = -1.0;
    double fps = 60.0;

    // The end is EXCLUSIVE. The last frame rendered is at `start + (frameCount - 1) / fps`, which
    // is one frame short of the end -- so a 0.5..1.5 s range at 10 fps is ten frames at 0.5 … 1.4
    // and not eleven. Stated here because it is the arithmetic every off-by-one in a render is.
    [[nodiscard]] double resolvedEnd(double audioSeconds, double timelineSeconds) const;
    [[nodiscard]] std::uint64_t frameCount(double resolvedEndSeconds) const;

    // Frame f's time. `FixedStepClock` is the thing that actually drives a render and it only walks
    // forward from `restartAt`, so this is the answer to "what time IS frame f" for a caller that
    // needs to know out of order -- a progress line, a test, a resumed range. It is the same
    // expression the clock uses (`core/time.cpp`), written once here so the two cannot drift.
    [[nodiscard]] double timeOf(std::uint64_t index) const;

    [[nodiscard]] Result<void> validate() const;
};

// ---- what a renderer has to provide -------------------------------------------------------------

// One finished frame, on its way to a writer. Deliberately bytes and floats rather than any GPU
// type: the rasteriser fills this from a `gpu::ReadbackRing`, the path tracer from a
// `pathtrace::Framebuffer`, and the driver cannot tell which.
struct SequenceFrame {
    std::uint64_t index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Exactly one of these is filled. `rgba8` is display-referred and already encoded; `rgbaF` is
    // scene-linear. Which one a frame carries is a property of the output format, not of the
    // renderer, and the hash is taken over whichever is present so it is always a hash of the bytes
    // that reach the file.
    std::vector<std::uint8_t> rgba8;
    std::vector<float> rgbaF;
    [[nodiscard]] bool linear() const { return rgba8.empty() && !rgbaF.empty(); }
    [[nodiscard]] bool valid() const { return width > 0 && height > 0 && (!rgba8.empty() || !rgbaF.empty()); }
};

// The renderer, as the driver sees it. Four calls, and none of them knows about a range.
class FrameSource {
public:
    virtual ~FrameSource() = default;
    // Called once before the first frame, with the resolved range so a source can size itself.
    [[nodiscard]] virtual Result<void> begin(const FrameRange& range, std::uint64_t frames) = 0;
    // Render the frame at `time`. May complete asynchronously; finished frames are collected by
    // `collect`. `index` is the frame's place in the sequence and is what it must be hashed under.
    [[nodiscard]] virtual Result<void> submit(std::uint64_t index, const FrameTime& time) = 0;
    // Hand over every frame that is ready. With `all`, block until every submitted frame is.
    // Frames MUST arrive in index order -- the hash chain is a sequence hash and reordering it
    // would make two identical renders disagree.
    [[nodiscard]] virtual Result<void> collect(bool all, const std::function<void(SequenceFrame)>& sink) = 0;
    // Called once after the last frame has been collected, before the writers are stopped.
    [[nodiscard]] virtual Result<void> end() = 0;
};

// What happens to a finished frame. Runs on a writer thread, so it must be safe to call from one.
using FrameWriter = std::function<Result<void>(const SequenceFrame&)>;

// ---- progress ------------------------------------------------------------------------------------

struct SequenceProgress {
    std::uint64_t framesSubmitted = 0;
    std::uint64_t framesHashed = 0;   // collected and in the hash chain
    std::uint64_t framesWritten = 0;
    std::uint64_t framesTotal = 0;
    double elapsedSeconds = 0.0;
    double framesPerSecond = 0.0;
    // Negative means "not known yet" rather than zero, which would read as "nearly done". A rate
    // taken over the first frame or two is not a rate: shaders are still compiling and the first
    // BVH is still building (ADR-372's neighbours keep learning this).
    double estimatedRemainingSeconds = -1.0;
    std::uint64_t lastFrameHash = 0;
    std::uint64_t sequenceHash = 0;
    bool finished = false;
    bool cancelled = false;
    std::string error;
    [[nodiscard]] double fraction() const {
        return framesTotal == 0 ? 0.0
                                : static_cast<double>(framesSubmitted) / static_cast<double>(framesTotal);
    }
};

// ---- the loop --------------------------------------------------------------------------------

class FrameSequenceDriver {
public:
    // `writerThreads` of 0 asks for one per hardware thread less one, clamped to [1, 16]. Pass 1
    // where the writer is order-dependent -- a video muxer is, a PNG sequence is not.
    FrameSequenceDriver(FrameSource& source, FrameWriter writer, unsigned writerThreads = 0);
    ~FrameSequenceDriver();
    FrameSequenceDriver(const FrameSequenceDriver&) = delete;
    FrameSequenceDriver& operator=(const FrameSequenceDriver&) = delete;

    // Resolves the range, starts the writers and calls `FrameSource::begin`.
    [[nodiscard]] Result<void> start(const FrameRange& range, double audioSeconds,
                                     double timelineSeconds);

    // Renders up to `maxFrames`, or until `budgetSeconds` of wall time is spent when that is > 0.
    // Returns true when the sequence is complete or has failed -- at which point the writers have
    // been drained and stopped, exactly as `RenderJob::step` behaves, because the editor's frame
    // loop is written against that contract.
    [[nodiscard]] bool step(int maxFrames = 1, double budgetSeconds = 0.0);
    [[nodiscard]] Result<void> run();   // everything, on the calling thread
    void cancel();                      // stops after the current frame; partial output is KEPT

    [[nodiscard]] SequenceProgress progress() const;
    [[nodiscard]] const std::vector<std::uint64_t>& frameHashes() const { return frameHashes_; }
    [[nodiscard]] std::uint64_t frameCount() const { return total_; }
    [[nodiscard]] double resolvedEndSeconds() const { return end_; }

    // The FNV-1a offset basis, exposed so a test can assert an empty sequence's hash is the seed
    // rather than zero -- which is the difference between "nothing was rendered" and "something was
    // rendered and hashed to nothing".
    static constexpr std::uint64_t kHashSeed = 14695981039346656037ull;

private:
    void writerLoop();
    void enqueue(SequenceFrame frame);
    void fail(std::string message);
    [[nodiscard]] Result<void> finish();

    FrameSource& source_;
    FrameWriter writer_;
    unsigned requestedThreads_ = 0;

    FrameRange range_{};
    double end_ = 0.0;
    std::uint64_t total_ = 0;
    std::uint64_t submitted_ = 0;
    std::uint64_t hashed_ = 0;
    std::uint64_t lastHash_ = 0;
    std::uint64_t sequenceHash_ = kHashSeed;
    std::vector<std::uint64_t> frameHashes_;

    FixedStepClock clock_{60.0};
    std::chrono::steady_clock::time_point startedAt_{};
    bool started_ = false;
    bool done_ = false;
    std::atomic<bool> cancelled_{false};

    std::vector<std::thread> writers_;
    mutable std::mutex mutex_;
    std::condition_variable work_;
    std::condition_variable space_;
    std::deque<SequenceFrame> queue_;
    std::size_t queueLimit_ = 8;
    bool stopWriters_ = false;
    std::uint64_t written_ = 0;
    std::string error_;
};

} // namespace avgen::app
