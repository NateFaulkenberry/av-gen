#pragma once

// An offline render (milestone 1.0, ADR-020): a dedicated Offline engine (fixed-step clock,
// precomputed analysis) drives a SceneRenderer into an offscreen target; every frame is read
// back through a gpu::ReadbackRing (the copy rides in the frame's command buffer and the render
// thread keeps submitting while the GPU finishes earlier frames), hashed in frame order, and
// handed to encoder threads (PNG or EXR) or the video writer. `step()` renders a bounded number
// of frames so the live application can run a job between UI frames; the headless CLI just loops
// until done. Frame f is rendered at time start + f / fps and depends on nothing but the project
// and f (ADR-012), so the per-frame hashes are the determinism check. EXR output reads the
// scene-linear RGBA16F image before tone mapping; PNG and video read the tone-mapped RGBA8.

#include "app/engine.hpp"
#include "app/render_settings.hpp"
#include "assets/video_writer.hpp"
#include "core/error.hpp"
#include "gpu/readback.hpp"
#include "gpu/readback_ring.hpp"
#include "rendering/composition_renderer.hpp"
#include "rendering/scene_renderer.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace avgen::app {


struct RenderProgress {
    std::uint64_t framesRendered = 0;  // submitted to the GPU
    std::uint64_t framesReadBack = 0;  // read back and hashed (<= framesRendered while in flight)
    std::uint64_t framesTotal = 0;
    std::uint64_t framesWritten = 0;   // encoded and on disk / in the video
    double elapsedSeconds = 0.0;
    double renderFps = 0.0;            // frames rendered per second of wall time
    std::uint64_t lastFrameHash = 0;
    std::uint64_t sequenceHash = 0;    // FNV-1a over all frame hashes so far
    bool finished = false;
    bool cancelled = false;
    std::string error;                 // first error, empty when none
    [[nodiscard]] double fraction() const {
        return framesTotal == 0 ? 0.0 : static_cast<double>(framesRendered) / static_cast<double>(framesTotal);
    }
};

class RenderJob {
public:
    // `engine` must be an Offline engine with the project already loaded. `baseDir` resolves a
    // relative settings.outputPath (normally the project's folder).
    RenderJob(gpu::Context& context, gpu::ShaderLibrary& shaders, std::unique_ptr<Engine> engine,
              RenderSettings settings, std::filesystem::path baseDir);
    ~RenderJob();
    RenderJob(const RenderJob&) = delete;
    RenderJob& operator=(const RenderJob&) = delete;

    // Validates, resolves the range, sizes the renderer, creates the output, starts encoders.
    [[nodiscard]] Result<void> start();
    // Renders up to `maxFrames` frames (or until `budgetSeconds` of wall time is spent, when
    // > 0). Returns true when the job is complete (finish() has run) or failed.
    [[nodiscard]] bool step(int maxFrames = 1, double budgetSeconds = 0.0);
    // Renders everything. Returns the first error, if any.
    [[nodiscard]] Result<void> run();
    void cancel(); // stops after the current frame; partial output is kept (video is finished)

