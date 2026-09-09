#pragma once

// Texture sharing to other applications (milestone 1.2): Syphon on macOS (native, through Dawn's
// IOSurface interop and the Syphon framework) and NDI (runtime-loaded libndi, GPU readback
// ring). One TextureShare is one output; publish() is called once per frame after the frame's
// command buffer was submitted and never blocks the caller for more than about a millisecond
// in the steady state. See docs/rendering.md "Sharing".

#include "core/error.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <string>

namespace avgen::gpu {
class Context;
}

namespace avgen::share {

enum class ShareKind { Syphon, Ndi };

struct ShareStats {
    std::uint64_t framesPublished = 0; // frames handed to the share pipeline since open()
    std::uint32_t width = 0, height = 0;
    std::string lastError;             // most recent publish() failure, empty when healthy
    bool connected = false;            // at least one client attached
    int clients = -1;                  // client count when the transport knows it (NDI), else -1
};

// Internal transport interface (share/*_share.*); one per ShareKind.
class ShareBackend;

class TextureShare {
public:
    TextureShare();
    ~TextureShare();
    TextureShare(const TextureShare&) = delete;
    TextureShare& operator=(const TextureShare&) = delete;

    // Syphon: any macOS build. NDI: the runtime library was found and initialised.
    static bool available(ShareKind kind);
    // e.g. "Syphon: available; NDI: libndi not found (set AVGEN_NDI_LIB or install the NDI Runtime)"
    static std::string describe();
    static const char* kindName(ShareKind kind);

    Result<void> open(ShareKind kind, gpu::Context& context, const std::string& name);
    void close();
    [[nodiscard]] bool isOpen() const { return backend_ != nullptr; }
    [[nodiscard]] ShareKind kind() const { return kind_; }
    [[nodiscard]] const std::string& name() const { return name_; }

    // Publishes `source` (RGBA8Unorm or BGRA8Unorm, usage includes CopySrc; `w`x`h`). Called once
    // per frame after the frame's command buffer was submitted. Must not block the caller for
    // more than ~1 ms on the steady state (async where needed).
    Result<void> publish(const wgpu::Texture& source, std::uint32_t w, std::uint32_t h);

    // Nominal frame rate advertised to transports that carry one (NDI). Default 60/1.
    void setFrameRate(int numerator, int denominator);

    [[nodiscard]] ShareStats stats() const;

private:
    std::unique_ptr<ShareBackend> backend_;
    ShareKind kind_ = ShareKind::Syphon;
    std::string name_;
    int frameRateN_ = 60;
    int frameRateD_ = 1;
};

} // namespace avgen::share
