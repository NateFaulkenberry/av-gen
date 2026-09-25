#pragma once

// Preview stills for a proposed plan (spec §55, ADR-764): one small offscreen frame per proposed
// shot, at the shot's mid-time, rendered from a SCRATCH COPY of the project with the proposal
// installed -- never from the person's project, which is not touched (ADR-753's isolation).
//
// Deliberately synchronous and on request: it loads a scratch session, uploads its textures once
// into a renderer of its own (so the editor's renderer keeps its uploads), and seeks to each
// mid-time. Seconds of work, paid only when the person asks or makes a preview.

#include "core/error.hpp"
#include "directing/compiler.hpp"
#include "gpu/readback.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {
class SceneRenderer;
} // namespace avgen::rendering

namespace avgen::app {

class Engine;

struct ShotStill {
    std::string item;     // the plan's shot key
    std::string shot;     // the sequence shot's name
    double seconds = 0.0; // the instant rendered: the shot's middle
    gpu::Image8 image;
};

struct ShotStillsReport {
    std::vector<ShotStill> stills;
    double loadMs = 0.0;   // the scratch session
    double renderMs = 0.0; // every seek and frame
};

// The plan's shots as `compilation` would install them, in plan order: (item key, shot).
[[nodiscard]] std::vector<std::pair<std::string, const seq::Shot*>> proposedShots(const directing::Compilation& c);

// Writes `live`'s scratch copy into `scratchDir`, loads it, installs `compilation`, and renders each
// proposed shot's middle at `width` x `height`. The copy is deleted before returning.
[[nodiscard]] Result<ShotStillsReport> renderShotStills(gpu::Context& context, gpu::ShaderLibrary& shaders, Engine& live,
                                                       const directing::Compilation& compilation, std::uint32_t width,
                                                       std::uint32_t height, const std::filesystem::path& scratchDir);

// The same stills without blocking the editor, and without paying for the scratch session twice.
//
//   * The scratch session is CACHED under a key the caller derives from the project's state (its
//     path, the composition it has, and the history's state id with any preview edit taken off). A
//     request under the same key reuses the loaded session and its renderer's uploaded textures; any
//     edit, undo, load or save-as changes the key and the next request rebuilds it.
//   * The simulation work -- loading the copy, installing the proposal, seeking to each shot -- runs
//     on a thread of the session's own (as `pathtrace::TraceJob` runs its offline engine). Only
//     writing the copy (it reads the live engine) and rendering each frame (the GPU is the main
//     thread's) happen on the main thread, one shot per `step`, so the editor keeps drawing and the
//     panel can say how far it has got.
class StillsSession {
public:
    StillsSession(gpu::Context& context, gpu::ShaderLibrary& shaders, std::filesystem::path scratchDir);
    ~StillsSession();
    StillsSession(const StillsSession&) = delete;
    StillsSession& operator=(const StillsSession&) = delete;

    // Main thread. Starts rendering `compilation`'s shots. Refused (false) while a request is running.
    bool request(Engine& live, const std::string& key, std::string task, directing::Compilation compilation,
                 std::uint32_t width, std::uint32_t height);

    struct Progress {
        bool busy = false;
        std::string task;
        std::size_t done = 0;
        std::size_t total = 0;
        std::string phase; // "loading a scratch copy", "shot 2 of 3", "done", or the error
        bool failed = false;
    };
    // Main thread, once per frame: renders the shot the worker has ready, if any, and reports.
    [[nodiscard]] Progress step();
    // Stills rendered since the last call, in order (they arrive one per `step`).
    [[nodiscard]] std::vector<ShotStill> takeNew();

    // How often a scratch session was loaded: for the cache's test.
    [[nodiscard]] int loads() const { return loads_.load(); }
    // Milliseconds of main-thread work the last request cost in total, and its whole wall time.
    [[nodiscard]] double mainThreadMs() const { return mainMs_; }
    [[nodiscard]] double wallMs() const { return wallMs_; }

private:
    enum class Stage : std::uint8_t { Idle, Working, Ready, Rendered, Done, Failed };
    void work();
    void join();
    void finishIfDone();

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::filesystem::path scratchDir_;
    std::unique_ptr<rendering::SceneRenderer> renderer_;
    std::unique_ptr<Engine> scratch_; // touched by the worker, or by the main thread while the worker waits
    std::string key_;                 // what `scratch_` was loaded from
    std::filesystem::path copy_;      // the copy the worker loads, when `needLoad_`
    bool needLoad_ = false;
    bool loadedThisRequest_ = false;

    directing::Compilation compilation_;
    std::vector<std::pair<std::string, const seq::Shot*>> shots_;
    std::uint32_t width_ = 0, height_ = 0;
    std::string task_;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    Stage stage_ = Stage::Idle;
    std::size_t current_ = 0;  // the shot the worker has ready (Stage::Ready)
    double seconds_ = 0.0;     // ...at this instant
    std::string phase_;
    std::string error_;
    std::atomic<bool> cancel_{false};
    std::atomic<int> loads_{0};
    std::size_t done_ = 0;
    std::vector<ShotStill> fresh_;
    double mainMs_ = 0.0;
    double wallMs_ = 0.0;
    std::chrono::steady_clock::time_point started_{};
};

} // namespace avgen::app
