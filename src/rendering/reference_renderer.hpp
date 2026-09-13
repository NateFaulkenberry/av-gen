#pragma once

// A minimal renderer to compare the production one against (renderer forensics Phase 2.2).
//
// **What it is for.** When a symptom cannot be attributed -- an object that appears to move, a shape
// in the wrong place -- the question is whether the geometry is where the scene says it is. This
// path answers it by drawing the same scene with everything else removed: authoritative world
// transform, explicit view and projection, depth test, draw. No culling, no LOD, no skinning, no
// water, no transparency, no particles, no shadows, no post, no temporal history, no instancing,
// no batching. Nothing it does not have can explain a difference.
//
// **What it is not.** It is not a fallback, not a quality tier and not a preview. It draws flat
// shaded colour and will never look like the picture. The comparison worth making against it is
// *coverage* -- which pixels contain geometry -- not colour, and the production side of that
// comparison should have its isolation arms switched off (Phase 4.2) so the two are asking the same
// question.
//
// Objects draw in scene order with no sorting, so the order is a property of the scene rather than
// of a frame; the depth test resolves the rest.

#include "core/error.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "scene/scene.hpp"

#include <vector>

namespace avgen::rendering {

class ReferenceRenderer {
public:
    ReferenceRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~ReferenceRenderer();
    ReferenceRenderer(const ReferenceRenderer&) = delete;
    ReferenceRenderer& operator=(const ReferenceRenderer&) = delete;

    [[nodiscard]] Result<void> init();
    // Draws the scene's opaque mesh entities and reads the result back. Deterministic: the same
    // scene and size produce the same bytes, every time, with no state carried between calls.
    [[nodiscard]] Result<gpu::Image8> renderToImage(const scene::Scene& scene, std::uint32_t width,
                                                    std::uint32_t height);

    // How many entities the last call drew, and how many it declined. Declining is the honest half:
    // a scene of water and particles renders as nothing here and the counts say so, rather than the
    // caller comparing against an empty frame and concluding the transforms agree.
    struct Counts {
        std::size_t drawn = 0;
        std::size_t skippedSkinned = 0;
        std::size_t skippedBlended = 0;
        std::size_t skippedWater = 0;
        std::size_t skippedInvisible = 0;
        std::size_t skippedNoMesh = 0;
    };
    [[nodiscard]] const Counts& counts() const { return counts_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Counts counts_;
};

} // namespace avgen::rendering
