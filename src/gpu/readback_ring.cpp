#include "gpu/readback_ring.hpp"

#include "gpu/context.hpp"
#include "gpu/texture.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstring>

namespace avgen::gpu {

namespace {

std::uint32_t bytesPerPixel(ReadbackRing::Format format) {
    // Rgba8, Rg16Float, R32Float and R32Uint are all four bytes; only RGBA16Float is eight.
    return format == ReadbackRing::Format::Rgba16Float ? 8u : 4u;
}

} // namespace

ReadbackRing::ReadbackRing(Context& context, std::uint32_t slots) : context_(context) {
    slots_.resize(slots == 0 ? 1 : slots);
}

ReadbackRing::~ReadbackRing() {
    for (const auto slot : order_) {
        if (slots_[slot].inFlight && !slots_[slot].mapped) {
            (void)context_.waitFor(slots_[slot].future);
        }
    }
}

Result<std::size_t> ReadbackRing::acquireSlot() {
    context_.processEvents();
    harvest();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].inFlight) {
            return i;
        }
    }
    // Every slot is in flight: wait for the oldest (the only blocking point of the ring).
    const std::size_t oldest = order_.front();
    const auto begin = std::chrono::steady_clock::now();
    const bool ok = context_.waitFor(slots_[oldest].future);
    blockedSeconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    if (!ok) {
        return fail("readback ring: timed out waiting for frame {}", slots_[oldest].index);
    }
    harvest();
    if (slots_[oldest].inFlight) {
        return fail("readback ring: frame {} did not complete", slots_[oldest].index);
    }
    return oldest;
}

Result<void> ReadbackRing::record(std::size_t index, wgpu::CommandEncoder& encoder, const wgpu::Texture& texture,
                                  std::uint32_t width, std::uint32_t height, std::uint64_t frameIndex,
                                  Format format) {
    if (width == 0 || height == 0 || texture == nullptr) {
        return fail("readback ring: empty texture");
    }
    Slot& slot = slots_[index];
    const std::uint32_t unpaddedRow = width * bytesPerPixel(format);
    const std::uint32_t paddedRow = (unpaddedRow + 255u) / 256u * 256u; // WebGPU row alignment
    const std::uint64_t size = static_cast<std::uint64_t>(paddedRow) * height;
    if (slot.buffer == nullptr || slot.size != size) {
        wgpu::BufferDescriptor desc{};
        desc.label = "readback-ring";
        desc.size = size;
        desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
        slot.buffer = context_.device().CreateBuffer(&desc);
        if (slot.buffer == nullptr) {
            return fail("readback ring: cannot create a {} byte staging buffer", size);
        }
        slot.size = size;
    }
    slot.width = width;
    slot.height = height;
    slot.paddedRow = paddedRow;
    slot.format = format;
    slot.index = frameIndex;
    slot.mapped = false;
    slot.failed = false;
    slot.message.clear();

    wgpu::TexelCopyTextureInfo source{};
    source.texture = texture;
    source.mipLevel = 0;
    source.origin = {0, 0, 0};
    wgpu::TexelCopyBufferInfo destination{};
    destination.buffer = slot.buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = paddedRow;
    destination.layout.rowsPerImage = height;
    const wgpu::Extent3D extent{width, height, 1};
    encoder.CopyTextureToBuffer(&source, &destination, &extent);
    wgpu::CommandBuffer commands = encoder.Finish();
    context_.queue().Submit(1, &commands);

    // The map may only start once the copy is submitted; the callback fires from processEvents()
    // or a WaitAny, both on this thread, so the slot needs no synchronisation.
    slot.future = slot.buffer.MapAsync(wgpu::MapMode::Read, 0, static_cast<std::size_t>(size),
                                       wgpu::CallbackMode::AllowProcessEvents,
                                       [s = &slot](wgpu::MapAsyncStatus status, wgpu::StringView message) {
                                           s->mapped = true;
                                           s->failed = status != wgpu::MapAsyncStatus::Success;
                                           if (s->failed) {
                                               s->message = Context::toString(message);
                                           }
                                       });
    slot.inFlight = true;
    order_.push_back(index);
    return {};
}

Result<void> ReadbackRing::enqueue(wgpu::CommandEncoder& encoder, const wgpu::Texture& texture, std::uint32_t width,
                                   std::uint32_t height, std::uint64_t frameIndex, Format format) {
    auto slot = acquireSlot();
    if (!slot) {
        return std::unexpected(slot.error());
    }
    return record(*slot, encoder, texture, width, height, frameIndex, format);
}

Result<void> ReadbackRing::enqueue(const wgpu::Texture& texture, std::uint32_t width, std::uint32_t height,
                                   std::uint64_t frameIndex, Format format) {
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    return enqueue(encoder, texture, width, height, frameIndex, format);
}

