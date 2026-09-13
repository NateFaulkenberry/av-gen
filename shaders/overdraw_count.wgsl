// Opt-in overdraw / fragment-density counting pass (ADR-115).
//
// WGSL atomics exist only on storage buffers -- there is no 64-bit atomic and no texture atomic --
// so counting fragments per pixel means a storage buffer sized to the viewport with atomicAdd, read
// back by a view in aux_debug.wgsl. Apple documents that a fragment shader which writes to a storage
// buffer disables hidden-surface removal (early-Z), and WGSL has no early-fragment-tests attribute to
// opt back in. That is not a bug to work around here -- it is the mechanism the count depends on:
// every "surface" drawn through this pipeline is rasterised and shaded even when a nearer surface
// already covers the pixel, so the buffer ends up holding *submitted* fragments, not *visible* ones,
// which is the picture an overdraw diagnostic exists to show.
//
// Because of that disabled early-Z, this must never run on the normal frame path. It exists only as
// its own pass, encoded only while AuxDebugView::Overdraw or ::FragmentDensity is selected
// (scene_renderer.cpp), through its own three-group pipeline layout (frame, object, this buffer) so
// it shares no pipeline state with the real opaque pass. The depth attachment it binds is the real
// scene depth with `depthCompare = Always` and writes disabled: the attachment is there only because
// a render pass needs one, and Always means it rejects nothing.

#include "common.wgsl"

@group(2) @binding(0) var<storage, read_write> overdrawCounts: array<atomic<u32>>;

@fragment
fn fs_overdraw(in: VertexOut) {
    let width = u32(frame.targetSize.x + 0.5);
    let height = u32(frame.targetSize.y + 0.5);
    let x = u32(in.clip.x);
    let y = u32(in.clip.y);
    if (x >= width || y >= height) {
        return;
    }
    atomicAdd(&overdrawCounts[y * width + x], 1u);
}
