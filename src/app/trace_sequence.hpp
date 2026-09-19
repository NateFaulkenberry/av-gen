#pragma once

// A path-traced SEQUENCE (ADR-382, `docs/offline-backend-audit.md` step 6).
//
// `pathtrace::TraceJob` renders exactly one frame, and the Render panel's tooltip said so: *"One
// frame, not a sequence -- a path-traced sequence is a queue of these and is not built yet."* This
// is that, and it is deliberately NOT a queue of `TraceJob`s, for one measured reason: `TraceJob`
// loads the project from disk inside its own body, so a hundred frames would parse the project and
// rebuild every mesh and texture a hundred times. The project is loaded once here and the engine is
// walked forward, exactly as `app::RenderJob` walks the rasteriser.
//
// What it is made of, and what it therefore does NOT reimplement:
//   * `app::FrameSequenceDriver` -- the range, the loop, cancellation, the hash chain in frame
//     order, bounded backpressure and the writer threads.
//   * `assets::writeExr` / `assets::VideoWriter` -- which were already renderer-agnostic; the audit
//     found that the output layer was never the missing piece.
//   * `scene::tonemapImage` -- the CPU output transform. This is the reason it exists: a
//     path-traced frame could not become a video frame without a GPU, which is exactly the property
//     that makes the tracer usable while the GPU is busy (ADR-351).
//
// The tracer itself is untouched. `pathtrace::PathTracer`, `buildSnapshot` and the Embree scene are
// called, not modified.

#include "app/frame_range.hpp"
#include "app/render_settings.hpp"
#include "core/error.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace avgen::app {

struct TraceSequenceRequest {
    std::filesystem::path project;   // an ordinary AV Gen project file
    PathTraceSettings trace;         // the range, samples, bounces, denoise, AOVs, the probe
    std::uint32_t width = 640;
    std::uint32_t height = 360;
    // Where the frames go. A directory for an EXR sequence (one file per frame, named by
    // `RenderSettings::frameFile`'s pattern), a file for a video.
    std::filesystem::path output;
    RenderOutput kind = RenderOutput::ExrSequence;
    // Video only. Ignored for a sequence.
    std::string codec = "prores4444";
    std::string backend = "auto";
    int quality = 80;
    bool muxAudio = true;

    [[nodiscard]] Result<void> validate() const;
};

// The translation from what a person has set to what the job takes, as a pure function.
//
// It is a free function and not three lines inside `Application::startPathTraceFromUi` for the
// reason ADR-350 keeps teaching: the decision a UI makes is the part that goes wrong, and a
// decision only reachable by clicking is a decision no test can see. What it decides is which
// output kind the path names -- a `.mov` is a movie, anything else is a folder of EXRs -- and that
// is exactly the rule a person gets wrong first.
//
// `video` supplies the codec, backend, quality and audio choice, which the Render panel already
// owns for the rasteriser and which there is no reason to ask for twice.
[[nodiscard]] TraceSequenceRequest traceSequenceRequestFrom(std::filesystem::path project,
                                                            const PathTraceSettings& trace,
                                                            std::uint32_t width, std::uint32_t height,
                                                            std::filesystem::path output,
                                                            const RenderSettings& video);

// Runs on the calling thread. `step()` exists so the editor can pump it between UI frames, the way
// it pumps a raster render; `run()` is what the CLI uses.
class TraceSequence {
public:
    explicit TraceSequence(TraceSequenceRequest request);
    ~TraceSequence();
    TraceSequence(const TraceSequence&) = delete;
    TraceSequence& operator=(const TraceSequence&) = delete;

    [[nodiscard]] Result<void> start();
    [[nodiscard]] bool step(int maxFrames = 1, double budgetSeconds = 0.0);
    [[nodiscard]] Result<void> run();
    void cancel();

    [[nodiscard]] SequenceProgress progress() const;
    // Samples finished on the frame being traced right now, and how many it wants. A frame of a
    // path trace takes long enough that a frame counter alone is not progress -- the honest report
    // is "frame 12 of 240, 96 of 128 samples" (ADR-351's progress rule, one level up).
    [[nodiscard]] std::uint32_t frameSamplesDone() const;
    [[nodiscard]] std::uint32_t frameSamplesTotal() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace avgen::app
