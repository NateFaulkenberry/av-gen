#pragma once

// In-process Syphon client for tests (macOS): discovers a server by name through the
// SyphonServerDirectory (pumping the main run loop, as any client application would), receives
// frames on a SyphonMetalClient and reads them back as RGBA8 through a Metal blit.

#include "gpu/readback.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace avgen::testsupport {

class SyphonTestClient {
public:
    // Waits up to `timeout` for a server with that name to appear and connects. nullptr on timeout.
    static std::unique_ptr<SyphonTestClient> connect(const std::string& serverName, std::chrono::milliseconds timeout);
    ~SyphonTestClient();

    // Waits for a frame newer than the last one returned by readFrame(). False on timeout.
    bool waitForFrame(std::chrono::milliseconds timeout);
    // Current frame as RGBA8 (swizzled from Syphon's BGRA8 surface). A notification can arrive
    // just before Syphon exposes the latest Metal texture during a coalesced burst; callers should
    // retry a null result rather than treating that transient as transport failure.
    std::optional<gpu::Image8> readFrame();
    [[nodiscard]] std::uint64_t framesReceived() const;

    struct Impl;

private:
    explicit SyphonTestClient(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace avgen::testsupport
