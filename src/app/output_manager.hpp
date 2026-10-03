#pragma once

// Output windows (milestone 1.2): each OutputDesc is one extra window (optionally fullscreen on a
// chosen display) with its own swapchain and an OutputMapping that places the final frame on it.
// The descriptor side (add/remove/JSON) is GPU-free and lives in output_manager.cpp so it can be
// unit-tested; open/closeAll/pumpEvents/presentAll live in output_manager_gpu.cpp.
//
// Per-frame order in the application: Window::pollEvents (primary, pumps the SDL queue) ->
// OutputManager::pumpEvents (consumes what was routed to the output windows) -> render the final
// frame into a TextureBinding texture -> presentAll.

#include "core/error.hpp"
#include "rendering/output_mapping.hpp"

#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wgpu {
class Texture;
} // namespace wgpu

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {
class OutputMapper;
} // namespace avgen::rendering

namespace avgen::app {

struct OutputDesc {
    std::string name;
    int display = -1;             // index into platform::Window::displays(); -1 = default display
    bool fullscreen = false;      // borderless desktop fullscreen on that display
    std::uint32_t width = 1920;   // window size in points when not fullscreen
    std::uint32_t height = 1080;
    bool borderless = true;
    bool alwaysOnTop = false;
    rendering::OutputMapping mapping;
    bool enabled = true;

    [[nodiscard]] Result<void> validate() const; // non-empty name, positive size, valid mapping
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<OutputDesc> fromJson(const nlohmann::json& j); // missing fields keep defaults
};

bool operator==(const OutputDesc& a, const OutputDesc& b);

struct OutputRuntime; // window + surface; defined in output_manager_gpu.cpp

// ADR-1089: how often an output is presented to, decided from how long its swapchain acquire took.
//
// Every output's acquire is a second (third, ...) Fifo `GetCurrentTexture` on the main thread, and
// WebGPU has no "try": when the window's display is not taking frames -- a projector that is slow,
// mis-clocked or asleep, or a window macOS throttles because it is covered -- the acquire blocks the
// whole loop until a drawable comes back. Measured on 2026-10-02: 14.8 ms per frame for a covered
// 960x540 projection window, capping the editor and the performance at 60 fps.
//
// The policy is the smallest that bounds it: an acquire slower than `slowAcquireMs` doubles the
// interval at which this output is presented to (every 2nd, 4th, then 8th frame -- the window shows
// its last frame in between), and `recoverAfter` fast acquires in a row halve it again. A healthy
// output never leaves interval 1 and costs nothing; a stuck one costs the loop one blocked acquire in
// eight frames instead of every frame. Pure and clockless, so the CPU suite checks it.
class PresentPacer {
public:
    static constexpr double kSlowAcquireMs = 4.0;
    static constexpr std::uint32_t kMaxInterval = 8;
    static constexpr std::uint32_t kRecoverAfter = 8;

    // Called once per frame before presenting: whether this frame presents to the output.
    [[nodiscard]] bool due() {
        ++frame_;
        if (interval_ <= 1 || frame_ >= interval_) {
            frame_ = 0;
            return true;
        }
        ++skipped_;
        return false;
    }
    // After a present: how long its acquire blocked.
    void acquired(double ms) {
        lastAcquireMs_ = ms;
        if (ms > kSlowAcquireMs) {
            fastStreak_ = 0;
            interval_ = std::min(kMaxInterval, interval_ * 2);
            ++slowAcquires_;
        } else if (interval_ > 1 && ++fastStreak_ >= kRecoverAfter) {
            fastStreak_ = 0;
            interval_ /= 2;
        }
    }
    [[nodiscard]] std::uint32_t interval() const { return interval_; }
    [[nodiscard]] std::uint64_t skipped() const { return skipped_; }
    [[nodiscard]] std::uint64_t slowAcquires() const { return slowAcquires_; }
    [[nodiscard]] double lastAcquireMs() const { return lastAcquireMs_; }

private:
    std::uint32_t interval_ = 1;
    std::uint32_t frame_ = 0;
    std::uint32_t fastStreak_ = 0;
    std::uint64_t skipped_ = 0;
    std::uint64_t slowAcquires_ = 0;
    double lastAcquireMs_ = 0.0;
};

struct Output {
    OutputDesc desc;
    std::shared_ptr<OutputRuntime> runtime; // null while closed
    std::uint64_t framesPresented = 0;
    std::string lastError;
    PresentPacer pacer; // ADR-1089
    // ADR-1026: the Live panel's projection. This machine's rather than the project's: toJson leaves it out,
    // fromJson (a project load) keeps it and its open window, and Esc in its window closes it like the close button.
    bool projection = false;

