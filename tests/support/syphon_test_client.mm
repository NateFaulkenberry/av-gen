#ifdef __APPLE__

#include "support/syphon_test_client.hpp"

#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <Syphon/SyphonMetalClient.h>
#import <Syphon/SyphonServerDirectory.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace avgen::testsupport {

namespace {

void pumpRunLoop(double seconds) { CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, true); }

NSDictionary* findServer(const std::string& name) {
    NSString* wanted = [NSString stringWithUTF8String:name.c_str()];
    NSArray* matches = [[SyphonServerDirectory sharedDirectory] serversMatchingName:wanted appName:nil];
    return matches.count > 0 ? matches.lastObject : nil;
}

} // namespace

struct SyphonTestClient::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    SyphonMetalClient* client = nil;
    std::shared_ptr<std::atomic<std::uint64_t>> received = std::make_shared<std::atomic<std::uint64_t>>(0);
    std::uint64_t consumed = 0;
};

SyphonTestClient::SyphonTestClient(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SyphonTestClient::~SyphonTestClient() {
    if (impl_ && impl_->client != nil) {
        [impl_->client stop];
        impl_->client = nil;
    }
}

std::unique_ptr<SyphonTestClient> SyphonTestClient::connect(const std::string& serverName,
                                                            std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    NSDictionary* description = nil;
    while ((description = findServer(serverName)) == nil && std::chrono::steady_clock::now() < deadline) {
        pumpRunLoop(0.02);
    }
    if (description == nil) {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->device = MTLCreateSystemDefaultDevice();
    impl->queue = [impl->device newCommandQueue];
    auto counter = impl->received;
    impl->client = [[SyphonMetalClient alloc] initWithServerDescription:description
                                                                 device:impl->device
                                                                options:nil
                                                        newFrameHandler:^(SyphonMetalClient*) {
                                                            counter->fetch_add(1, std::memory_order_relaxed);
                                                        }];
    if (impl->client == nil || !impl->client.isValid) {
        return nullptr;
    }
    return std::unique_ptr<SyphonTestClient>(new SyphonTestClient(std::move(impl)));
}

bool SyphonTestClient::waitForFrame(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (impl_->received->load(std::memory_order_relaxed) <= impl_->consumed) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        pumpRunLoop(0.005);
    }
    return true;
}

std::optional<gpu::Image8> SyphonTestClient::readFrame() {
    @autoreleasepool {
        impl_->consumed = impl_->received->load(std::memory_order_relaxed);
        id<MTLTexture> texture = [impl_->client newFrameImage];
        if (texture == nil || texture.width == 0 || texture.height == 0) {
            // Syphon's notification and texture publication are separate main-run-loop events
            // under a coalesced latest-wins burst. A caller can observe the notification first;
            // leave consumed advanced and let its wait/retry loop observe the next notification.
            return std::nullopt;
        }
        const auto width = static_cast<std::uint32_t>(texture.width);
        const auto height = static_cast<std::uint32_t>(texture.height);
        const NSUInteger bytesPerRow = static_cast<NSUInteger>(width) * 4;
        id<MTLBuffer> staging = [impl_->device newBufferWithLength:bytesPerRow * height
                                                           options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> commands = [impl_->queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:texture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(width, height, 1)
                        toBuffer:staging
               destinationOffset:0
          destinationBytesPerRow:bytesPerRow
        destinationBytesPerImage:bytesPerRow * height];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];

        gpu::Image8 image;
        image.width = width;
        image.height = height;
        image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
        const auto* src = static_cast<const std::uint8_t*>(staging.contents);
        const bool bgra = texture.pixelFormat == MTLPixelFormatBGRA8Unorm;
        for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
            image.rgba[i + 0] = src[i + (bgra ? 2 : 0)];
            image.rgba[i + 1] = src[i + 1];
            image.rgba[i + 2] = src[i + (bgra ? 0 : 2)];
            image.rgba[i + 3] = src[i + 3];
        }
        return image;
    }
}

std::uint64_t SyphonTestClient::framesReceived() const { return impl_->received->load(std::memory_order_relaxed); }

} // namespace avgen::testsupport

#endif // __APPLE__
