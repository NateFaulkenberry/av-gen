#pragma once

// RepresentationSelector (renderer upgrade, Deliverable 5 §5.4; ADR-123, ADR-124, ADR-125).
//
// Which *geometry* a drawable is drawn as this frame: the full mesh, a rung of its LOD ladder, a
// merged proxy, a billboard, or nothing. It reads an ImportanceRecord and a policy and returns a
// choice. It owns exactly one piece of state -- last frame's choice, for hysteresis -- and that
// state is what the oscillation test exists to exercise.
//
// ---- the rule, and why it is not a distance ladder --------------------------------------------
//
// §5.4 sketched five bands of projected radius (>200 px full mesh, 40-200 mesh LOD, 8-40 proxy,
// 2-8 impostor, <2 cull) and said plainly that the thresholds were placeholders to be calibrated
// against measurement. The measurement (§4.5) calibrates them, and in doing so says the middle of
// that ladder is asking the wrong question.
//
// What the sweep measured is that cost tracks **pixels per triangle**, with a minimum near 500 and
// a knee at the 2x2-quad threshold below it. Projected radius does not determine that: a fern and a
// terrain chunk at the same radius have triangles four orders of magnitude apart. So this selector
// splits the decision in two, by what each half can actually answer:
//
//   * **Which kind of representation** -- mesh, proxy, impostor, cull -- is a question about what a
//     thing can still *look* like at that size, and projected radius is the right measure of that.
//     A 3 px object cannot show silhouette detail whatever its tessellation.
//   * **Which rung of the mesh ladder** is a question about cost, and pixels per triangle is the
//     only measure §4.5 supports. So the ">200 px = LOD0" band is not a second threshold here: LOD0
//     is simply what the px/triangle rule returns for anything large, and two thresholds deciding
//     one thing is how they drift apart.
//
// ---- the band, not the minimum -----------------------------------------------------------------
//
// The sweep's caveat is the load-bearing part and it is easy to optimise straight past. Two
// screen-filling triangles cost 3.02 ms; 2,048 cost 1.57 ms. **Fewer triangles is not monotonically
// better**, so a selector written as "reach the fewest triangles you can get away with" is wrong by
// measurement, not by taste. The rule below picks the *coarsest rung that is still at or under the
// target*, which structurally cannot overshoot past the cheap band into the two-triangle regime,
// and falls back to LOD0 -- the finest -- when every rung is already coarser than the target.
// ADR-124 records the calibration and what is and is not measured about it.
//
// ---- hysteresis, and the promise it trades against ---------------------------------------------
//
// Hysteresis is a dead zone around every threshold: it takes a larger metric to come back than it
// took to leave, so a drawable sitting on a threshold keeps its choice instead of strobing as the
// camera breathes. It is the same rule shaders/cull.wgsl applies to scattered instances (ADR-082),
// deliberately so -- an entity and a scattered copy of the same asset must not disagree.
//
// It is **off by default**, in every tier, and ADR-082's reason is why: it reads the previous
// frame, so with it on what you see depends on how the camera arrived and not only on where it is.
// This engine promises frame-independence, and an offline render must not inherit a realtime
// compromise (§5.9), so `QualityTier::Offline` forces it to zero and forces every drawable to its
// top representation. Turning it on is an explicit act.

#include "rendering/importance.hpp"
#include "rendering/render_quality.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace avgen::rendering {

enum class Representation : std::uint8_t {
    FullMesh,  // LOD 0, the mesh as authored
    MeshLod,   // a rung of the existing ladder; `lodLevel` says which
    HlodProxy, // a merged proxy standing in for a group (not built yet -- Phase C, later)
    Impostor,  // a billboard (not built yet)
    Culled,    // not drawn
};

[[nodiscard]] constexpr const char* representationName(Representation r) {
    switch (r) {
    case Representation::FullMesh: return "full";
    case Representation::MeshLod: return "lod";
    case Representation::HlodProxy: return "proxy";
    case Representation::Impostor: return "impostor";
    case Representation::Culled: return "culled";
    }
    return "full";
}

