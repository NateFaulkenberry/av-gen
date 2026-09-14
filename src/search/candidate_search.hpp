#pragma once

// Candidate search: generate many procedural individuals, reject the broken, score the rest, and
// curate a small diverse set (ADR-172, ADR-173).
//
// This layer knows nothing about what is being generated. It is shared between the Glowmere Valley 2
// hero mushrooms and the procedural Tree of Life, and anything that names a cap, a stem, a trunk or
// a leaf does not belong in this file. A generator supplies four things (`CandidateGenerator`); this
// layer supplies the sampler, the validity gate, the scoring harness, the diversity selection and
// the canonical record.
//
// The pipeline, and each stage's home:
//
//   GeneratorSchema ─▶ sampleAt()  ─▶ Generator::build() ─▶ meshHygiene() ─▶ score  ─▶ features
//     generator         here             generator            here          both       generator
//                                                                │
//                                                  Rejection, recorded with its reason
//                                                                ▼
//                                        selectDiverse() ─▶ contact sheet ─▶ a person chooses
//                                             here            gpu layer        not a function
//
// Two rules are load-bearing and are enforced by the types rather than by comment.
//
// **A score component is a band, never a maximum (ADR-172).** A selection stage that ranks by score
// *is* an optimiser over whatever the score measures, so a monotone component is an instruction to
// maximise it -- and maximised edge density, asymmetry and tessellation are three of the failure
// modes this pipeline exists to avoid. `ScoreBand` has no monotone form: it is four numbers and it
// falls off on both sides. Penalties are the deliberate exception and are a separate field, because
// there is no such thing as too little of an artifact.
//
// **What a generator emits must be editable (the world-editor contract).** A hero that an artist
// cannot click, inspect, nudge, key, undo and save is a mesh blob wearing a procedural label. So the
// unit a generator produces is not geometry: it is `GeneratedSource` -- an authored parameter vector
// with provenance -- which a scene stores, the editor edits, the project file round-trips, and the
// generator reads. There is deliberately no second serialisation format for "the winning candidate":
// `candidateToJson` emits that same block under "source". See `GeneratedSource` below for the one
// engine gap this needs, which is named rather than worked around.
//
// **A candidate's identity is its index (ADR-173).** `sampleAt(schema, i)` is a pure function, so a
// selected individual is reproducible from `(generatorName, generatorVersion, schemaHash, index)`
// and no mesh is ever stored. `schemaHash` is what keeps that honest: widen a parameter's range and
// the hash moves, so a stale record announces itself instead of silently regenerating a different
// individual under the same name.

#include "core/error.hpp"
#include "scene/procedural.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace avgen::search {

// ---------------------------------------------------------------------------------------------
// The parameter space

// One axis of the search. `integral` is sampled continuously and rounded, so an integer parameter
// still gets the sequence's stratification rather than a hash.
struct ParameterSpec {
    std::string name;
    float min = 0.0f;
    float max = 1.0f;
    bool integral = false;
    std::string note; // what it does visually. Written for the person reading a candidate record.
};

// An ordered list of axes. **The order is load-bearing and is not alphabetical.** A low-discrepancy
// sequence's low-dimensional projections are much better than its high-dimensional ones, so the
// most visually influential parameter must be index 0. Reordering a schema silently degrades the
// sample without changing any single parameter's range, which is why `hash()` covers the order.
class ParameterSchema {
public:
    ParameterSchema() = default;
    explicit ParameterSchema(std::vector<ParameterSpec> specs) : specs_(std::move(specs)) {}

    [[nodiscard]] std::size_t size() const { return specs_.size(); }
    [[nodiscard]] const ParameterSpec& at(std::size_t i) const { return specs_[i]; }
    [[nodiscard]] std::span<const ParameterSpec> specs() const { return specs_; }
    [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view name) const;
    [[nodiscard]] Result<void> validate() const;

private:
    std::vector<ParameterSpec> specs_;
};

