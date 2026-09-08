#pragma once

// WebGPU device context (ADR-001). This is the only module that includes webgpu headers, and
// context.cpp is the only file allowed to know it is talking to Dawn. Everything the engine
// needs from the API is reachable through the wgpu:: handles returned here; the facade adds
// nothing WebGPU does not have.

#include "core/error.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <string>

namespace avgen::gpu {

struct ContextDesc {
    void* metalLayer = nullptr;      // CAMetalLayer*; nullptr = headless (offscreen only)
    bool requestTimestamps = true;   // ask for the timestamp-query feature if available
    bool preferHighPerformance = true;
    std::string label = "avgen";
};

struct Capabilities {
    std::string adapterName;
    std::string backendName;
    std::string adapterType;
    std::string vendor;
    bool timestampQuery = false;
    wgpu::Limits limits{};
};

class Context {
public:
    static Result<std::unique_ptr<Context>> create(const ContextDesc& desc);
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    [[nodiscard]] const wgpu::Instance& instance() const { return instance_; }
    [[nodiscard]] const wgpu::Adapter& adapter() const { return adapter_; }
    [[nodiscard]] const wgpu::Device& device() const { return device_; }
    [[nodiscard]] const wgpu::Queue& queue() const { return queue_; }
    [[nodiscard]] const Capabilities& capabilities() const { return caps_; }

    // ---- surface (absent in headless mode) ----
    [[nodiscard]] bool hasSurface() const { return surface_ != nullptr; }
    [[nodiscard]] wgpu::TextureFormat surfaceFormat() const { return surfaceFormat_; }
    [[nodiscard]] std::uint32_t surfaceWidth() const { return surfaceWidth_; }
    [[nodiscard]] std::uint32_t surfaceHeight() const { return surfaceHeight_; }
    // (Re)configures the swapchain to the given pixel size. No-op when headless or size is zero.
    void configureSurface(std::uint32_t width, std::uint32_t height);
    // Acquires the current swapchain texture view. Fails on lost/outdated surfaces after one
    // reconfigure attempt.
    [[nodiscard]] Result<wgpu::TextureView> acquireSurfaceView();
    void present();

    // ---- synchronisation ----
    void processEvents();     // pumps callbacks (errors, map-async, compilation info)
    void waitForQueue();      // blocks until all submitted work has completed
    // Waits for a future with a timeout, pumping events. Returns false on timeout.
    bool waitFor(wgpu::Future future, std::uint64_t timeoutNs = 10'000'000'000ull);

    // ---- diagnostics ----
    [[nodiscard]] std::uint64_t errorCount() const { return errorCount_; }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }
    void clearErrors() { errorCount_ = 0; lastError_.clear(); }
    [[nodiscard]] bool deviceLost() const { return deviceLost_; }

    static std::string toString(wgpu::StringView view);

private:
    Context() = default;
    void onError(wgpu::ErrorType type, wgpu::StringView message);

    wgpu::Instance instance_;
    wgpu::Adapter adapter_;
    wgpu::Device device_;
    wgpu::Queue queue_;
    wgpu::Surface surface_;
    wgpu::TextureFormat surfaceFormat_ = wgpu::TextureFormat::Undefined;
    std::uint32_t surfaceWidth_ = 0;
    std::uint32_t surfaceHeight_ = 0;
    bool surfaceConfigured_ = false;
    Capabilities caps_;
    std::uint64_t errorCount_ = 0;
    std::string lastError_;
    bool deviceLost_ = false;
};

} // namespace avgen::gpu
