// NDI backend: GPU readback ring (three staging buffers, asynchronous map) feeding
// NDIlib_send_send_video_async_v2 with progressive BGRA8/RGBA8 frames at the app's frame rate.
//
// Per frame: publish() first pumps Dawn's events so finished maps are delivered, hands every
// newly mapped slot to NDI (the async send keeps reading a buffer until the *next* send call, so
// the previously sent slot is unmapped only then), and finally encodes a texture->buffer copy into
// a free slot and maps it. Nothing blocks: a frame is dropped when all slots are busy. Latency is
// one to two frames.

#include "share/ndi_runtime.hpp"
#include "share/share_backend.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"

#include <array>
#include <memory>

namespace avgen::share {

namespace {

constexpr std::size_t kRingSize = 3;

struct Slot {
    enum class State { Free, Copying, Mapped, Sent };
    wgpu::Buffer buffer;
    std::uint64_t capacity = 0;
    std::uint32_t width = 0, height = 0, bytesPerRow = 0;
    std::uint32_t fourCC = ndi::kFourCCBGRA;
    State state = State::Free;
    std::uint64_t sequence = 0; // publish order, so frames go out in order
};

// Shared with the map callbacks so a close() while a map is in flight stays safe.
struct RingState {
    std::array<Slot, kRingSize> slots{};
    bool closed = false;
};

class NdiBackend final : public ShareBackend {
public:
    ~NdiBackend() override { close(); }

    Result<void> open(gpu::Context& context, const std::string& name) override {
        const auto& rt = ndi::Runtime::get();
        if (!rt.loaded()) {
            return fail("{}", rt.error);
        }
        context_ = &context;
        ring_ = std::make_shared<RingState>();
        name_ = name;
        ndi::SendCreate create{};
        create.ndiName = name_.c_str();
        create.clockVideo = false; // the app paces frames; NDI must not sleep in send
        create.clockAudio = false;
        sender_ = rt.sendCreate(&create);
        if (sender_ == nullptr) {
            return fail("NDIlib_send_create failed");
        }
        log::info("NDI source \"{}\" started ({})", name, rt.versionText);
        return {};
    }

    Result<void> publish(const wgpu::Texture& source, std::uint32_t w, std::uint32_t h) override {
        if (sender_ == nullptr || context_ == nullptr) {
            return fail("NDI sender is not open");
        }
        auto& context = *context_;
        context.processEvents(); // deliver finished maps
        sendMappedSlots();

        Slot* slot = nullptr;
        for (auto& s : ring_->slots) {
            if (s.state == Slot::State::Free) {
                slot = &s;
                break;
            }
        }
        if (slot == nullptr) {
            ++dropped_;
            return {}; // readback ring saturated: skip this frame rather than block
        }
        const std::uint32_t bytesPerRow = (w * 4u + 255u) / 256u * 256u;
        const std::uint64_t needed = static_cast<std::uint64_t>(bytesPerRow) * h;
        if (!slot->buffer || slot->capacity < needed) {
            wgpu::BufferDescriptor desc{};
            desc.label = "ndi-readback";
            desc.size = needed;
            desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
            slot->buffer = context.device().CreateBuffer(&desc);
            slot->capacity = needed;
            if (!slot->buffer) {
                return setError("could not allocate an NDI readback buffer");
            }
        }
        slot->width = w;
        slot->height = h;
        slot->bytesPerRow = bytesPerRow;
        slot->fourCC = source.GetFormat() == wgpu::TextureFormat::BGRA8Unorm ? ndi::kFourCCBGRA : ndi::kFourCCRGBA;
        slot->sequence = ++sequence_;

        const auto errorsBefore = context.errorCount();
        wgpu::TexelCopyTextureInfo src{};
        src.texture = source;
        wgpu::TexelCopyBufferInfo dst{};
        dst.buffer = slot->buffer;
        dst.layout.bytesPerRow = bytesPerRow;
        dst.layout.rowsPerImage = h;
        const wgpu::Extent3D extent{w, h, 1};
        wgpu::CommandEncoderDescriptor encDesc{};
        encDesc.label = "ndi-readback";
        wgpu::CommandEncoder encoder = context.device().CreateCommandEncoder(&encDesc);
        encoder.CopyTextureToBuffer(&src, &dst, &extent);
        wgpu::CommandBuffer commands = encoder.Finish();
        context.queue().Submit(1, &commands);
        if (context.errorCount() != errorsBefore) {
            return setError("GPU error during the NDI readback copy: " + context.lastError());
        }

        slot->state = Slot::State::Copying;
        auto ring = ring_;
        const auto index = static_cast<std::size_t>(slot - ring_->slots.data());
        slot->buffer.MapAsync(wgpu::MapMode::Read, 0, static_cast<std::size_t>(needed), wgpu::CallbackMode::AllowProcessEvents,
                              [ring, index](wgpu::MapAsyncStatus status, wgpu::StringView) {
                                  auto& s = ring->slots[index];
                                  if (ring->closed) {
                                      return;
                                  }
                                  s.state = status == wgpu::MapAsyncStatus::Success ? Slot::State::Mapped : Slot::State::Free;
                              });
        width_ = w;
        height_ = h;
        lastError_.clear();
        return {};
    }

