#pragma once

// The one hook the 3D renderer offers the 2D composition (ADR-081).
//
// SceneRenderer calls this once per frame, after the tone map has written the target and before
// the frame timeline is resolved. It is deliberately the whole of the coupling between the two
// systems: the renderer knows that something may draw over the finished picture, and nothing more.
// Whatever draws there knows nothing about the scene, which is what will let the base image become
// a video, a still or another composition without either side changing.

#include "gpu/render_target.hpp"

#include <webgpu/webgpu_cpp.h>

namespace avgen::rendering {

class FrameOverlay {
public:
    virtual ~FrameOverlay() = default;
    // Appends passes that load `target` and blend into it. Must not clear it.
    virtual void encodeOverlay(wgpu::CommandEncoder& encoder, const gpu::TargetView& target) = 0;
};

} // namespace avgen::rendering
