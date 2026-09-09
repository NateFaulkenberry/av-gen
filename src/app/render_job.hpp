#pragma once

// An offline render (milestone 1.0, ADR-020): a dedicated Offline engine (fixed-step clock,
// precomputed analysis) drives a SceneRenderer into an offscreen target; every frame is read
// back, hashed, and handed to encoder threads (PNG) or the video writer. `step()` renders a
// bounded number of frames so the live application can run a job between UI frames; the
// headless CLI just loops until done. Frame f is rendered at time start + f / fps and depends on
// nothing but the project and f (ADR-012), so the per-frame hashes are the determinism check.

#include "app/engine.hpp"
#include "app/render_settings.hpp"
#include "assets/video_writer.hpp"
#include "core/error.hpp"
#include "gpu/readback.hpp"
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
    std::uint64_t framesRendered = 0;
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

private:
    struct Pending {
        std::uint64_t index = 0;
        gpu::Image8 image;
    };
    [[nodiscard]] Result<void> renderOne();
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
    std::unique_ptr<assets::VideoWriter> video_;
    std::unique_ptr<FixedStepClock> clock_;
    double end_ = 0.0;
    std::uint64_t total_ = 0;
    std::uint64_t rendered_ = 0;
    std::uint64_t sequenceHash_ = 14695981039346656037ull;
    std::uint64_t lastHash_ = 0;
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
