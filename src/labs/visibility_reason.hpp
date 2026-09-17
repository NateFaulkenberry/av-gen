#pragma once

// §9's visibility reason codes, as a vocabulary (ADR-260).
//
// **This header is the words, not the work.** The Visibility Lab owns wiring these to the real cull
// stages; what is settled here is which reasons may exist at all, because a vocabulary invented
// twice is two vocabularies. Every entry below names the line of code that already makes the
// decision, and `status` says whether the engine reports it today or whether reporting it is work
// somebody still has to do. A reason with no decision behind it is not in the list:
//
//   * **There is no occlusion culling in this engine.** No HiZ, no depth pyramid, no occlusion
//     query, no two-phase cull -- `docs/renderer-2-architecture.md` says so and grep agrees; the
//     only "occlusion" in the tree is ambient. The spec asked for `OCCLUSION_CULLED`; adding it
//     would produce a reason that can never be returned, which is a diagnostic that cannot fail.
//   * **`SHADOW_ONLY` is not a rejection.** An entity the camera culled is offered to the shadow
//     passes anyway (`scene_renderer.cpp`'s second caster pass), so "camera-culled" and "not a
//     shadow caster" are two independent answers and the vocabulary keeps them apart rather than
//     collapsing them into one code that means different things depending on which pass asked.
//
// Today's reality, which this vocabulary must remain able to describe: `RenderObjectDiagnostic`
// carries a free-form `std::string cullReason` that takes exactly five values -- `invalid-mesh`,
// `hidden`, `camera-frustum`, `eligible`, `submitted` -- and the GPU instance cull in
// `shaders/cull.wgsl` carries none at all, collapsing six planes, a distance test, a screen-radius
// test and a depth-band thinning into one `culled` bool. `fromCullReason` maps the five, so an enum
// and the string the renderer actually wrote cannot drift apart while the wiring is in progress.

#include <optional>
#include <span>
#include <string_view>

namespace avgen::labs {

enum class VisibilityReason {
    // Reached a draw call. `cullReason == "submitted"`.
    Visible,
    // Survived every test but the frame had not yet recorded a draw when the diagnostic was built.
    // `cullReason == "eligible"`. Not a rejection; it is the state between the cull and the draw,
    // and a diagnostic that reported it as VISIBLE would be lying about a real gap.
    Eligible,
    // `scene::Entity::visible == false`: the scene says do not draw this. `cullReason == "hidden"`.
    Disabled,
    // No mesh to draw. `cullReason == "invalid-mesh"`.
    InvalidMesh,
    // The bounds are not finite. `entityCullBounds` reports this rather than letting the object
    // vanish silently; the diagnostic's `finite` flag already carries it.
    InvalidBounds,
    // Outside the camera frustum. `cullReason == "camera-frustum"` for entities; the GPU instance
    // cull reaches the same conclusion and says nothing.
    FrustumCulled,
    // Beyond a distance limit: `LodSettings::maxDistance` for scatter instances,
    // `TerrainSettings::viewDistance` for terrain chunks, `EntityDesc::cullDistance` for entities.
    DistanceCulled,
    // Projects to fewer pixels than `LodSettings::minScreenRadius`. A distinct decision from
    // DistanceCulled: it moves with the field of view and the viewport, and a diagnostic that
    // reported both as "too far" would send somebody to the wrong knob.
    ScreenSizeCulled,
    // Thinned out by its depth band (ADR-038): the instance is in range and in frame, and the band
    // it landed in keeps only a fraction of its instances.
    DepthBandThinned,
    // The LOD ladder ran off its end -- no level remains for this instance.
    LodRejected,
    // Not a shadow caster, asked of a shadow view. Independent of every code above.
    NotAShadowCaster,
    // Outside this shadow view's frustum, though inside the camera's.
    ShadowFrustumCulled,
    // The pass that would have drawn it is switched off -- `--disable`, or a `PassToggles` flag.
    // An arm, not a defect, and the code exists so a frame taken under an arm cannot be read as a
    // frame with a bug in it.
    PassDisabled,
};

enum class ReasonStatus {
    // The engine reports this today, and `fromCullReason` or a diagnostic flag recovers it.
    Reported,
    // The engine *makes* this decision and does not report it. This is the Visibility Lab's work
    // list, and each entry names where the decision is made.
    Unreported,
};

struct ReasonInfo {
    VisibilityReason reason;
    // The uppercase code, as §9 writes them.
    std::string_view code;
    ReasonStatus status;
    // Where the decision is actually made, `path:symbol`.
    std::string_view decidedAt;
    // The `cullReason` string the renderer writes today, or empty when it writes none.
    std::string_view cullReason;
};

[[nodiscard]] std::span<const ReasonInfo> visibilityReasons();
[[nodiscard]] const ReasonInfo& reasonInfo(VisibilityReason reason);
[[nodiscard]] std::string_view reasonCode(VisibilityReason reason);
[[nodiscard]] std::optional<VisibilityReason> reasonFromCode(std::string_view code);
// The five strings `SceneRenderer` writes today, mapped. Empty for anything else -- including the
// empty string, which means the renderer never reached that object, and is not `VISIBLE`.
[[nodiscard]] std::optional<VisibilityReason> fromCullReason(std::string_view cullReason);

} // namespace avgen::labs
