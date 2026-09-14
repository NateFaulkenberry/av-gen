#pragma once

// The hero mushroom generator (Glowmere Valley 2, Phase 4).
//
// A mushroom is four swept surfaces sharing one parameter vector: an upper cap, an underside, a
// stem, and a ring of gill blades between the two cap surfaces. Each is a separate part, so each
// becomes its own named scene node with its own material -- which is what makes a cap that glows
// differently from its gills expressible at all, and what makes both selectable in the editor.
//
// Three ideas are taken from outside and each is credited where it is used:
//
//   * **The cap is a radius profile, not a primitive.** `docs/visual-cookbook/fungi.md` -- a tube
//     swept with a per-control-point scale is a lathe of any profile, and a cap is widest at the rim
//     and curves over to the apex, which a cone cannot be.
//   * **The identity lives in the rim tangent.** From proc-shrooms (Blender, source read): the cap
//     is two cubic Hermite profiles sharing a rim, and the free parameters are the *tangent angles*
//     at the centre and at the rim. That is why four numbers can produce a flat parasol, a bell, a
//     cone and a recurved chanterelle. Control points can express those shapes; they cannot be
//     *asked* for "twenty degrees more rim droop".
//   * **The profile is swept about a curved axis.** Desbenoit et al., *Interactive Modeling of
//     Mushrooms* (EG 2004): one silhouette rotated about a 2D spline rather than a straight line,
//     then deformed. A bent stem for free.
//
// And one law, from `docs/visual-cookbook/bioluminescence.md`, which governs the emissive design
// rather than the geometry: **give the light a structure to come out of.** A glowing surface reads
// as paint; a glowing structure reads as biology. The same document records the trap -- gills were
// added once and were invisible, because the geometry above enclosed them -- which is why the
// underside is built as its own part and why the search renders a low-angle view.
//
// What the engine could not do, and this does not ask it to: `scene::makeTube` sweeps a circular
// cross-section only, so no engine primitive can break a cap's radial symmetry. That is the defect
// visible in the original elder, whose cap is a perfect ellipse. Rather than widen the shared sweep
// -- which the original Glowmere's own hero goes through -- the modulation lives here, in the
// generator, applied per (angle, arclength) as it lathes.

#include "core/error.hpp"
#include "scene/procedural.hpp"
#include "scene/scene_types.hpp"
#include "search/candidate_search.hpp"

#include <cstdint>
#include <vector>

