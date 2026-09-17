#pragma once

// Phase 4 of the Quality Lab: the AOV-gated masks (docs/quality-lab/artifact-detection.md §3).
//
// **One residual, four masks — not four detectors.** §9 of the mandate wants shadow, specular and
// detail as separate dimensions, and the honest way to produce separate numbers is to evaluate one
// well-understood measurement over regions the renderer itself can identify. Four algorithms
// measuring the same thing under four names is how a metric suite becomes unfalsifiable, which is
// the failure ADR-243 is the receipt for.
//
// So nothing in this file computes a quality number. Every function returns a **mask**: one byte
// per pixel, 1 meaning "this pixel is in the class". `MotionResidual::over(mask)` does the
// measuring, and it reports the mask's coverage beside every number it produces, because a residual
// over 0.4% of the frame is a statement about 0.4% of the frame.
//
// **What is deliberately absent: shadow.** There is no shadow AOV. ADR-242 lists it among the views
// not added because nothing had asked for one, and the Quality Lab is the first consumer with a
// reason to ask. Approximating it from "regions the lighting model says are shadowed" is refused —
// it would be a number whose name promised more than it knew.

#include "capture/sequence.hpp"

#include <cstdint>
#include <vector>

namespace avgen::quality {

using Mask = std::vector<std::uint8_t>;

// The fraction of a mask that is set. Every caller needs it and every report prints it.
[[nodiscard]] double coverage(const Mask& mask);

// ---- the identifier AOV, and the check that makes every id-based mask non-vacuous --------------

// What an identifier plane actually contains. ADR-242's own test records why this exists: without
// asserting that `id` has more than one distinct value, every mask-based row below is vacuous — a
// plane of one constant produces a mask of everything or a mask of nothing, and both look like a
// working detector.
struct IdentifierSurvey {
    std::size_t distinctValues = 0;
    double backgroundFraction = 0.0; // exactly 0, which is what no geometry writes
    // A float EXR represents integers exactly only to 2^24. The identifier packs a material id into
    // the high 16 bits of a 32-bit word, so a large enough material id is NOT exactly representable
    // and an equality test on it is approximate. True when that has actually happened here, so the
    // limitation is reported when it applies rather than always.
    bool exceedsExactFloatRange = false;
    [[nodiscard]] bool usable() const { return distinctValues > 1; }
};
[[nodiscard]] IdentifierSurvey surveyIdentifiers(const Plane& id);

// ---- the four masks ----------------------------------------------------------------------------

// **Specular.** Emission above a threshold, or roughness below one — roughness being the alpha of
// the decoded normal AOV (ADR-242 §3 writes rgb = unit normal, a = roughness).
//
// The caveat travels with the number and is not negotiable: **emission is not specular.** A rough
// emissive surface is in this mask and should not be. Roughness is the better half and it is stored
// at half precision. Either plane may be null; with both null the mask is empty and the caller
// reports the dimension unavailable rather than measuring the whole frame and calling it specular.
[[nodiscard]] Mask specularMask(const Plane* emission, const Plane* normal,
                                double emissionLuminanceThreshold = 0.25,
                                double maxRoughness = 0.3);

// **Shading versus geometry.** Pixels whose normal did *not* change between two frames. A normal
// that changed means geometry moved; a normal that did not while colour did means shading is
// unstable — which is a different defect with a different fix.
//
// `cosThreshold` is a decision and not a fact: a normal that changed by less than the half-float
// epsilon reads as unchanged, and where the threshold sits above that epsilon is a choice.
[[nodiscard]] Mask normalUnchangedMask(const Plane& previous, const Plane& current,
                                       double cosThreshold = 0.999);

// **LOD popping — reported as a candidate, never as a detection.** The identifier changed while the
// velocity says the surface did not move and the depth says it is the same surface. That is what a
// geometric level swapping under a stationary surface looks like; it is also what a genuine object
// change at a silhouette looks like, and nothing in these AOVs can separate them.
//
// Note the asymmetry with the disocclusion mask, which is deliberate: disocclusion asks whether the
// identifier changed *along the motion*, and this asks whether it changed *where there is no
// motion*. The second is a subset of the first, which is why an id churn pixel is also excluded
// from the residual — the churn count is the finding, not the residual over it.
[[nodiscard]] Mask identifierChurnMask(const Plane& idPrevious, const Plane& idCurrent,
                                       const Plane& velocity, const Plane* depthPrevious,
                                       const Plane* depthCurrent,
                                       double staticVelocityPixels = 0.25,
                                       double depthRelativeThreshold = 0.02);

// **Class membership.** Pixels whose identifier's material half is in `materialIds`.
//
// The mechanism exists; **the mapping does not.** "Vegetation" is a set of material ids nobody has
// written down, so `vegetationResidual` reports `available: false, reason: "no material-id to class
// mapping"` rather than guessing — see the report's limitations. This function is what that
// decision would be implemented with the day the mapping is authored, and it is tested against a
// synthetic plane so it is not dead code waiting to be wrong.
[[nodiscard]] Mask materialClassMask(const Plane& id, const std::vector<std::uint32_t>& materialIds);

// The material and object halves of a packed identifier, as `scene_types.hpp` defines them: the low
// 16 bits an object id (of which the top two are a `PickSpace` tag), the high 16 a material id.
[[nodiscard]] std::uint32_t materialIdOf(float packed);
[[nodiscard]] std::uint32_t objectIdOf(float packed);

// Every pixel. The control arm for every mask above: a gated residual over `everything()` must equal
// the ungated residual, or the gating machinery is doing something other than gating.
[[nodiscard]] Mask everything(std::uint32_t width, std::uint32_t height);

// ---- interior versus silhouette (ADR-257) ------------------------------------------------------

// Morphology over a mask, by a Chebyshev radius -- a (2r+1)x(2r+1) box, applied separably. Out of
// bounds counts as NOT set, so erosion clips an object touching the frame edge rather than
// pretending the frame continues.
[[nodiscard]] Mask erode(const Mask& mask, std::uint32_t width, std::uint32_t height, int radius);
[[nodiscard]] Mask dilate(const Mask& mask, std::uint32_t width, std::uint32_t height, int radius);

// One object, split into the part that is safely inside it and the band around its silhouette.
//
// **This exists because a scene called a shape a control and it was only half one** (ADR-257). A
// large smooth sphere's interior shading is unaffected by anti-aliasing; its silhouette is a curved
// edge like any other and is affected exactly as much. The two claims are only separable if the two
// regions are, so they are separated here and measured apart.
//
// `band` is every pixel within `radius` of the boundary, inside OR outside it, because a filter that
// works on an edge reads across it. `interior + band + elsewhere` is exactly the frame.
struct SilhouetteSplit {
    Mask object;    // the identifier's own pixels
    Mask interior;  // object, eroded by radius
    Mask band;      // dilate(object) minus erode(object)
    Mask elsewhere; // everything outside dilate(object)
};
[[nodiscard]] SilhouetteSplit splitSilhouette(const Plane& id, std::uint32_t objectId, int radius);

// Mean and max |luma difference| of two frames over a mask, in luma steps 0..255 -- the unit the
// rest of the Lab reports in, so these numbers sit beside the others without conversion. `pixels` is
// how many contributed, and a caller that does not print it is publishing a headline about an
// unknown number of pixels.
struct MaskedDifference {
    double mean = 0.0;
    double max = 0.0;
    std::size_t pixels = 0;
};
[[nodiscard]] MaskedDifference maskedLumaDifference(const Frame& a, const Frame& b,
                                                    const Mask& mask);

} // namespace avgen::quality
