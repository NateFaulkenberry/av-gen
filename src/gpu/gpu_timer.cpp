#include "gpu/gpu_timer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"

namespace avgen::gpu {

GpuTimer::GpuTimer(Context& context) : context_(context) {
    if (!context.capabilities().timestampQuery) {
        return;
    }
    wgpu::QuerySetDescriptor qsDesc{};
    qsDesc.label = "frame-timestamps";
    qsDesc.type = wgpu::QueryType::Timestamp;
    qsDesc.count = 2;
    querySet_ = context.device().CreateQuerySet(&qsDesc);
    if (!querySet_) {
        return;
    }
    begin_.querySet = querySet_;
    begin_.beginningOfPassWriteIndex = 0;
    end_.querySet = querySet_;
    end_.endOfPassWriteIndex = 1;

    for (auto& slot : slots_) {
        wgpu::BufferDescriptor resolveDesc{};
        resolveDesc.label = "timestamp-resolve";
        resolveDesc.size = 16;
        resolveDesc.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
        slot.resolve = context.device().CreateBuffer(&resolveDesc);
        wgpu::BufferDescriptor readDesc{};
        readDesc.label = "timestamp-read";
        readDesc.size = 16;
        readDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
        slot.read = context.device().CreateBuffer(&readDesc);
    }
    available_ = true;
}

GpuTimer::~GpuTimer() {
    // A MapAsync callback carries a raw Slot pointer; make every pending one complete (or fail)
    // while the slots still exist. WaitAny works for AllowProcessEvents futures.
    for (auto& slot : slots_) {
        if (slot.inFlight) {
            context_.waitFor(slot.mapFuture, 2'000'000'000ull);
            slot.inFlight = false;
        }
    }
}

void GpuTimer::resolve(wgpu::CommandEncoder& encoder) {
    if (!available_) {
        return;
    }
    Slot& slot = slots_[next_];
    if (slot.inFlight) {
        pendingSlot_ = -1; // ring full: skip this frame's measurement
        return;
    }
    encoder.ResolveQuerySet(querySet_, 0, 2, slot.resolve, 0);
    encoder.CopyBufferToBuffer(slot.resolve, 0, slot.read, 0, 16);
    pendingSlot_ = static_cast<int>(next_);
    next_ = (next_ + 1) % kSlots;
}

double GpuTimer::collect() {
    if (!available_) {
        return -1.0;
    }
    if (pendingSlot_ >= 0) {
        Slot& slot = slots_[static_cast<std::size_t>(pendingSlot_)];
        slot.inFlight = true;
        slot.ready = false;
        slot.failed = false;
        Slot* raw = &slot;
        slot.mapFuture = slot.read.MapAsync(
            wgpu::MapMode::Read, 0, 16, wgpu::CallbackMode::AllowProcessEvents,
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
            const auto* data = static_cast<const std::uint64_t*>(slot.read.GetConstMappedRange(0, 16));
            if (data != nullptr && data[1] >= data[0]) {
                lastMs_ = static_cast<double>(data[1] - data[0]) * 1e-6;
            }
            slot.read.Unmap();
            slot.inFlight = false;
            slot.ready = false;
        } else if (slot.failed) {
            slot.inFlight = false;
            slot.failed = false;
        }
    }
    return lastMs_;
}

} // namespace avgen::gpu