namespace avgen::organism {

// The parts, in the order `scene::SourceSpec::generatedPart` names them.
enum class MushroomPart : int { CapUpper = 0, CapUnder = 1, Stem = 2, Gills = 3 };
constexpr int kMushroomParts = 4;

// The parameter schema, ordered by visual influence because Sobol's low dimensions are its good ones
// (ADR-173). Aspect and rim tangent are axes 0 and 1 for that reason; surface noise is at the end.
[[nodiscard]] const search::GeneratorSchema& mushroomSchema();

// Named accessors into the parameter vector, so the generator body reads as mushroom anatomy rather
// than as `p[7]`. Index-based rather than name-lookup because this runs a few hundred times per
// search and the schema order is fixed.
enum class MushroomParam : std::size_t {
    Aspect = 0,          // cap radius / stem height
    RimTangentDeg,       // the identity parameter: parasol vs bell vs cone vs chanterelle
    CentreTangentDeg,    // domed vs flat vs depressed centre
    CapThickness,        // of cap radius
    StemCurvature,       // a straight stem reads as a cylinder
    StemTaper,           // < 1 narrows upward, > 1 flares
    StemBulgePosition,
    StemBulgeWidth,
    LobeCount,           // 0 = a circular rim
    LobeDepth,           // the radial asymmetry no engine primitive can express
    CapTiltDeg,
    EdgeWaviness,        // a higher harmonic on the same per-angle term
    SurfaceNoiseAmp,
    SurfaceNoiseScale,
    GillCount,
    GillDepth,           // of cap thickness
    EmissionStructure,   // 0 gills, 1 rim, 2 veins, 3 filaments -- *where* the light comes out
    EmissionIntensity,
    Count
};

[[nodiscard]] inline float param(const std::vector<float>& v, MushroomParam p) {
    const auto i = static_cast<std::size_t>(p);
    return i < v.size() ? v[i] : 0.0f;
}

// One part's mesh, in a unit frame: the stem's base is at the origin and its height is 1, so the
// scene's own transform decides how big the mushroom is. Deterministic in the values alone.
[[nodiscard]] Result<scene::MeshData> buildMushroomPart(const scene::GeneratedSource& source, int part);

// The whole organism, for the search's scorer and for a preview.
[[nodiscard]] Result<search::Subject> buildMushroom(const search::Parameters& values);

// Registers `buildMushroomPart` under "mushroom" in the scene's generator registry (ADR-175). Safe
// to call more than once.
void registerMushroomGenerator();

// ---- the search generator (the four members of `search::CandidateGenerator`) -------------------
class MushroomGenerator {
public:
    [[nodiscard]] const search::GeneratorSchema& schema() const { return mushroomSchema(); }
    [[nodiscard]] Result<search::Subject> build(const search::Parameters& values) const {
        return buildMushroom(values);
    }
    [[nodiscard]] search::FeatureVector features(const search::Subject& subject,
                                                 const search::Parameters& values) const;
    [[nodiscard]] std::vector<search::ScoreComponent> domainScores(const search::Subject& subject,
                                                                   const search::Parameters& values) const;
};

// Where the parts of a mushroom are supposed to meet, **derived from the generated meshes rather
// than from the parameters**.
//
// That distinction is the whole point. A parameter-space check agrees with itself: it would recompute
// the attachment from `aspect` and `capThickness` and find it exactly where the builder put it, and a
// transform applied to the wrong pivot would sail straight past. Reading the anchors back off the
// vertices is what makes the check able to catch a bug in the transform -- which is not hypothetical
// here, because "the cap tilted about the world origin and swung itself off the stem" was one of the
// five defects the first contact sheet caught.
//
// It is also the emitter position for spore-fall, which is the same quantity and should not be
// authored twice: if the anchor is right the spores leave from the right place on every mushroom the
// generator can produce, and if it is wrong the alignment check and the spores say so together.
struct MushroomAnchors {
    glm::vec3 stemTop{0.0f};          // centroid of the stem's topmost ring
    float stemTopRadius = 0.0f;       // that ring's spread, which sets the tolerance
    glm::vec3 capAttach{0.0f};        // centroid of the cap underside's innermost ring
    glm::vec3 gillLow{0.0f};          // the lowest point of the gill set: where spores fall from
    float gillRadius = 0.0f;          // how far the gills reach out: how wide spores fall from
    bool valid = false;
};

[[nodiscard]] MushroomAnchors mushroomAnchors(const search::Subject& subject);

// What is wrong with a mushroom's assembly, in metres. Empty means nothing is.
//
// Checked across a whole candidate population rather than the selected few: "the six we shipped line
// up" is a much weaker statement than "every mushroom this generator can produce lines up", and
// ADR-180 is about exactly that difference -- a search finds only what its parameterisation
// expresses, and an invariant that has only been checked on the winners has only been checked on the
// region the scorer liked.
struct AlignmentDefect {
    std::string rule;
    float metres = 0.0f;
    std::string detail;
};

[[nodiscard]] std::vector<AlignmentDefect> checkMushroomAlignment(const search::Subject& subject);

// Organic plausibility, as distinct from mesh hygiene (which `search::meshHygiene` does). The rule
// is that this rejects things that are *incoherent*, never things that are merely strange: a cap
// lobed into six drooping petals passes, a cap floating half a metre above a stem it never touches
// does not.
[[nodiscard]] std::optional<search::Rejection> mushroomPlausibility(const search::Subject& subject,
                                                                    const search::Parameters& values);

} // namespace avgen::organism