// Values parallel to the schema's order. A bare vector rather than a map: the pairing with the
// schema is the point, and a map would let a caller lose the ordering the sampler depends on.
using Parameters = std::vector<float>;

// Everything a generator declares about itself. One type so that `hash()` covers all of it: a
// record that names a schema hash has pinned the parameter ranges, their order, the feature axes
// and the generator's version together.
struct GeneratorSchema {
    std::string generatorName;             // "mushroom", "tree" -- for the record, not for dispatch
    std::uint32_t generatorVersion = 1;    // bump when `build` changes what a parameter set means
    ParameterSchema parameters;
    std::vector<std::string> featureNames; // the diversity axes, parallel to FeatureVector
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t hash() const;
};

// ---------------------------------------------------------------------------------------------
// Sampling (ADR-173)

// The parameters of candidate `index`, from a scrambled Sobol sequence mapped through each spec's
// range. Pure: the same (schema, index) always gives the same parameters, and any prefix of the
// sequence is well-distributed, so a population can be grown by appending indices without
// invalidating the identity of anything already chosen.
[[nodiscard]] Parameters sampleAt(const ParameterSchema& schema, std::uint32_t index);

// ---------------------------------------------------------------------------------------------
// What a generator produces

// One generated individual, as geometry the generic scorers can measure. Parts are separate so that
// per-region material scoring and per-region triangle budgets are expressible; a generator with one
// material emits one part.
struct SubjectPart {
    scene::MeshData mesh;
    glm::vec3 baseColor{0.5f};
    glm::vec3 emissiveColor{0.0f};
    float emissiveIntensity = 0.0f;
    float roughness = 0.6f;
    // Opaque here, and carried through to both the record and the emitted node's name, so the
    // editor shows "elder-2.cap" rather than "part 3". Must be unique within a Subject.
    std::string role;
};

struct Subject {
    // Each part becomes one named `scene::CompositionNode`, so it is selectable in the viewport and
    // its parameters live under a prefix the inspector can find (`WorldSelection::parameterPrefix`).
    // A generator that emits one merged mesh has made its own output uninspectable.
    std::vector<SubjectPart> parts;
    // The radius a preview camera should frame. A generator knows its own scale; the contact sheet
    // must not have to guess it from bounds that a stray vertex can ruin.
    float framingRadius = 1.0f;
    // Up-axis offset of the point a preview camera should look at, as a fraction of framingRadius.
    float framingCenterY = 0.5f;
};

// ---------------------------------------------------------------------------------------------
// Scoring (ADR-172)

// A trapezoid. 0 below `lowEdge` and above `highEdge`, 1 between the plateaus, smoothstepped
// between. There is deliberately no constructor that produces a monotone response: a band whose
// `highEdge` is at infinity is not expressible, because that is the shape this type exists to
// prevent.
struct ScoreBand {
    float lowEdge = 0.0f;
    float lowPlateau = 0.0f;
    float highPlateau = 1.0f;
    float highEdge = 1.0f;

    [[nodiscard]] Result<void> validate() const; // requires lowEdge <= lowPlateau <= highPlateau <= highEdge
    [[nodiscard]] float operator()(float x) const;
};

// One measured quantity, its banded score and its weight. Stored per candidate rather than folded
// into a total, because when the pipeline proposes something ugly the breakdown is what says which
// band lied -- and that is the only mechanism by which the bands ever improve.
struct ScoreComponent {
    std::string name;
    float raw = 0.0f;   // the quantity as measured, in its own units
    float score = 0.0f; // raw through the band, 0..1
    float weight = 1.0f;
};

// A penalty is monotone on purpose: there is no such thing as too little of an artifact.
struct Penalty {
    std::string name;
    float amount = 0.0f; // subtracted from the weighted mean, in score units
};

struct Score {
    std::vector<ScoreComponent> components;
    std::vector<Penalty> penalties;
    // Weighted mean of the components, less the penalties. Not clamped: a heavily penalised
    // candidate should read as worse than zero rather than tie with a merely mediocre one.
    [[nodiscard]] float overall() const;
    [[nodiscard]] const ScoreComponent* find(std::string_view name) const;
};

