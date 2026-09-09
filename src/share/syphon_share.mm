// Syphon backend (macOS). Native path on top of Dawn's IOSurface interop:
//
//   Dawn texture --CopyTextureToTexture--> IOSurface (wrapped as a wgpu::SharedTextureMemory)
//   --SyphonMetalServer blit (separate MTLDevice/queue)--> Syphon's own IOSurface --> clients
//
// Synchronisation is entirely GPU-side, so publish() never waits for the GPU:
//   - EndAccess hands back Dawn's queue MTLSharedEvent with the copy's serial; Syphon's command
//     buffer encodes a wait on that value before its blit (no CPU wait, no extra frame latency).
//   - Syphon's command buffer signals our own MTLSharedEvent when its blit is done; the next
//     frame's BeginAccess passes that event as a fence so Dawn's copy cannot overwrite the surface
//     while the blit still reads it. One IOSurface therefore suffices.
// The IOSurface takes the source's pixel format (BGRA8 or RGBA8, both understood by Dawn and
// Metal); Syphon blits BGRA sources and re-draws RGBA ones through its sampling shader, so the
// published frame is always the BGRA8 surface clients expect. Compiled with ARC.

#ifdef __APPLE__

#include "share/share_backend.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#import <Syphon/SyphonMetalServer.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace avgen::share {

namespace {

class SyphonBackend final : public ShareBackend {
public:
    ~SyphonBackend() override { close(); }

    Result<void> open(gpu::Context& context, const std::string& name) override {
        if (!context.capabilities().sharedTextureIOSurface) {
            return fail("the GPU device lacks Dawn's SharedTextureMemoryIOSurface/SharedFenceMTLSharedEvent features");
        }
        context_ = &context;
        device_ = MTLCreateSystemDefaultDevice();
        if (device_ == nil) {
            return fail("no Metal device");
        }
        queue_ = [device_ newCommandQueue];
        readDone_ = [device_ newSharedEvent];
        if (queue_ == nil || readDone_ == nil) {
            return fail("failed to create a Metal command queue or shared event");
        }
        readDone_.label = @"avgen syphon read-done";
        wgpu::SharedFenceMTLSharedEventDescriptor eventDesc{};
        eventDesc.sharedEvent = (__bridge void*)readDone_;
        wgpu::SharedFenceDescriptor fenceDesc{};
        fenceDesc.nextInChain = &eventDesc;
        fenceDesc.label = "avgen-syphon-read-done";
        readDoneFence_ = context.device().ImportSharedFence(&fenceDesc);
        if (!readDoneFence_) {
            return fail("Dawn refused to import the MTLSharedEvent fence");
        }
        server_ = [[SyphonMetalServer alloc] initWithName:[NSString stringWithUTF8String:name.c_str()]
                                                   device:device_
                                                  options:nil];
        if (server_ == nil) {
            return fail("SyphonMetalServer could not be started");
        }
        log::info("Syphon server \"{}\" started", name);
        return {};
    }