    [[nodiscard]] RenderProgress progress() const;
    [[nodiscard]] const RenderSettings& settings() const { return settings_; }
    [[nodiscard]] const std::filesystem::path& outputPath() const { return output_; }
    [[nodiscard]] Engine& engine() { return *engine_; }
    [[nodiscard]] double resolvedEndSeconds() const { return end_; }
    // Per-frame hashes of the frames read back so far, in frame order (determinism checks).
    [[nodiscard]] const std::vector<std::uint64_t>& frameHashes() const { return frameHashes_; }

private:
    struct Pending {
        std::uint64_t index = 0;
        gpu::Image8 image;  // PNG / video
        gpu::ImageF imageF; // EXR
        // ADR-242. Empty for the beauty frame; otherwise the AOV this image is, and whether its
        // EXR is written as half or as 32-bit float. Depth in metres and an integer identifier are
        // not representable in half -- a half carries integers exactly only to 2048, and a far
        // plane is hundreds of metres -- so those two are written wide and the rest are not.
        std::string aov;
        bool aovHalf = true;
    };
    // One auxiliary target to export: what to call it, where to get it, how it is laid out, and
    // what has to happen to it before it is a file somebody else can use.
    //
    // `decodeNormal` is not an optional nicety. The normal target is **octahedral**: the scene pass
    // writes `vec4(octEncode(n), roughness, flags)`, so its red and green are an encoded pair and
    // not the x and y of anything. Copied out raw it is a pass no compositor can read, and it looks
    // entirely plausible while being useless. The test that asserts a unit vector is what found it.
    struct AovSource {
        std::string name;
        const wgpu::Texture* texture = nullptr;
        gpu::ReadbackRing::Format format = gpu::ReadbackRing::Format::Rgba16Float;
        bool half = true;
        bool decodeNormal = false;
    };
    // The CPU half of `octDecode` in shaders/common.wgsl, applied in place to an RGBA float image:
    // rg is the encoded normal and b is roughness, and the result is xyz world normal with the
    // roughness moved into alpha -- which is the layout the name `normal` promises.
    static void decodeNormalRoughness(gpu::ImageF& image);
    // ADR-251: box-average a supersampled HDR frame down to the output size. Not static: it needs
    // `settings_.width`/`height` to know what it is resolving to.
    void resolveToOutput(gpu::ImageF& image);
    [[nodiscard]] Result<void> renderOne();
    // Hands completed readbacks to the encoders; `all` waits for every frame in flight first.
    [[nodiscard]] Result<void> drain(bool all);
    void handleFrame(gpu::ReadbackRing::Frame frame);
    [[nodiscard]] Result<void> finish();
    void encoderLoop();
    void fail(std::string message);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::unique_ptr<Engine> engine_;
    RenderSettings settings_;
    std::filesystem::path baseDir_;
    std::filesystem::path output_;
    std::unique_ptr<rendering::SceneRenderer> renderer_;
    // The 2D composition (ADR-083), installed as the renderer's overlay exactly as the live path
    // installs it. The offline frame is the live frame plus a fixed clock; the composition must
    // not be one of the differences.
    std::unique_ptr<rendering::CompositionRenderer> compositor_;
    std::unique_ptr<gpu::ReadbackRing> ring_; // after renderer_: destroyed (and flushed) first
    // ADR-242: a SECOND ring, deliberately not the beauty ring. That one hands frames back in
    // enqueue order and the job's sequence hash is built from that order, so interleaving five more
    // copies per frame into it would corrupt the determinism check -- the one thing an offline
    // render exists to be able to prove. Three slots, which is also the back-pressure: an AOV
    // enqueue blocks until a slot frees, which bounds how many float images can pile up at once.
    std::unique_ptr<gpu::ReadbackRing> aovRing_;
    std::vector<AovSource> aovs_;
    // ADR-186 says an offline render lifts the live distance limits, and the job logs that it is
    // doing so. A log line is not evidence that a thing happened -- this repository has an ADR
    // about a supersample fix that logged "2.00x" while doing nothing -- so the entity world's own
    // structural counters are carried out with the render and reported beside the frame count.
    // `coarse` or `skipped` above zero in a render means the lift did not reach the simulation,
    // whatever the log said.
    std::size_t entityCoarseMax_ = 0;
    std::size_t entitySkippedMax_ = 0;
    std::size_t entityFullMax_ = 0;
    // The rig half of the same question. `RigStats` has carried these three since ADR-086 and
    // nothing has ever read them either. A rig counted rateLimited or culled in a render is a
    // distant character posed at 15 Hz (feet sliding) or not posed at all (gliding in a frozen
    // pose) in the deliverable -- the *other* artifact reported, and not the one the entity
    // counters above can see.
    std::uint32_t rigRateLimitedMax_ = 0;
    std::uint32_t rigCulledMax_ = 0;
    std::uint32_t rigPosedMax_ = 0;
    // Where the AOV files go. For a sequence that is the output directory; for a VIDEO render
    // `output_` is a file, and writing `<movie.mov>/frame_000000.normal.exr` inside it is not a
    // path. A video with AOVs beside it is a real request -- the passes are for the compositor,
    // and the movie is for everyone else -- so the directory is resolved once, here.
    std::filesystem::path aovDir_;
    // ADR-256: what `materials.json` said at `start()`. Kept so `finish()` can re-derive it and
    // say so if the scene's object set moved under the render -- a mapping that ships with the
    // frames can only be trusted if the frames it shipped with are the frames it described.
    std::string materialManifest_;
    wgpu::Texture ldr_;                       // tone-mapped RGBA8 target (CopySrc)
    wgpu::TextureView ldrView_;
    std::unique_ptr<assets::VideoWriter> video_;
    std::unique_ptr<FixedStepClock> clock_;
    double end_ = 0.0;
    std::uint64_t total_ = 0;
    std::uint64_t rendered_ = 0;
    std::uint64_t readBack_ = 0;
    double encoderWaitSeconds_ = 0.0; // render thread blocked on a full encoder queue
    std::uint64_t sequenceHash_ = 14695981039346656037ull;
    std::uint64_t lastHash_ = 0;
    std::vector<std::uint64_t> frameHashes_;
    std::chrono::steady_clock::time_point startedAt_;
    bool started_ = false;
    bool done_ = false;
    std::atomic<bool> cancelled_{false};

    // Encoder threads: PNG frames are independent (any thread, any order); video frames must be
    // written in order by one thread, so the queue is consumed by one thread in that case.
    std::vector<std::thread> encoders_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable spaceCv_;
    std::deque<Pending> queue_;
    std::size_t queueLimit_ = 8;
    bool stopEncoders_ = false;
    std::uint64_t written_ = 0;
    std::string error_;
};

} // namespace avgen::app