// ---------------------------------------------------------------------------------------------
// The validity gate

// Why a candidate was thrown away. Recorded rather than dropped: a histogram of rejection rules over
// a few hundred candidates is the pipeline's most useful diagnostic, because a rule that fires on
// 40% of the population is a schema bug reporting itself.
struct Rejection {
    std::string rule;
    std::string detail;
};

struct HygieneLimits {
    std::uint32_t maxTriangles = 200000;
    float maxDegenerateFraction = 0.02f; // of triangles, by count
    float maxBoundsRadius = 1000.0f;     // metres
    float minBoundsRadius = 1e-3f;
    bool requireFiniteNormals = true;
};

// Generic mesh hygiene: non-finite positions or normals, out-of-range indices, degenerate triangles
// above a fraction, absurd bounds. Domain plausibility (does this thing stand up?) is the
// generator's job, not this one's.
[[nodiscard]] std::optional<Rejection> meshHygiene(const scene::MeshData& mesh, const HygieneLimits& limits);

// ---------------------------------------------------------------------------------------------
// Diversity

// Values parallel to `GeneratorSchema::featureNames`.
using FeatureVector = std::vector<float>;

struct Candidate {
    std::uint32_t index = 0;
    Parameters parameters;
    std::optional<Rejection> rejected; // set means everything below is unpopulated
    Score score;
    FeatureVector features;
    std::uint32_t triangles = 0;
    float surfaceArea = 0.0f;
};

// Farthest-point selection over normalised feature space, seeded with the highest scorer.
//
// The quality/diversity trade is `alpha` and it is explicit on purpose: `alpha = 1` is
// top-N-by-score, `alpha = 0` is diversity with no regard for quality. Hiding this number inside an
// algorithm is how a pipeline ends up with N excellent near-identical individuals, or N diverse ugly
// ones, with no knob to say which way it went wrong.
//
// Each axis is normalised by its observed range across `candidates`, so no axis dominates by unit.
// Returns indices into `candidates`, in selection order. Rejected candidates are ignored.
[[nodiscard]] std::vector<std::size_t> selectDiverse(std::span<const Candidate> candidates,
                                                     std::size_t count, float alpha);

// ---------------------------------------------------------------------------------------------
// Preview specification (the renderer lives in the GPU layer; this is the data half)

struct PreviewView {
    std::string name;
    float azimuthDegrees = 0.0f;
    float elevationDegrees = 10.0f;
    float distanceInRadii = 3.0f;
};

// The six views the addendum asks for. The low angle is not optional: a structure the geometry above
// it encloses is invisible from any camera at subject height, and that has happened here before.
[[nodiscard]] std::vector<PreviewView> defaultViews();

struct PreviewLighting {
    std::string name;              // "neutral", "moonlight", "bioluminescent"
    std::string lightRigPath;      // empty = the built-in studio rig
    bool applyScenePost = false;   // whether to use the target scene's tonemap/bloom settings
};

// ---------------------------------------------------------------------------------------------
// The scene's description of a generated object -- the shared representation

