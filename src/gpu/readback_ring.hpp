#pragma once

// Asynchronous GPU->CPU readback for offline rendering (ADR-020 follow-up; research
// offline-rendering.md §6): a ring of N staging buffers. Frame f's texture->buffer copy goes at
// the end of the frame's own command buffer, the map starts right after the submit, and the
// render thread keeps submitting frames f+1, f+2 while the GPU finishes f. poll() hands back
// completed frames in submission order without blocking; enqueue() blocks only when every slot
// is in flight (the "semaphore" of Apple's triple-buffering guidance). Three slots overlap the
// GPU with the CPU-side hashing and encoding; readTexture8()/readTextureF16() stay for tests.

#include "core/error.hpp"
#include "gpu/readback.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace avgen::gpu {

class Context;

class ReadbackRing {
public:
    // The beauty path needs the first two. The three after it are the auxiliary targets an AOV
    // export reads (ADR-242): RG16Float velocity, R32Float linear depth, R32Uint identifiers. All
    // three arrive as `imageF`, expanded to RGBA -- a single-channel target replicates into RGB so
    // the file is viewable and every channel carries the same number, and velocity fills R and G.
    // The consumer is `writeExr`, which takes RGBA floats, so one unpacked shape serves all of them.
    enum class Format : std::uint8_t { Rgba8, Rgba16Float, Rg16Float, R32Float, R32Uint };

    struct Frame {
        std::uint64_t index = 0;
        Format format = Format::Rgba8;
        Image8 image;  // filled for Rgba8
        ImageF imageF; // filled for Rgba16Float (half converted to float, exact)
    };

    explicit ReadbackRing(Context& context, std::uint32_t slots = 3);
    ~ReadbackRing(); // waits for in-flight maps so no callback fires into a destroyed slot
    ReadbackRing(const ReadbackRing&) = delete;
    ReadbackRing& operator=(const ReadbackRing&) = delete;

    // Appends the copy of `texture` (the format must match: RGBA8Unorm for Rgba8, RGBA16Float for
    // Rgba16Float, RG16Float, R32Float and R32Uint for their own; needs CopySrc usage) to `encoder`, finishes and submits it, and starts mapping the staging
    // buffer. Blocks only while every slot is in flight.
    [[nodiscard]] Result<void> enqueue(wgpu::CommandEncoder& encoder, const wgpu::Texture& texture,
                                       std::uint32_t width, std::uint32_t height, std::uint64_t frameIndex,
                                       Format format = Format::Rgba8);
    // Same with a command buffer of its own, for a texture whose contents are already submitted.
    [[nodiscard]] Result<void> enqueue(const wgpu::Texture& texture, std::uint32_t width, std::uint32_t height,
                                       std::uint64_t frameIndex, Format format = Format::Rgba8);

    // The oldest completed frame, if any. Never blocks; frames come back in enqueue order.
    [[nodiscard]] std::optional<Frame> poll();
    // Waits for everything in flight; afterwards poll() returns every remaining frame.
    [[nodiscard]] Result<void> flush();

    [[nodiscard]] std::uint32_t slots() const { return static_cast<std::uint32_t>(slots_.size()); }
    [[nodiscard]] std::uint32_t inFlight() const { return static_cast<std::uint32_t>(order_.size()); }
    [[nodiscard]] std::size_t completed() const { return completed_.size(); }
    // First map failure, if any (poll() drops the failed frame and records it here).
    [[nodiscard]] const std::string& error() const { return error_; }
    // Wall time enqueue() spent blocked because every slot was in flight (GPU-bound indicator).
    [[nodiscard]] double blockedSeconds() const { return blockedSeconds_; }

private:
    struct Slot {
        wgpu::Buffer buffer;
        std::uint64_t size = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t paddedRow = 0;
        Format format = Format::Rgba8;
        std::uint64_t index = 0;
        wgpu::Future future{};
        bool inFlight = false;
        bool mapped = false;
        bool failed = false;
        std::string message;
    };
    [[nodiscard]] Result<std::size_t> acquireSlot();
    [[nodiscard]] Result<void> record(std::size_t slot, wgpu::CommandEncoder& encoder, const wgpu::Texture& texture,
                                      std::uint32_t width, std::uint32_t height, std::uint64_t frameIndex,
                                      Format format);
    void harvest(); // moves mapped slots, oldest first, into completed_
    Frame extract(Slot& slot);

    Context& context_;
    std::vector<Slot> slots_;
    std::deque<std::size_t> order_; // in-flight slots, oldest first
    std::deque<Frame> completed_;
    std::string error_;
    double blockedSeconds_ = 0.0;
};

} // namespace avgen::gpu
