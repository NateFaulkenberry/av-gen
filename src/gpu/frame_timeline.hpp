#pragma once

// One GPU timestamp timeline for the whole frame. See docs/performance.md.
//
// Every pass writes ONE timestamp, at its end, into the next slot of a single query set, in
// submission order. A pass's cost is then `end[i] - end[i-1]`: the interval between two
// consecutive "the GPU has finished everything up to here" markers. The intervals partition the
// frame exactly, so they sum to it and no pass can be charged for another's work.
//
// This replaces per-pass begin/end pairs, which were not measuring what they claimed. A pass's
// own begin timestamp is written when the pass is *reached*, not when its work starts, so a pass
// behind a heavy one absorbed the drain of everything still in flight ahead of it. Measured on
// the world scene: the volumetric pass reported 39.4 ms of a 46 ms frame, while removing
// volumetrics entirely moved the frame from 53.1 ms to 47.7 -- the pass costs 5.4 ms, and the
// number that claimed 39.4 did not move when its own march steps were halved. See
// docs/performance.md.
//
// Passes that are not marked do not vanish; their cost is charged to the next marked pass. So
// mark every pass in the frame, or knowingly group a run of them under one label.
//
// Readback is the same non-stalling ring as before: resolve once per frame into a staging slot,
// map asynchronously, and read whichever slot has landed. The render thread never waits.

#include "gpu/timeline_math.hpp"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::gpu {

class Context;

class FrameTimeline {
public:
    // Timestamps per frame. Slot 0 is the frame origin; the rest are pass ends. The world scene
    // uses about 30 with every subsystem live and the post chain at full length.
    static constexpr std::uint32_t kMaxMarks = 128;

    explicit FrameTimeline(Context& context);
    // Waits for in-flight readbacks so no map callback can fire into a destroyed slot.
    ~FrameTimeline();
    FrameTimeline(const FrameTimeline&) = delete;
    FrameTimeline& operator=(const FrameTimeline&) = delete;

    [[nodiscard]] bool available() const { return available_; }

    // Starts a frame's encoding: forgets last frame's marks. Call once, before the first pass.
    void beginFrame();

    // Reserves this pass's slot and returns the timestamp writes to hand to the pass descriptor.
    // The first mark of a frame also opens the timeline (it writes slot 0 at the start of its own
    // pass). Returns nullptr when timestamps are unavailable or the timeline is full, which a
    // caller can assign straight to `timestampWrites`.
    [[nodiscard]] const wgpu::PassTimestampWrites* mark(std::string_view label);

    // Call after every pass is encoded and before Finish(): resolves the used range into a
    // staging slot. Cheap and does nothing when the frame marked nothing.
    void resolve(wgpu::CommandEncoder& encoder);
    // Call after Submit(): starts the async map and pumps whatever has landed. Never blocks.
    void collect();

    // The interval math itself lives in gpu/timeline_math.hpp, where the unit tests can reach it
    // without a device.
    using Entry = TimelineInterval;
    // Last completed frame's passes, in submission order. Empty until one has landed.
    [[nodiscard]] const std::vector<Entry>& passes() const { return passes_; }
    // Whole timeline: the last pass's end minus the frame origin. -1 when nothing has landed.
    [[nodiscard]] double frameMs() const { return frameMs_; }
    // Sum of every pass carrying this label in the last completed frame; -1 when it did not run.
    [[nodiscard]] double msFor(std::string_view label) const;
    // Sum of every pass whose label starts with this prefix; -1 when none did.
    [[nodiscard]] double msForPrefix(std::string_view prefix) const;
    // Timestamps this frame has claimed so far (0 before the first mark).
    [[nodiscard]] std::uint32_t marks() const { return count_; }
    // Marks refused this frame because the timeline was full; a non-zero value means the
    // per-pass numbers below the overflow point are missing, not that the frame was cheap.
    [[nodiscard]] std::uint32_t overflowed() const { return overflow_; }
    // Slots the driver left unwritten in the last completed frame. Metal skips the end-of-pass
    // timestamp of a render pass with no draws (an empty clear), so those passes report 0 and
    // their cost, such as it is, lands on the next pass. Non-zero is expected, not an error.
    [[nodiscard]] std::uint32_t unwritten() const { return unwritten_; }

private:
    struct Slot {
        wgpu::Buffer resolve;
        wgpu::Buffer read;
        wgpu::Future mapFuture{};
        std::vector<std::string> labels; // labels[i] names the pass that ends at timestamp i
        std::uint32_t count = 0;
        bool inFlight = false;
        bool ready = false;
        bool failed = false;
    };
    static constexpr std::size_t kSlots = 4;
    static constexpr std::uint64_t kBufferBytes = static_cast<std::uint64_t>(kMaxMarks) * sizeof(std::uint64_t);

    Context& context_;
    bool available_ = false;
    bool rawDump_ = false;
    wgpu::QuerySet querySet_;
    // Stable storage: a pass descriptor holds the pointer until BeginRenderPass reads it.
    std::array<wgpu::PassTimestampWrites, kMaxMarks> writes_{};
    std::array<std::string, kMaxMarks> labels_{};
    std::array<Slot, kSlots> slots_{};
    std::uint32_t count_ = 0;
    std::uint32_t overflow_ = 0;
    std::size_t next_ = 0;
    int pendingSlot_ = -1;
    std::vector<Entry> passes_;
    std::uint32_t unwritten_ = 0;
    double frameMs_ = -1.0;
};

} // namespace avgen::gpu