// What a scene file stores for one generated individual, what the world editor edits, and what a
// generator reads. One description, not three.
//
// **`values` is authoritative, not `index`.** The parameter vector is what the generator builds
// from and what the editor writes; the index is provenance -- where these numbers came from. For an
// untouched hero `values == sampleAt(schema, index)` exactly, which is an assertion a test can make
// and the reason the provenance is worth keeping at all. The moment an artist nudges one slider the
// two diverge, and that is correct: the edit is the authored truth and the index still says which
// candidate it started life as.
//
// This ordering is not a preference. This engine's governing rule is that a parameter is
// authoritative and `Scene` is a per-frame derivation rebuilt by `Composition::applyParameters`, so
// anything that lives only as a mesh does not survive one update. A generator whose morphology sat
// in C++ constants would produce an object that cannot be tweaked, keyed or modulated.
//
// **The engine gap, named rather than worked around.** `scene::SourceSpec` enumerates its shapes in
// `scene::PrimitiveKind` -- Box, Cylinder, Sphere, Torus, Point, Procedural, Tube, Mesh -- and none
// of them is "built by a registered generator from a parameter vector". Making a searched organism a
// first-class, editable, round-tripping scene object therefore needs one new `PrimitiveKind` whose
// spec is this struct, plus its arm in `SourceSpec::validate`, `SourceSpec::structuralHash` and the
// JSON both ways, plus its fields in `scene::cloneNodeSpec` -- whose own header says it is "the list
// a new authored field has to be added to", and a node that comes back from an undo missing a field
// is what forgetting it produces.
//
// That new kind is generic on purpose: a tree and a mushroom differ in their generator's name and
// schema, not in how the scene stores them. Whichever project lands it first owns it; the other
// should not add a second.
//
// **Live regeneration is affordable and the throttle already exists.** Hashing `values` into
// `SourceSpec::structuralHash` means an edited parameter rebuilds the mesh, which is what makes an
// artist's nudge visible. `Composition::setInteractiveRebuildBudget` is the measured mechanism that
// keeps that from turning a drag into a slideshow -- an object whose last rebuild cost more than the
// budget waits for its inputs to settle -- and it is deliberately zero offline, because a wall clock
// has no business deciding what a deterministic render contains.
// **This is `scene::GeneratedSource`**, not a parallel type. It lives in `scene/procedural.hpp`
// because `scene::SourceSpec` holds one -- `PrimitiveKind::Generated` (ADR-175) -- and a scene
// cannot depend on the search layer that produced it. The alias is here so this header still reads
// as the one description of a generated object, which it is.
using GeneratedSource = scene::GeneratedSource;

// Whether `source.values` is still exactly what the sampler produced for `source.index`. False after
// an artist has edited it, which is not an error -- the edit is the authored truth and the index
// still records where it started.
[[nodiscard]] bool matchesSample(const ParameterSchema& schema, const GeneratedSource& source);
// Whether this source can be built by this generator: name, version, schema hash and arity.
[[nodiscard]] Result<void> validateAgainst(const GeneratorSchema& schema, const GeneratedSource& source);

// The authored block a scene file stores, and the same block `candidateToJson` emits under
// "source". Deliberately one pair of functions, so there is no way for the two to drift.
[[nodiscard]] nlohmann::json generatedSourceToJson(const GeneratedSource& source);
[[nodiscard]] Result<GeneratedSource> generatedSourceFromJson(const nlohmann::json& j);

// The parameter paths a generated node registers, one per schema axis, relative to the node's own
// prefix (`procedural/<name>/`). Registered rather than baked, so each is keyable, modulatable and
// visible in the inspector like any other parameter.
[[nodiscard]] std::vector<std::string> parameterPaths(const GeneratorSchema& schema,
                                                      std::string_view nodePrefix);

// ---------------------------------------------------------------------------------------------
// The generator contract -- four members

template <typename G>
concept CandidateGenerator = requires(const G& g, const Parameters& params, const Subject& subject) {
    { g.schema() } -> std::convertible_to<const GeneratorSchema&>;
    { g.build(params) } -> std::same_as<Result<Subject>>;
    { g.features(subject, params) } -> std::same_as<FeatureVector>;
    { g.domainScores(subject, params) } -> std::same_as<std::vector<ScoreComponent>>;
};

// ---------------------------------------------------------------------------------------------
// The canonical record

// Everything needed to regenerate a chosen individual, and nothing that could drift from it. No mesh
// is stored: `(generatorName, generatorVersion, schemaHash, index)` regenerates it exactly.
// `selectionNote` is where a person writes why they overrode the ranking, and it is the only field
// here a machine does not fill in.
[[nodiscard]] nlohmann::json candidateToJson(const GeneratorSchema& schema, const Candidate& candidate,
                                             std::string_view selectionNote = {});
[[nodiscard]] Result<Candidate> candidateFromJson(const GeneratorSchema& schema, const nlohmann::json& j);

} // namespace avgen::search