ReadbackRing::Frame ReadbackRing::extract(Slot& slot) {
    Frame frame;
    frame.index = slot.index;
    frame.format = slot.format;
    const auto* data =
        static_cast<const std::uint8_t*>(slot.buffer.GetConstMappedRange(0, static_cast<std::size_t>(slot.size)));
    if (data == nullptr) {
        slot.failed = true;
        slot.message = "mapped range unavailable";
        return frame;
    }
    const std::size_t pixels = static_cast<std::size_t>(slot.width) * slot.height;
    // The three auxiliary formats. Each is four bytes a texel and each expands to RGBA floats, so
    // the encoder side has one shape to write whatever the target was.
    if (slot.format == Format::Rg16Float || slot.format == Format::R32Float ||
        slot.format == Format::R32Uint) {
        frame.imageF.width = slot.width;
        frame.imageF.height = slot.height;
        frame.imageF.rgba.resize(pixels * 4);
        std::vector<std::uint8_t> row(static_cast<std::size_t>(slot.width) * 4);
        for (std::uint32_t y = 0; y < slot.height; ++y) {
            // The staging rows are 256-byte aligned, so copy the row out before reading it as
            // anything wider than a byte -- the same reason the RGBA16F branch below does.
            std::memcpy(row.data(), data + static_cast<std::size_t>(y) * slot.paddedRow, row.size());
            float* out = frame.imageF.rgba.data() + static_cast<std::size_t>(y) * slot.width * 4;
            for (std::uint32_t x = 0; x < slot.width; ++x, out += 4) {
                if (slot.format == Format::Rg16Float) {
                    std::uint16_t halves[2];
                    std::memcpy(halves, row.data() + static_cast<std::size_t>(x) * 4, 4);
                    float xy[2];
                    halfToFloatArray(halves, xy, 2);
                    out[0] = xy[0];
                    out[1] = xy[1];
                    out[2] = 0.0f;
                } else if (slot.format == Format::R32Float) {
                    float v = 0.0f;
                    std::memcpy(&v, row.data() + static_cast<std::size_t>(x) * 4, 4);
                    out[0] = out[1] = out[2] = v;
                } else {
                    std::uint32_t v = 0;
                    std::memcpy(&v, row.data() + static_cast<std::size_t>(x) * 4, 4);
                    // An identifier is an integer and this is the only lossless float that holds
                    // it up to 2^24; the EXR it lands in is written as 32-bit for the same reason.
                    out[0] = out[1] = out[2] = static_cast<float>(v);
                }
                out[3] = 1.0f;
            }
        }
    } else if (slot.format == Format::Rgba8) {
        frame.image.width = slot.width;
        frame.image.height = slot.height;
        frame.image.rgba.resize(pixels * 4);
        const std::size_t row = static_cast<std::size_t>(slot.width) * 4;
        for (std::uint32_t y = 0; y < slot.height; ++y) {
            std::memcpy(frame.image.rgba.data() + y * row, data + static_cast<std::size_t>(y) * slot.paddedRow, row);
        }
    } else {
        frame.imageF.width = slot.width;
        frame.imageF.height = slot.height;
        frame.imageF.rgba.resize(pixels * 4);
        const std::size_t rowValues = static_cast<std::size_t>(slot.width) * 4;
        std::vector<std::uint16_t> row(rowValues);
        for (std::uint32_t y = 0; y < slot.height; ++y) {
            // The staging rows are only 256-byte aligned in the buffer, so copy before converting.
            std::memcpy(row.data(), data + static_cast<std::size_t>(y) * slot.paddedRow, rowValues * 2);
            halfToFloatArray(row.data(), frame.imageF.rgba.data() + y * rowValues, rowValues);
        }
    }
    return frame;
}

void ReadbackRing::harvest() {
    while (!order_.empty()) {
        Slot& slot = slots_[order_.front()];
        if (!slot.mapped) {
            break; // keep submission order: a newer frame never overtakes an older one
        }
        if (!slot.failed) {
            Frame frame = extract(slot);
            slot.buffer.Unmap();
            if (!slot.failed) {
                completed_.push_back(std::move(frame));
            }
        }
        if (slot.failed && error_.empty()) {
            error_ = fmt::format("frame {}: readback map failed: {}", slot.index, slot.message);
        }
        slot.inFlight = false;
        order_.pop_front();
    }
}

std::optional<ReadbackRing::Frame> ReadbackRing::poll() {
    context_.processEvents();
    harvest();
    if (completed_.empty()) {
        return std::nullopt;
    }
    Frame frame = std::move(completed_.front());
    completed_.pop_front();
    return frame;
}

Result<void> ReadbackRing::flush() {
    while (!order_.empty()) {
        Slot& slot = slots_[order_.front()];
        if (!slot.mapped && !context_.waitFor(slot.future)) {
            return fail("readback ring: timed out waiting for frame {}", slot.index);
        }
        harvest();
    }
    if (!error_.empty()) {
        return fail("{}", error_);
    }
    return {};
}

} // namespace avgen::gpu