    [[nodiscard]] bool open() const { return runtime != nullptr; }
    // Pixel size of the open window's swapchain (0 while closed). Defined in output_manager_gpu.cpp.
    [[nodiscard]] std::uint32_t pixelWidth() const;
    [[nodiscard]] std::uint32_t pixelHeight() const;
};

class OutputManager {
public:
    OutputManager();
    ~OutputManager();
    OutputManager(const OutputManager&) = delete;
    OutputManager& operator=(const OutputManager&) = delete;

    // ---- descriptors (GPU-free) ----
    // Fails on an invalid descriptor or a duplicate name. The returned pointer stays valid until
    // the output is removed or the set is replaced by fromJson().
    Result<Output*> add(OutputDesc desc);
    // Closes the window if open. Returns false when no output has that name.
    bool remove(const std::string& name);
    [[nodiscard]] Output* find(const std::string& name);
    [[nodiscard]] const Output* find(const std::string& name) const;
    [[nodiscard]] std::vector<std::unique_ptr<Output>>& outputs() { return outputs_; }
    [[nodiscard]] const std::vector<std::unique_ptr<Output>>& outputs() const { return outputs_; }
    [[nodiscard]] std::size_t openCount() const;

    // The project's "outputs" block: an array of OutputDesc. fromJson validates the whole array
    // first (names unique) and then replaces the set, closing any open windows. Projection outputs
    // (ADR-1026) are not the project's: toJson skips them and fromJson keeps them open; a project
    // output named like one is refused.
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] Result<void> fromJson(const nlohmann::json& outputs);

    // ---- windows and presentation (output_manager_gpu.cpp) ----
    // Creates a window and a swapchain for every enabled output that is not open, and the mapper
    // on first use. Outputs that fail keep their error in Output::lastError; the first error is
    // returned after every output was tried.
    [[nodiscard]] Result<void> open(gpu::Context& context, gpu::ShaderLibrary& shaders);
    void closeAll();
    // Closes the project's windows and leaves a projection (ADR-1026) open.
    void closeProjectOutputs();
    // Consumes the events routed to the output windows: resizes their swapchains and closes
    // windows the user closed (those outputs are disabled so they are not reopened). Call after
    // the primary window's pollEvents each frame. `pumpQueue` must be false when another window
    // already pumped the shared SDL queue this frame: pumping again with no handler would swallow
    // the events that window's handler has not seen yet (mouse input never reaching the UI).
    void pumpEvents(bool pumpQueue = true);
    // For each open output: acquire, draw `finalTexture` (width x height, TextureBinding usage)
    // through its mapping in one command buffer, submit, present. Minimised windows are skipped.
    [[nodiscard]] Result<void> presentAll(gpu::Context& context, const wgpu::Texture& finalTexture,
                                          std::uint32_t width, std::uint32_t height);
    [[nodiscard]] rendering::OutputMapper* mapper() { return mapper_.get(); }

private:
    std::vector<std::unique_ptr<Output>> outputs_;
    std::shared_ptr<rendering::OutputMapper> mapper_;
};

} // namespace avgen::app