    void close() override {
        if (context_ != nullptr && ring_) {
            context_->waitForQueue();
            context_->processEvents();
            ring_->closed = true;
        }
        const auto& rt = ndi::Runtime::get();
        if (sender_ != nullptr && rt.loaded()) {
            rt.sendVideoAsyncV2(sender_, nullptr); // flush: NDI releases the last buffer
            rt.sendDestroy(sender_);
            sender_ = nullptr;
        }
        if (ring_) {
            for (auto& s : ring_->slots) {
                if (s.buffer) {
                    s.buffer.Destroy(); // also unmaps
                    s.buffer = nullptr;
                }
            }
            ring_.reset();
        }
        context_ = nullptr;
        frames_ = 0;
        dropped_ = 0;
        sequence_ = 0;
        width_ = height_ = 0;
    }

    void setFrameRate(int numerator, int denominator) override {
        frameRateN_ = numerator;
        frameRateD_ = denominator;
    }

    [[nodiscard]] ShareStats stats() const override {
        ShareStats s;
        s.framesPublished = frames_;
        s.width = width_;
        s.height = height_;
        s.lastError = lastError_;
        const auto& rt = ndi::Runtime::get();
        if (sender_ != nullptr && rt.loaded()) {
            s.clients = rt.sendGetNoConnections(sender_, 0);
            s.connected = s.clients > 0;
        }
        return s;
    }

private:
    Result<void> setError(std::string message) {
        lastError_ = message;
        return fail(std::move(message));
    }

    // Sends mapped slots in publish order; the slot NDI read before becomes free.
    void sendMappedSlots() {
        const auto& rt = ndi::Runtime::get();
        for (;;) {
            Slot* next = nullptr;
            for (auto& s : ring_->slots) {
                if (s.state == Slot::State::Mapped && (next == nullptr || s.sequence < next->sequence)) {
                    next = &s;
                }
            }
            if (next == nullptr) {
                return;
            }
            const auto size = static_cast<std::size_t>(next->bytesPerRow) * next->height;
            const void* mapped = next->buffer.GetConstMappedRange(0, size);
            if (mapped == nullptr) {
                next->state = Slot::State::Free;
                continue;
            }
            ndi::VideoFrameV2 frame{};
            frame.xres = static_cast<std::int32_t>(next->width);
            frame.yres = static_cast<std::int32_t>(next->height);
            frame.fourCC = next->fourCC;
            frame.frameRateN = frameRateN_;
            frame.frameRateD = frameRateD_;
            frame.frameFormatType = ndi::Progressive;
            frame.timecode = ndi::kTimecodeSynthesize;
            frame.data = static_cast<std::uint8_t*>(const_cast<void*>(mapped));
            frame.lineStrideInBytes = static_cast<std::int32_t>(next->bytesPerRow);
            rt.sendVideoAsyncV2(sender_, &frame);
            ++frames_;
            // NDI has now finished with the previously sent buffer.
            for (auto& s : ring_->slots) {
                if (s.state == Slot::State::Sent) {
                    s.buffer.Unmap();
                    s.state = Slot::State::Free;
                }
            }
            next->state = Slot::State::Sent;
        }
    }

    gpu::Context* context_ = nullptr;
    std::shared_ptr<RingState> ring_;
    std::string name_;
    ndi::SendInstance sender_ = nullptr;
    std::uint64_t frames_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t sequence_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    int frameRateN_ = 60;
    int frameRateD_ = 1;
    std::string lastError_;
};

} // namespace

bool ndiAvailable() { return ndi::Runtime::get().loaded(); }

std::string ndiDescribe() {
    const auto& rt = ndi::Runtime::get();
    if (rt.loaded()) {
        return rt.path + (rt.versionText.empty() ? "" : " (" + rt.versionText + ")");
    }
    return rt.error;
}

std::unique_ptr<ShareBackend> createNdiBackend() {
    if (!ndiAvailable()) {
        return nullptr;
    }
    return std::make_unique<NdiBackend>();
}

} // namespace avgen::share
