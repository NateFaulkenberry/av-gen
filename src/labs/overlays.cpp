#include "labs/overlays.hpp"

namespace avgen::labs {

rendering::DebugViewOptions overlaysFor(LabId id) {
    rendering::DebugViewOptions o;
    switch (id) {
    case LabId::Animation:
        // The joints, and the entity's own frame to read them against. `skeletons` draws from the
        // rig's model-space matrices rather than the GPU palette, which is the distinction the
        // Animation Lab is about.
        o.skeletons = true;
        o.entityOrigins = true;
        break;
    case LabId::Character:
        // Where it is, where it has been, and how big the box the cull uses is. The navigation
        // overlays are the editor's (`WorldEditor::showNavRoute` and friends) and are not part of
        // `DebugViewOptions`, so this profile cannot turn them on and does not pretend to.
        o.entityOrigins = true;
        o.entityBounds = true;
        o.transformTrail = true;
        o.skeletons = true;
        break;
    case LabId::Visibility:
        // The three things the entity cull decides with: the box, the volume, and whether the frame
        // kept it. `submittedOnly` is the control -- with it off you see every candidate, with it
        // on only the survivors, and the difference is the cull.
        o.entityBounds = true;
        o.frustum = true;
        o.submittedOnly = true;
        o.worldAxes = true;
        break;
    case LabId::Lod:
        // The rung every instance was assigned, over the scatter clouds' bounds. `lod` reads the
        // cull pass's own `lodIndex` buffer now (it drew nothing at all when this profile was
        // written, which is why the header used to say it was deliberately off).
        o.lod = true;
        o.points = true;
        o.bounds = true;
        break;
    case LabId::Camera:
        // The frustum the cull is given, and the world origin to place it against. Drawn from
        // `scene.camera`, so it is only informative while the view is not that camera's.
        o.frustum = true;
        o.worldAxes = true;
        o.entityBounds = true;
        break;
    case LabId::Shadow:
        // The two halves of a cascade and the caster list. `shadowCascades` is the volume the
        // depth pass rasterises into; `shadowCascadeSlices` is the part of the camera frustum whose
        // pixels select it, which is the half that answers "why did the shadow change when I
        // dollied". Both are drawn from the views the renderer uploaded, not from a second fit.
        //
        // `entityBounds` is deliberately OFF here and `shadowCasters` on in its place: two boxes
        // per entity in two colour schemes is one box too many, and the caster colouring already
        // carries the bound.
        o.shadowCascades = true;
        o.shadowCascadeSlices = true;
        o.shadowCasters = true;
        o.frustum = true;
        break;
    case LabId::Lighting:
        // Where every light is, what shape the shading pass thinks its emitter is, and the sphere
        // of influence past which the froxel pass stops evaluating it. That last ring is the
        // reason this profile is not empty: it is a *hard* edge in the image, and before it was
        // drawn the only way to find it was to notice a line on the floor.
        //
        // `lightClusters` is deliberately OFF. It is 16 x 8 x 24 froxels over the whole frame and
        // it obscures exactly the pixels a lighting question is about; it is reachable from
        // `--debug-draw lightClusters` when the question is about the grid rather than the light.
        // `worldAxes` places the rest, because a light rig expresses positions relative to a
        // subject and the first question about a light that is in the wrong place is where zero is.
        o.lights = true;
        o.worldAxes = true;
        break;
    case LabId::Hdr:
        // Nothing, and it stays nothing. Every question this lab asks is about a VALUE at a pixel
        // -- what radiance arrived, what the bright pass kept, what the curve did with it -- and a
        // line drawn over that pixel is a value somebody then measures. The Rendering Lab draws
        // nothing because an overlay in a frame it grades becomes an artifact it reports; this lab
        // draws nothing for the sharper version of the same reason: its overlay would be *inside*
        // the measurement rather than beside it, and a debug line above the bloom threshold would
        // bloom.
        //
        // The instruments are elsewhere and they are not overlays: `--post-stages` writes every
        // intermediate the chain rendered (ADR-277), `--debug-target emission` shows the mask the
        // bright pass is weighted by, `--aov emission` exports it, and `--disable post` is the
        // boundary the Lighting Lab drew in code. See docs/hdr-lab/README.md.
        break;
    case LabId::Volumetric:
        // Nothing. Judged on the frame, not on geometry drawn over it; `--debug-target` and
        // `--aov` are its instruments.
        break;
    case LabId::Particle:
        o.points = true;
        o.bounds = true;
        break;
    case LabId::Temporal:
        // Trails: what moved between two frames, drawn from the recorded history rather than
        // inferred from the image.
        o.transformTrail = true;
        o.entityOrigins = true;
        break;
    case LabId::Aov:
        // The identifier target's own colouring, so a pick id in an EXR can be matched to the
        // object that wrote it.
        o.entityIds = true;
        o.entityBounds = true;
        break;
    case LabId::Rendering:
        // Nothing, and this is the load-bearing one: the Rendering Lab measures frames, and an
        // overlay in a frame it measures is a defect it would then report as an artifact.
        break;
    case LabId::Integration:
        o.worldAxes = true;
        break;
    case LabId::Interaction:
        // Nothing, and for the same reason the Rendering Lab draws nothing, one step earlier in the
        // chain: this lab measures how long a frame takes to appear, and an overlay is work added
        // to every frame it times. The Rendering Lab must not draw into a frame it grades; this one
        // must not draw into a frame it clocks.
        break;
    }
    return o;
}

} // namespace avgen::labs
