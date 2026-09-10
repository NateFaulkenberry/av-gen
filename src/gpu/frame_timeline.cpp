#include "gpu/frame_timeline.hpp"

#include "gpu/context.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace avgen::gpu {

FrameTimeline::FrameTimeline(Context& context) : context_(context) {
    // AVGEN_TIMELINE_RAW=1 dumps the resolved timestamps, which is the only way to tell a pass
    // that genuinely cost nothing from a slot the driver never wrote.
    rawDump_ = std::getenv("AVGEN_TIMELINE_RAW") != nullptr;
    if (!context.capabilities().timestampQuery) {
        return;
    }
    wgpu::QuerySetDescriptor qsDesc{};
    qsDesc.label = "frame-timeline";
    qsDesc.type = wgpu::QueryType::Timestamp;
    qsDesc.count = kMaxMarks;
    querySet_ = context.device().CreateQuerySet(&qsDesc);
    if (!querySet_) {
        return;
    }
    for (std::uint32_t i = 0; i < kMaxMarks; ++i) {
        writes_[i].querySet = querySet_;
        // Slot 0 is the frame origin, written at the beginning of whichever pass marks first.
        writes_[i].beginningOfPassWriteIndex = i == 1 ? 0 : wgpu::kQuerySetIndexUndefined;
        writes_[i].endOfPassWriteIndex = i;
    }
    for (auto& slot : slots_) {
        wgpu::BufferDescriptor resolveDesc{};
        resolveDesc.label = "timeline-resolve";
        resolveDesc.size = kBufferBytes;
        resolveDesc.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
        slot.resolve = context.device().CreateBuffer(&resolveDesc);
        wgpu::BufferDescriptor readDesc{};
        readDesc.label = "timeline-read";
        readDesc.size = kBufferBytes;
        readDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
        slot.read = context.device().CreateBuffer(&readDesc);
        slot.labels.resize(kMaxMarks);
    }
    available_ = true;
}

FrameTimeline::~FrameTimeline() {
    // A MapAsync callback carries a raw Slot pointer; make every pending one complete (or fail)
    // while the slots still exist. WaitAny works for AllowProcessEvents futures.
    for (auto& slot : slots_) {
        if (slot.inFlight) {
            context_.waitFor(slot.mapFuture, 2'000'000'000ull);
            slot.inFlight = false;
        }
    }
}

void FrameTimeline::beginFrame() {
    count_ = 0;
    overflow_ = 0;
}

const wgpu::PassTimestampWrites* FrameTimeline::mark(std::string_view label) {
    if (!available_) {
        return nullptr;
    }
    // The first mark claims slot 0 for the frame origin as well as slot 1 for its own end.
    const std::uint32_t index = count_ == 0 ? 1u : count_;
    if (index >= kMaxMarks) {
        ++overflow_;
        return nullptr;
    }
    labels_[index].assign(label);
    count_ = index + 1;
    return &writes_[index];
}

void FrameTimeline::resolve(wgpu::CommandEncoder& encoder) {
    if (!available_ || count_ < 2) {
        return; // nothing marked, or only the origin: no interval to report
    }
    Slot& slot = slots_[next_];
    if (slot.inFlight) {
        pendingSlot_ = -1; // ring full: skip this frame's measurement
        return;
    }
    const std::uint64_t bytes = static_cast<std::uint64_t>(count_) * sizeof(std::uint64_t);
    encoder.ResolveQuerySet(querySet_, 0, count_, slot.resolve, 0);
    encoder.CopyBufferToBuffer(slot.resolve, 0, slot.read, 0, bytes);
    slot.count = count_;
    for (std::uint32_t i = 0; i < count_; ++i) {
        slot.labels[i] = labels_[i];
    }
    pendingSlot_ = static_cast<int>(next_);
    next_ = (next_ + 1) % kSlots;
}

void FrameTimeline::collect() {
    if (!available_) {
        return;
    }
    if (pendingSlot_ >= 0) {
        Slot& slot = slots_[static_cast<std::size_t>(pendingSlot_)];
        slot.inFlight = true;
        slot.ready = false;
        slot.failed = false;
        Slot* raw = &slot;
        slot.mapFuture = slot.read.MapAsync(
            wgpu::MapMode::Read, 0, kBufferBytes, wgpu::CallbackMode::AllowProcessEvents,
            [](wgpu::MapAsyncStatus status, wgpu::StringView, Slot* s) {
                s->ready = status == wgpu::MapAsyncStatus::Success;
                s->failed = status != wgpu::MapAsyncStatus::Success;
            },
            raw);
        pendingSlot_ = -1;
    }
    context_.processEvents();
    for (auto& slot : slots_) {
        if (!slot.inFlight) {
            continue;
        }
        if (slot.ready) {
            const auto* data = static_cast<const std::uint64_t*>(slot.read.GetConstMappedRange(0, kBufferBytes));
            if (data != nullptr && slot.count >= 2) {
                TimelineSpan span = timelineIntervals(data, slot.labels.data(), slot.count);
                passes_ = std::move(span.passes);
                unwritten_ = span.unwritten;
                frameMs_ = span.frameMs;
                if (rawDump_) {
                    std::string line;
                    for (std::uint32_t i = 0; i < slot.count; ++i) {
                        line += " " + slot.labels[i] + ":" + std::to_string(data[i]);
                    }
                    std::fprintf(stderr, "RAW%s\n", line.c_str());
                }
            }
            slot.read.Unmap();
            slot.inFlight = false;
            slot.ready = false;
        } else if (slot.failed) {
            slot.inFlight = false;
            slot.failed = false;
        }
    }
}

double FrameTimeline::msFor(std::string_view label) const {
    double total = -1.0;
    for (const auto& entry : passes_) {
        if (entry.label == label) {
            total = total < 0.0 ? entry.ms : total + entry.ms;
        }
    }
    return total;
}

double FrameTimeline::msForPrefix(std::string_view prefix) const {
    double total = -1.0;
    for (const auto& entry : passes_) {
        if (entry.label.size() >= prefix.size() &&
            std::string_view(entry.label).substr(0, prefix.size()) == prefix) {
            total = total < 0.0 ? entry.ms : total + entry.ms;
        }
    }
    return total;
}

} // namespace avgen::gpu