    Result<void> publish(const wgpu::Texture& source, std::uint32_t w, std::uint32_t h) override {
        if (server_ == nil || context_ == nullptr) {
            return fail("Syphon server is not open");
        }
        auto& context = *context_;
        const auto errorsBefore = context.errorCount();
        if (auto ok = ensureSurface(w, h, source.GetFormat()); !ok) {
            setError(ok.error().message);
            return ok;
        }

        // ---- Dawn: copy the frame into the IOSurface between BeginAccess/EndAccess ----
        wgpu::SharedTextureMemoryBeginAccessDescriptor begin{};
        begin.initialized = initialized_;
        begin.concurrentRead = false;
        if (readDoneValue_ > 0) {
            begin.fenceCount = 1;
            begin.fences = &readDoneFence_;
            begin.signaledValueCount = 1;
            begin.signaledValues = &readDoneValue_;
        }
        if (memory_.BeginAccess(wrapped_, &begin) != wgpu::Status::Success) {
            return setError("BeginAccess on the shared IOSurface texture failed");
        }
        wgpu::TexelCopyTextureInfo src{};
        src.texture = source;
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = wrapped_;
        const wgpu::Extent3D extent{w, h, 1};
        wgpu::CommandEncoderDescriptor encDesc{};
        encDesc.label = "syphon-publish";
        wgpu::CommandEncoder encoder = context.device().CreateCommandEncoder(&encDesc);
        encoder.CopyTextureToTexture(&src, &dst, &extent);
        wgpu::CommandBuffer commands = encoder.Finish();
        context.queue().Submit(1, &commands);

        wgpu::SharedTextureMemoryEndAccessState end{};
        if (memory_.EndAccess(wrapped_, &end) != wgpu::Status::Success) {
            return setError("EndAccess on the shared IOSurface texture failed");
        }
        initialized_ = true;
        id<MTLSharedEvent> copyDone = nil;
        std::uint64_t copyDoneValue = 0;
        for (std::size_t i = 0; i < end.fenceCount && i < end.signaledValueCount; ++i) {
            wgpu::SharedFenceMTLSharedEventExportInfo eventInfo{};
            wgpu::SharedFenceExportInfo info{};
            info.nextInChain = &eventInfo;
            end.fences[i].ExportInfo(&info);
            if (info.type == wgpu::SharedFenceType::MTLSharedEvent && eventInfo.sharedEvent != nullptr) {
                copyDone = (__bridge id<MTLSharedEvent>)eventInfo.sharedEvent;
                copyDoneValue = end.signaledValues[i];
            }
        }
        if (context.errorCount() != errorsBefore) {
            return setError("GPU error during the shared-texture copy: " + context.lastError());
        }
        if (copyDone == nil) {
            return setError("EndAccess returned no MTLSharedEvent fence");
        }

        // ---- Metal: wait for the copy, blit into Syphon's surface, signal read-done, publish ----
        @autoreleasepool {
            id<MTLCommandBuffer> commandBuffer = [queue_ commandBuffer];
            if (commandBuffer == nil) {
                return setError("could not create a Metal command buffer");
            }
            commandBuffer.label = @"avgen syphon publish";
            [commandBuffer encodeWaitForEvent:copyDone value:copyDoneValue];
            [server_ publishFrameTexture:surfaceTexture_
                         onCommandBuffer:commandBuffer
                             imageRegion:NSMakeRect(0, 0, w, h)
                                 flipped:NO];
            ++readDoneValue_;
            [commandBuffer encodeSignalEvent:readDone_ value:readDoneValue_];
            // Runs after Syphon's own completed handler (handlers fire in registration order), so
            // once it has fired the frame has been announced to clients. ensureSurface() relies on
            // that to make size changes deterministic.
            const std::uint64_t frameIndex = frames_.fetch_add(1, std::memory_order_relaxed) + 1;
            [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
                {
                    std::lock_guard lock(mutex_);
                    announced_ = frameIndex;
                }
                announcedChanged_.notify_all();
            }];
            [commandBuffer commit];
            std::lock_guard lock(mutex_);
            lastCommandBuffer_ = commandBuffer;
            lastError_.clear();
        }
        return {};
    }

    void close() override {
        waitForAnnouncements(std::chrono::seconds(2)); // Syphon's blit still reads our surface
        {
            std::lock_guard lock(mutex_);
            lastCommandBuffer_ = nil;
        }
        if (server_ != nil) {
            [server_ stop];
            server_ = nil;
        }
        releaseSurface();
        readDoneFence_ = nullptr;
        readDone_ = nil;
        queue_ = nil;
        device_ = nil;
        context_ = nullptr;
        readDoneValue_ = 0;
        frames_.store(0, std::memory_order_relaxed);
        std::lock_guard lock(mutex_);
        announced_ = 0;
    }

    void setFrameRate(int, int) override {} // Syphon frames carry no rate

    [[nodiscard]] ShareStats stats() const override {
        ShareStats s;
        s.framesPublished = frames_.load(std::memory_order_relaxed);
        s.width = width_;
        s.height = height_;
        s.clients = -1;
        s.connected = server_ != nil && server_.hasClients;
        std::lock_guard lock(mutex_);
        s.lastError = lastError_;
        return s;
    }

private:
    Result<void> setError(std::string message) {
        {
            std::lock_guard lock(mutex_);
            lastError_ = message;
        }
        return fail(std::move(message));
    }

    // Blocks until every published frame's command buffer has completed and been announced.
    void waitForAnnouncements(std::chrono::milliseconds timeout) {
        const auto target = frames_.load(std::memory_order_relaxed);
        std::unique_lock lock(mutex_);
        if (!announcedChanged_.wait_for(lock, timeout, [&] { return announced_ >= target; })) {
            log::warn("Syphon: timed out waiting for frame {} to complete", target);
        }
    }

    void releaseSurface() {
        if (wrapped_) {
            wrapped_.Destroy();
            wrapped_ = nullptr;
        }
        memory_ = nullptr;
        surfaceTexture_ = nil;
        if (surface_ != nullptr) {
            CFRelease(surface_);
            surface_ = nullptr;
        }
        width_ = height_ = 0;
        format_ = wgpu::TextureFormat::Undefined;
        initialized_ = false;
    }

