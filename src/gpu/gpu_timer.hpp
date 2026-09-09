#pragma once

// GPU frame timing via timestamp queries, read back through a small ring of staging buffers so
// the render thread never stalls. Reports -1 when the feature is unavailable.

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>

namespace avgen::gpu {

class Context;

class GpuTimer {
public:
    explicit GpuTimer(Context& context);
    // Waits for in-flight readbacks so no map callback can fire into a destroyed slot.
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;
    [[nodiscard]] bool available() const { return available_; }

    // Timestamp writes to attach to the first and last pass of the frame. Null when unavailable.
    [[nodiscard]] const wgpu::PassTimestampWrites* beginWrites() const { return available_ ? &begin_ : nullptr; }
    [[nodiscard]] const wgpu::PassTimestampWrites* endWrites() const { return available_ ? &end_ : nullptr; }
    // Both timestamps on one pass (a single compute or render pass measured on its own).
    [[nodiscard]] const wgpu::PassTimestampWrites* passWrites() const { return available_ ? &both_ : nullptr; }

    // Call after all passes are encoded, before Finish(): resolves and copies into a staging slot.
    void resolve(wgpu::CommandEncoder& encoder);
    // Call after Submit(): starts the async map. Pumps completed slots; returns latest ms or -1.
    double collect();
    [[nodiscard]] double lastFrameMs() const { return lastMs_; }

private:
    struct Slot {
        wgpu::Buffer resolve;
        wgpu::Buffer read;
        wgpu::Future mapFuture{};
        bool inFlight = false;
        bool ready = false;
        bool failed = false;
    };
    static constexpr std::size_t kSlots = 4;

    Context& context_;
    bool available_ = false;
    wgpu::QuerySet querySet_;
    wgpu::PassTimestampWrites begin_{};
    wgpu::PassTimestampWrites end_{};
    wgpu::PassTimestampWrites both_{};
    std::array<Slot, kSlots> slots_{};
    std::size_t next_ = 0;
    int pendingSlot_ = -1;
    double lastMs_ = -1.0;
};

} // namespace avgen::gpu