struct RepresentationChoice {
    Representation kind = Representation::FullMesh;
    std::uint8_t lodLevel = 0; // meaningful for MeshLod; always 0 for FullMesh
    // Differs from the choice this drawable had last frame. A transition mechanism -- cross-fade,
    // dither, whatever the open C2 question settles on -- needs to know *when* a change happened
    // and this is where it reads it. The selector deliberately does not decide how to blend: it has
    // no temporal machinery and inventing one here would pre-empt a scope decision.
    bool changed = false;
    // The dead zone kept this drawable where it was; without hysteresis it would have moved. Zero
    // whenever `policy.hysteresis` is zero, which makes "is hysteresis doing anything" observable
    // rather than inferred.
    bool held = false;

    [[nodiscard]] bool sameAs(const RepresentationChoice& other) const {
        return kind == other.kind && lodLevel == other.lodLevel;
    }
};

// One rung of an object's ladder, as the selector needs to cost it.
struct LodRung {
    float surfaceArea = 0.0f;    // world units^2 at the instance's scale
    std::uint32_t triangles = 0;
};

struct RepresentationPolicy {
    // The rollback §6 asks the tests to assert: everything is drawn as its full mesh, exactly as
    // the renderer does today.
    bool enabled = true;
    // Offline (§5.9): the top representation for every object whatever its screen size, and no
    // history. Set from the quality tier; never from a scene file.
    bool forceTopRepresentation = false;

    // ---- kind bands, in projected radius (px) --------------------------------------------------
    // Calibrated in ADR-124. These are §5.4's bands with the mesh/LOD split removed, because the
    // px/triangle rule below decides that better than a radius can.
    float proxyRadius = 40.0f;    // at or below: a merged proxy will do
    float impostorRadius = 8.0f;  // at or below: a billboard will do
    float cullRadius = 2.0f;      // at or below: not worth drawing

    // ---- the pixels-per-triangle band (§4.5, ADR-124) -------------------------------------------
    // `target` is the measured cheapest point of the sweep. `floor` is the quad knee: below it cost
    // climbs steeply and a rung that lands there is the thing this system exists to remove.
    // `ceiling` is advisory and is NOT a threshold anything crosses -- see ADR-124 for why the
    // upper end of the band is unmeasured, and why no selector can act on it anyway.
    float targetPixelsPerTriangle = 500.0f;
    float floorPixelsPerTriangle = 8.0f;
    float ceilingPixelsPerTriangle = 50000.0f;

    // Dead zone around every threshold, as a fraction of it. Off by default (see the header).
    float hysteresis = 0.0f;
    // Per-object threshold offset, as a fraction, from a hash of the index (ADR-082's `lodSpread`).
    // Zero for entities by default: an authored object is one thing, not a population, and there is
    // nothing for it to decorrelate against. A caller selecting for a scatter sets it.
    float spread = 0.0f;

    // A hero (ADR-104) is never demoted past this. The default keeps heroes on real geometry: a
    // billboard of the subject of the shot is the kind of saving that costs the shot.
    Representation heroFloor = Representation::MeshLod;

    [[nodiscard]] static RepresentationPolicy forTier(QualityTier tier);
};

class RepresentationSelector {
public:
    // One drawable. `rungs` is its ladder, finest first; an empty ladder means "one rung, whatever
    // the record measured", which is the case for an object with no LOD chain.
    //
    // `previous` is read from this selector's own memory, keyed on `record.index`. A caller whose
    // indices moved -- a new scene, a rebuilt entity list -- must `reset()` first: a previous choice
    // read against a different object is the derived-copy defect this engine has nine of.
    [[nodiscard]] RepresentationChoice select(const ImportanceRecord& record,
                                              std::span<const LodRung> rungs,
                                              const RepresentationPolicy& policy);

    // Everything the selector remembers. Call on any change that renumbers drawables.
    void reset();
    [[nodiscard]] std::size_t remembered() const { return previous_.size(); }
    // Last frame's choice for `index`, or a default FullMesh choice if there is none.
    [[nodiscard]] RepresentationChoice previous(std::uint32_t index) const;

    // The pure decision, with the previous choice passed in explicitly. `select` is this plus the
    // memory; this is what a test that wants to state both sides of a transition calls.
    [[nodiscard]] static RepresentationChoice decide(const ImportanceRecord& record,
                                                     std::span<const LodRung> rungs,
                                                     const RepresentationPolicy& policy,
                                                     const RepresentationChoice& previous);

private:
    std::vector<RepresentationChoice> previous_;
    std::vector<bool> seen_;
};

} // namespace avgen::rendering