    // (Re)creates the IOSurface, its Dawn wrapper and its Metal texture when size or format change.
    Result<void> ensureSurface(std::uint32_t w, std::uint32_t h, wgpu::TextureFormat format) {
        if (surface_ != nullptr && w == width_ && h == height_ && format == format_) {
            return {};
        }
        // A previous Syphon blit may still read the old surface, and Syphon announces frames from
        // completion handlers that would otherwise race with its own surface swap (a handler for an
        // old frame would push the new surface to clients before the first new-size frame landed):
        // wait until the last frame has been announced before touching anything.
        waitForAnnouncements(std::chrono::seconds(2));
        releaseSurface();

        const bool bgra = format == wgpu::TextureFormat::BGRA8Unorm;
        const OSType pixelFormat = bgra ? kCVPixelFormatType_32BGRA : kCVPixelFormatType_32RGBA;
        NSDictionary* attributes = @{
            (__bridge NSString*)kIOSurfaceWidth: @(w),
            (__bridge NSString*)kIOSurfaceHeight: @(h),
            (__bridge NSString*)kIOSurfaceBytesPerElement: @(4U),
            (__bridge NSString*)kIOSurfacePixelFormat: @(pixelFormat),
        };
        surface_ = IOSurfaceCreate((__bridge CFDictionaryRef)attributes);
        if (surface_ == nullptr) {
            return fail("IOSurfaceCreate {}x{} failed", w, h);
        }

        wgpu::SharedTextureMemoryIOSurfaceDescriptor ioDesc{};
        ioDesc.ioSurface = surface_;
        ioDesc.allowStorageBinding = false;
        wgpu::SharedTextureMemoryDescriptor memDesc{};
        memDesc.nextInChain = &ioDesc;
        memDesc.label = "syphon-iosurface";
        memory_ = context_->device().ImportSharedTextureMemory(&memDesc);
        wgpu::SharedTextureMemoryProperties props{};
        if (!memory_ || memory_.GetProperties(&props) != wgpu::Status::Success) {
            releaseSurface();
            return fail("Dawn could not import the IOSurface as a shared texture");
        }
        if (props.format != format || props.size.width != w || props.size.height != h ||
            !(props.usage & wgpu::TextureUsage::CopyDst)) {
            releaseSurface();
            return fail("imported IOSurface properties do not match ({}x{})", props.size.width, props.size.height);
        }
        wgpu::TextureDescriptor texDesc{};
        texDesc.label = "syphon-iosurface-texture";
        texDesc.size = {w, h, 1};
        texDesc.format = format;
        texDesc.usage = wgpu::TextureUsage::CopyDst;
        wrapped_ = memory_.CreateTexture(&texDesc);
        if (!wrapped_) {
            releaseSurface();
            return fail("Dawn could not create a texture on the shared IOSurface");
        }

        MTLTextureDescriptor* mtlDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:bgra ? MTLPixelFormatBGRA8Unorm
                                                                          : MTLPixelFormatRGBA8Unorm
                                                               width:w
                                                              height:h
                                                           mipmapped:NO];
        mtlDesc.usage = MTLTextureUsageShaderRead;
        mtlDesc.storageMode = MTLStorageModeShared;
        surfaceTexture_ = [device_ newTextureWithDescriptor:mtlDesc iosurface:surface_ plane:0];
        if (surfaceTexture_ == nil) {
            releaseSurface();
            return fail("Metal could not wrap the IOSurface");
        }
        surfaceTexture_.label = @"avgen syphon source";
        width_ = w;
        height_ = h;
        format_ = format;
        // readDoneValue_ is not reset: MTLSharedEvent values must stay monotonic, and waiting on an
        // already-signalled value is free.
        log::debug("Syphon surface {}x{} {}", w, h, bgra ? "BGRA8" : "RGBA8");
        return {};
    }

    gpu::Context* context_ = nullptr;
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    SyphonMetalServer* server_ = nil;
    IOSurfaceRef surface_ = nullptr;
    id<MTLTexture> surfaceTexture_ = nil;
    wgpu::SharedTextureMemory memory_;
    wgpu::Texture wrapped_;
    id<MTLSharedEvent> readDone_ = nil;
    wgpu::SharedFence readDoneFence_;
    std::uint64_t readDoneValue_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    wgpu::TextureFormat format_ = wgpu::TextureFormat::Undefined;
    bool initialized_ = false;
    std::atomic<std::uint64_t> frames_{0};
    mutable std::mutex mutex_;
    std::condition_variable announcedChanged_;
    std::uint64_t announced_ = 0; // frames whose command buffer completed (guarded by mutex_)
    std::string lastError_;
    id<MTLCommandBuffer> lastCommandBuffer_ = nil;
};

} // namespace

bool syphonAvailable() { return true; }

std::string syphonDescribe() { return "available"; }

std::unique_ptr<ShareBackend> createSyphonBackend() { return std::make_unique<SyphonBackend>(); }

} // namespace avgen::share

#endif // __APPLE__
