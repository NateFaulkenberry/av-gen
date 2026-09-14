#pragma once

// Procedural geometry (ADR-023): one source mesh, many instances placed by a distribution and
// perturbed by seeded variation, deformed per vertex on the GPU by an ordered deformer stack,
// shaded with the normal PBR material. Everything here is data; generators are pure functions.
//
// Transform order (world = parent * distribution * placement(i) * variation(i) * source):
//   1. `sourceTransform`      : applied to the source mesh (object-local space)
//   2. variation(i)           : seeded per-instance offset/rotation/scale, in placement space
//   3. placement(i)           : the distribution's transform for instance i
//   4. `distributionTransform`: the whole arrangement's transform
//   5. parent                 : the owning composition node / scene (applied by the caller)
// Local-space deformers act on positions before step 2 (object space); world-space deformers
// act after step 5 (world space). Coordinates are right-handed, +Y up, as everywhere in avgen.
//
// Determinism: instance randoms are `hashInstance(seed, index, channel)`; nothing depends on
// wall time or call order. Structural changes (source, distribution, variation, counts) bump
// `structureVersion` so the renderer rebuilds meshes/instances only then; everything else is a
// per-frame uniform.

#include "core/error.hpp"
#include "core/wind.hpp"
#include "params/parameter_set.hpp"
#include "scene/scene_types.hpp"
#include "spatial/effector.hpp"
#include "spatial/field.hpp"
#include "spatial/spline.hpp"

#include <functional>
#include <memory>
#include "spatial/point_cloud.hpp"
#include "spatial/spatial_ops.hpp"
#include "spatial/spline.hpp"
#include "scene/grammar.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

// ---- source geometry ---------------------------------------------------------------------------

// Point: a camera-facing quad of `pointSize` units (billboarded by the vertex shader; the CPU
// mesh is a unit quad in XY facing +Z). Instances of a Point source are the "points" of the
// performance targets (1M points with a GPU field).
// Procedural: the source is another procedural object of the same scene (`reference`): its
// source mesh is used and its cloud is composed under each of this object's placements
// (hierarchical instancing, ADR-029). The referenced object may itself reference another
// (depth <= kMaxHierarchyDepth; cycles are rejected by validate through the scene).
// Appended only, never reordered: the enum's integer value is written into `structuralHash` and its
// name into every scene file.
enum class PrimitiveKind : std::uint8_t { Box, Cylinder, Sphere, Torus, Point, Procedural, Tube, Mesh, Generated };
[[nodiscard]] const char* primitiveKindName(PrimitiveKind kind);
[[nodiscard]] std::optional<PrimitiveKind> primitiveKindFromName(std::string_view name);

// What a scene stores for one procedurally *searched* organism -- a hero mushroom, a tree -- and what
// the world editor edits (ADR-175).
//
// The unit is a parameter vector with provenance, not geometry, and the ordering is the point:
// `values` is authoritative because this engine's rule is that a parameter is authoritative and
// `Scene` is a per-frame derivation rebuilt by `applyParameters`. An organism whose morphology lived
// in a mesh would not survive one update, could not be keyed, modulated, undone or saved, and would
// be a mesh blob wearing a procedural label.
//
// `index` is provenance rather than identity-of-record: it says which candidate of a search these
// numbers started life as. For an untouched winner `values == search::sampleAt(schema, index)`
// exactly; the moment an artist moves a slider the two diverge, and that is correct.
//
// `schemaHash` is what keeps it honest. Widen a parameter's range or reorder the schema and the hash
// moves, so a stale record announces itself instead of silently regenerating a *different* organism
// under the same name -- the values are positional, so a reordered schema makes every one of them
// mean something else.
//
// Deliberately generic: a tree and a mushroom differ in their generator's name and schema, not in how
// a scene stores them.
struct GeneratedSource {
    std::string generator;              // names a builder in the generator registry below
    std::uint32_t generatorVersion = 1; // bumped when `build` changes what a parameter set means
    std::uint64_t schemaHash = 0;       // 0 = unchecked (hand-authored); non-zero is enforced
    std::uint32_t index = 0;            // provenance: the candidate these values came from
    std::vector<float> values;          // authoritative

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    [[nodiscard]] bool empty() const { return generator.empty(); }
};

// How a generated source becomes geometry. One builder per generator name, returning the mesh for
// one part -- a mushroom's cap, stem and gills are three parts of one parameter set, the same way an
// imported asset's materials are three parts of one file (`SourceSpec::assetPart`).
//
// A registry rather than a switch because the generators live above this layer: `scene` cannot know
// about mushrooms, and a data-driven scene format has to reach code somehow. It is process-global and
// that is a real cost -- registration order must not matter, and it does not: names are unique and a
// second registration of the same name replaces the first, which is what a hot reload needs.
using GeneratedMeshBuilder = std::function<Result<MeshData>(const GeneratedSource&, int part)>;
void registerGenerator(std::string name, GeneratedMeshBuilder builder);
[[nodiscard]] bool hasGenerator(std::string_view name);
[[nodiscard]] std::vector<std::string> registeredGenerators();
void clearGenerators(); // tests, and only tests

struct SourceSpec {
    PrimitiveKind kind = PrimitiveKind::Cylinder;
    // Box
    glm::vec3 size{1.0f, 1.0f, 1.0f};
    int subdivisions = 1;          // per edge, 1..64
    // Bevel (ADR-042), used by Box and Cylinder. A mathematically sharp edge is the loudest tell
    // that geometry was generated rather than made: a real edge is a small radius that catches a
    // highlight. `bevel` is that radius in units, clamped to what the primitive can hold;
    // `bevelSegments` is how many quads cross it. 0 leaves the primitive exactly as it was.
    float bevel = 0.0f;
    int bevelSegments = 3;         // 1..16
    // Cylinder (also uses height, radialSegments, heightSegments, caps)
    float radius = 0.5f;           // cylinder / sphere radius
    float height = 2.0f;           // cylinder height (centred on the origin)
    int radialSegments = 24;       // 3..256
    int heightSegments = 1;        // 1..128
    bool caps = true;
    // Sphere
    int segments = 32;             // longitude, 3..256
    int rings = 16;                // latitude, 2..128
    // Torus
    float majorRadius = 1.0f;
    float minorRadius = 0.25f;
    int majorSegments = 48;        // 3..256
    int minorSegments = 16;        // 3..128
    // Tube (ADR-043): a profile swept along a curve. This is the organic primitive -- stems,
    // branches, vines, roots, tendrils, tentacles and ribbons are all one swept curve with
    // different taper, twist and profile. The curve is embedded rather than referenced by name so
    // the mesh stays a pure function of the spec, and its own generator (line, helix, spiral,
    // bezier, noise) supplies the shape.
    spatial::Spline curve;
    float tubeRadius = 0.1f;       // radius at the start
    float tubeTaper = 0.2f;        // radius at the end, as a fraction of the start (1 = no taper)
    int tubeSides = 10;            // 3..64 around the profile
    int tubeSegments = 24;         // 2..512 along the curve
    float tubeTwist = 0.0f;        // radians of roll accumulated over the whole length
    bool tubeCaps = true;
    // Point
    float pointSize = 0.05f;       // quad edge (units); scaled by the instance scale
    // Procedural
    std::string reference;         // name of the referenced procedural object (kind Procedural)
    // Mesh (ADR-044): an imported glTF/GLB asset as the source, so a scanned rock or an authored
    // fern goes through the same instancing, culling, LOD, variation and distribution machinery
    // as a generated primitive. The renderer is not the artist: the asset supplies what a thing
    // looks like and the procedural system supplies where, how many, and how varied.
    std::string asset;             // path as written; resolved through the AssetRegistry
    // Triangle budget for an imported mesh, applied once at resolve time. Photogrammetry assets
    // arrive at film density -- a single scanned cliff can be 1.5 million triangles -- and an
    // environment made of them will not hold a frame rate. 0 keeps the asset as authored.
    int meshBudget = 0;
    // Which of the asset's materials this object draws (ADR-044). An asset's entities are grouped
    // by material and each group becomes one instanceable mesh: a scanned rock is one part, a tree
    // is bark and leaves. Part 0 is the one carrying the most surface area.
    //
    // Runtime, filled by the Composition when it resolves `asset`, and never serialised -- but
    // unlike `assetMesh` it *is* hashed, because two objects that name the same asset and differ
    // only in their part are two different meshes and the renderer caches meshes by that hash.
    // Without it a tree's leaves would be drawn with the bark's geometry out of the cache.
    int assetPart = 0;
    // Runtime, filled by the Composition when it resolves `asset`; never serialised, and part of
    // no hash except through `asset` and `assetPart`.
    std::shared_ptr<const MeshData> assetMesh;
    // Generated (ADR-175). `generatedPart` selects which part of the organism this object draws, and
    // is the exact analogue of `assetPart`: one parameter set, several meshes, one node each, so that
    // every part is separately selectable and separately materialled in the editor.
    GeneratedSource generated;
    int generatedPart = 0;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const; // changes whenever the mesh would change
};
// Deterministic unit-tested primitive generators (positions, smooth or faceted normals as the
// primitive dictates, uvs, CCW winding facing outwards, centred on the origin).
[[nodiscard]] MeshData makeBox(glm::vec3 size, int subdivisions);
// A box whose twelve edges are cylindrical fillets of radius `bevel` and whose eight corners are
// spherical octants: the Minkowski sum of a smaller box with a sphere. Patch boundaries share
// exact positions and normals, so the flat faces, the fillets and the corners meet without a seam.
[[nodiscard]] MeshData makeBeveledBox(glm::vec3 size, int subdivisions, float bevel, int bevelSegments);
[[nodiscard]] MeshData makeCylinder(float radius, float height, int radialSegments, int heightSegments, bool caps);
// A cylinder with quarter-round rims, revolved from a profile. `bevel` 0, or `caps` off, gives
// makeCylinder exactly.
[[nodiscard]] MeshData makeBeveledCylinder(float radius, float height, int radialSegments, int heightSegments,
                                           bool caps, float bevel, int bevelSegments);
[[nodiscard]] MeshData makeUvSphere(float radius, int segments, int rings);
[[nodiscard]] MeshData makeTorus(float majorRadius, float minorRadius, int majorSegments, int minorSegments);
[[nodiscard]] MeshData makePointQuad(float size); // 4 vertices, 2 triangles, XY plane, normal +Z, uv 0..1
// A tapering, twisting tube swept along a curve (ADR-043). Normals come from the swept surface
// itself, so a taper shades as a cone rather than as a cylinder. Zero-length or degenerate curves
// return an empty mesh rather than a fold.
[[nodiscard]] MeshData makeTube(const spatial::Spline& curve, float radius, float taper, int sides, int segments,
                                float twist, bool caps);
[[nodiscard]] Result<MeshData> makeSourceMesh(const SourceSpec& spec);
// Reduced versions of the source for LOD levels (ADR-029, GPU culling/LOD):
//   0 = makeSourceMesh(spec) exactly;
//   1 = the same generator at half the segment counts (radial/height/major/minor/segments/rings
//       and box subdivisions halved; floors 3 for radial-style counts, 2 for sphere rings, 1 for
//       height segments and subdivisions);
//   2 = a camera-facing billboard quad circumscribing the source's bounding sphere, edge
//       2 * impostorSize * boundingRadius(spec);
//   3 = a single point quad (edge 2 * impostorSize * boundingRadius / 8) - a dot at distance.
// Levels 2 and 3 are drawn through the shader's Point path (camera-facing), so they need no
// orientation of their own. Pure and deterministic: the same spec/level always gives the same mesh.
[[nodiscard]] Result<MeshData> makeLodMesh(const SourceSpec& spec, int level, float impostorSize = 1.0f);
// Vertex-clustering decimation (ADR-045): snaps vertices to a grid sized from `targetTriangles`,
// welds each cell to one averaged vertex and drops the triangles that collapse. Deterministic and
// linear in the input. It suits scanned organic shapes, where the silhouette matters and the
// topology does not; it is the wrong tool for hard-surface geometry with sharp creases.
[[nodiscard]] MeshData decimateMesh(const MeshData& mesh, int targetTriangles);
// Half-diagonal of the source's axis-aligned bounds (the bounding-sphere radius the cull pass
// scales by the instance scale).
[[nodiscard]] float sourceBoundingRadius(const SourceSpec& spec);

// ---- distributions ------------------------------------------------------------------------------

// Spline: instances along the named scene spline (ADR-026): by count (evenly by distance between
// splineStart and splineEnd of the length) or by `spacing` (units of arc length); frame-aligned
// when `alignToSpline` (x = binormal, y = normal, z = tangent) with `roll` about the tangent and
// `splineOffset` in frame space. Grammar: the object's `grammar` expansion provides placements.
// Scatter (ADR-048): the placements come from outside. Every other kind is a formula the object
// evaluates for itself; this one is for arrangements nothing local can derive -- an ecology pass
// that knows which slopes a fern grows on, a scatter baked in a DCC, a survey of real positions.
// The cloud is supplied at resolve time, exactly as an imported mesh is.
enum class DistributionKind : std::uint8_t { Single, Linear, Grid, Radial, Spiral, Spline, Grammar, Scatter };
[[nodiscard]] const char* distributionKindName(DistributionKind kind);
[[nodiscard]] std::optional<DistributionKind> distributionKindFromName(std::string_view name);

enum class OrientationMode : std::uint8_t { None, Outward, Inward, Tangent };
[[nodiscard]] const char* orientationModeName(OrientationMode mode);
[[nodiscard]] std::optional<OrientationMode> orientationModeFromName(std::string_view name);

enum class DistributionPlane : std::uint8_t { XZ, XY, YZ }; // radial/spiral plane; the normal is the third axis
[[nodiscard]] const char* distributionPlaneName(DistributionPlane plane);
[[nodiscard]] std::optional<DistributionPlane> distributionPlaneFromName(std::string_view name);

struct Distribution {
    DistributionKind kind = DistributionKind::Radial;
    int count = 32;                // Linear/Radial/Spiral total; Grid uses countX*countY*countZ
    // Linear
    glm::vec3 start{-5.0f, 0.0f, 0.0f};
    glm::vec3 end{5.0f, 0.0f, 0.0f};
    bool orientAlong = false;      // rotate instances to face the start->end direction
    float spacing = 0.0f;          // > 0 overrides the end point: end = start + dir * spacing * (count - 1)
    // Grid
    glm::ivec3 gridCount{4, 1, 4};
    glm::vec3 gridSpacing{2.0f, 2.0f, 2.0f}; // centred on the origin
    // Radial / Spiral
    float radius = 6.0f;
    float startAngle = 0.0f;       // radians
    float endAngle = 6.2831853f;   // radians; == start + 2pi means a closed circle (count divides 2pi)
    DistributionPlane plane = DistributionPlane::XZ;
    glm::vec3 center{0.0f};
    OrientationMode orientation = OrientationMode::Outward;
    // Spiral
    float radiusGrowth = 0.0f;     // radius added over the whole spiral
    float turns = 3.0f;
    float spiralHeight = 8.0f;     // rise over the whole spiral along the plane normal
    float spiralAngle = 0.0f;      // extra constant rotation about the normal (radians)
    // Scatter: runtime, filled by whoever generated the placements; never serialised, and part of
    // no hash except through `scatterHash`, which that same generator sets to something that
    // changes when the cloud does.
    std::shared_ptr<const spatial::PointCloud> scatterCloud;
    std::uint64_t scatterHash = 0;
    // Spline
    std::string spline;            // scene spline name
    float splineStart = 0.0f;      // fraction of the length
    float splineEnd = 1.0f;
    bool alignToSpline = true;
    float roll = 0.0f;             // radians about the tangent
    glm::vec3 splineOffset{0.0f};  // in the spline frame (binormal, normal, tangent)

    [[nodiscard]] Result<void> validate() const;
    // Spline kind: count when spacing == 0, else floor(length * (end - start) / spacing) + 1
    // (needs the spline; `count` when null). Grammar kind: reported by the owner (ProceduralGeometry).
    [[nodiscard]] int instanceCount(const spatial::Spline* spline = nullptr) const;
    // Placement of instance i (0..count-1) in distribution space: position, rotation (unit
    // quaternion), scale 1. Pure. `u` = normalised index (0 for i=0, 1 for the last; 0 when count==1).
    // `spline` is required for the Spline kind (identity placements when null).
    [[nodiscard]] Transform placement(int index, const spatial::Spline* spline = nullptr) const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// ---- variation ----------------------------------------------------------------------------------

struct Variation {
    std::uint32_t seed = 12345;
    glm::vec3 randomPosition{0.0f};   // max offset per axis (units), uniform in [-r, r]
    glm::vec3 randomRotation{0.0f};   // max angle per axis (radians), uniform in [-r, r]
    glm::vec3 randomScale{0.0f};      // max relative scale per axis: scale *= 1 + [-r, r]
    float randomUniformScale = 0.0f;  // max relative uniform scale
    [[nodiscard]] std::uint64_t structuralHash() const;
};
// Deterministic per-instance random in [0, 1): PCG-style mix of (seed, index, channel).
[[nodiscard]] float hashInstance(std::uint32_t seed, std::uint32_t index, std::uint32_t channel);
// The variation transform for instance i (position offset, rotation, scale), pure.
[[nodiscard]] Transform variationTransform(const Variation& v, std::uint32_t index);

// ---- deformers ----------------------------------------------------------------------------------

enum class DeformerKind : std::uint8_t { Bend, Twist, Sine, Noise, Displacement, Field, Path };
[[nodiscard]] const char* deformerKindName(DeformerKind kind);
[[nodiscard]] std::optional<DeformerKind> deformerKindFromName(std::string_view name);
enum class DeformSpace : std::uint8_t { Local, World };
[[nodiscard]] const char* deformSpaceName(DeformSpace space);
[[nodiscard]] std::optional<DeformSpace> deformSpaceFromName(std::string_view name);

// Semantics (p = point, t = render time, a = amount; axis is a unit vector; center in the
// deformer's space; falloff = the extent along the axis over which the effect ramps 0..1 from
// the center, 0 = no ramp):
//   Bend:  bends around `axis` (the axis the shape is bent along); angle = a * (dot(p-c, axis) /
//          max(falloff, eps)) radians; points rotate about the perpendicular `bendAxis`... in this
//          implementation the bend is the classic Barr bend: the coordinate along `axis` maps to an
//          arc of radius 1/curvature with curvature = a (radians per unit).
//   Twist: rotation about `axis` by angle = a * dot(p - c, axis) (radians per unit) [+ speed * t].
//   Sine:  p += displacementAxis * a * sin(dot(p - c, axis) * frequency + phase + speed * t).
//   Noise: q = p * scale + time * speed; p += a * axisMask * (fbm3(q), fbm3(q + 31.7), fbm3(q + 67.3)) * 2 - 1
//          (three decorrelated channels of hash-based 3-octave value noise; identical on CPU and
//          GPU within float rounding).
//   Displacement: p += n * a * (fbm(p * scale + speed * t) * 2 - 1) — displacement
//          along the vertex normal; the pattern source is `pattern` (0 = noise now; texture/audio/
//          field/user later).
//   Path: curve deform along the scene spline named `spline` (ADR-026). The coordinate along
//          `axis` (from `center`, object space) maps to arc length d = pathOffset + coord *
//          pathScale (pathScale 0 = the source extent along the axis maps to the whole length);
//          the perpendicular components (u along a stable perpendicular of the axis, v along
//          axis x u) are placed in the spline frame: p' = S(d).position + binormal * u + normal * v
//          (+ roll). Result = mix(p, p', amount). Object space only; applied after the instance
//          transform is NOT used (the deformed object is placed by its instance transform).
//   Field: samples the scene field named `field` (ADR-025) at the point (world space when the
//          deformer space is World, else at the instance's world position + local offset):
//          vector fields: p += v * a; scalar fields: p += n * s * a (along the normal) when
//          `alongNormal`, else p += axis * s * a.
struct Deformer {
    DeformerKind kind = DeformerKind::Twist;
    bool enabled = true;
    float amount = 0.0f;
    DeformSpace space = DeformSpace::Local;
    float speed = 0.0f;                 // animation speed (radians or units per second)
    float phase = 0.0f;
    glm::vec3 axis{0.0f, 1.0f, 0.0f};   // primary axis (normalised on use)
    glm::vec3 center{0.0f};
    float falloff = 0.0f;               // 0 = none
    float frequency = 1.0f;             // sine
    glm::vec3 displacementAxis{1.0f, 0.0f, 0.0f}; // sine
    float scale = 1.0f;                 // noise / displacement spatial frequency
    std::uint32_t seed = 1;             // noise
    glm::vec3 axisMask{1.0f, 1.0f, 1.0f}; // noise per-axis mask
    int pattern = 0;                    // displacement pattern source (0 = noise)
    std::string field;                  // Field: FieldSpec name (resolved by the owner/renderer)
    bool alongNormal = true;            // Field: scalar fields displace along the normal
    std::string spline;                 // Path: spline name
    float pathOffset = 0.0f;            // Path: arc-length offset
    float pathScale = 0.0f;             // Path: units of arc length per object unit (0 = fit)
    float pathRoll = 0.0f;              // Path: extra roll (radians)
};
// CPU reference of the whole stack, identical in meaning to the GPU shader: applies the enabled
// deformers in order; `instanceWorld` is the instance's world matrix (local deformers apply to
// `p` in object space before it, world deformers to the world position after it). Returns the
// world-space position. Used by tests and tools.
[[nodiscard]] glm::vec3 deformPoint(const std::vector<Deformer>& stack, glm::vec3 objectPoint,
                                    const glm::mat4& instanceWorld, double time,
                                    const spatial::FieldSet* fields = nullptr, glm::vec3 normal = {0.0f, 1.0f, 0.0f});
[[nodiscard]] glm::vec3 applyDeformer(const Deformer& d, glm::vec3 p, double time);
// Field deformer (needs the field set; `normal` in the same space as `p`). No-op when unbound.
[[nodiscard]] glm::vec3 applyFieldDeformer(const Deformer& d, glm::vec3 p, glm::vec3 normal, double time,
                                           const spatial::FieldSet& fields);
// Path deformer (needs the spline and the source extent along the axis for pathScale == 0).
[[nodiscard]] glm::vec3 applyPathDeformer(const Deformer& d, glm::vec3 p, const spatial::Spline& spline,
                                          float sourceExtentAlongAxis);
// Full stack with every dependency (fields for Field deformers, splines for Path deformers).
struct DeformContext {
    const spatial::FieldSet* fields = nullptr;
    const spatial::SplineSet* splines = nullptr;
    float sourceExtent = 1.0f; // along the path axis
};
[[nodiscard]] glm::vec3 deformPointWith(const std::vector<Deformer>& stack, glm::vec3 objectPoint,
                                        const glm::mat4& instanceWorld, double time, const DeformContext& ctx,
                                        glm::vec3 normal = {0.0f, 1.0f, 0.0f});
// The GPU-side noise, evaluated on the CPU (for tests): 3-octave value fBM in [0, 1].
[[nodiscard]] float fbm3(glm::vec3 p, std::uint32_t seed);
constexpr int kMaxDeformers = 8;
constexpr std::size_t kKeepCloudMax = 262144;

// ---- material variation ------------------------------------------------------------------------

struct MaterialVariation {
    float hueShift = 0.0f;          // per-instance random hue rotation amount (turns, 0..1)
    float hueGradient = 0.0f;       // hue rotation across normalised index (turns)
    float valueRandom = 0.0f;       // per-instance brightness variation (relative)
    float emissiveRandom = 0.0f;    // per-instance emissive multiplier variation (relative)
    float emissiveGradient = 0.0f;  // emissive multiplier ramp across normalised index (adds 0..x)
    // ADR-054: colour that clusters in space. Per-instance random hue makes a meadow noisy --
    // every neighbour disagrees -- where what reads as a living population is regions that agree
    // with themselves and differ from the next valley over. `hueField` is the swing in turns and
    // `hueFieldScale` the size of a region in metres; the field is sampled at the instance's
    // placement, so neighbours land in the same part of it.
    float hueField = 0.0f;
    float hueFieldScale = 40.0f;
    // Rotate hue through OKLCH rather than by spinning RGB about the grey axis. The legacy path
    // changes lightness and chroma with the angle, which on a saturated emitter reads as the
    // brightness flickering rather than the colour turning.
    bool perceptualHue = false;
    // ADR-057: the living chromatic field. The hue offsets above are baked once when the cloud is
    // projected, so they cannot move; this drifts the emissive hue in world space and time, in the
    // shader. 0 turns it off.
    float chromaDrift = 0.0f;       // hue swing, turns
    float chromaDriftScale = 55.0f; // metres between opposite phases
    float chromaDriftSpeed = 0.05f; // Hz
    // What fraction of instances do not light up at all, 0..1. Random *variation* is symmetric --
    // it makes every specimen a bit brighter or dimmer than the mean -- so it cannot say "most of
    // this forest is dark and a few trees are lanterns", which is the difference between a wood
    // with something living in it and a wall of lamps. Chosen by a stable per-instance hash.
    float emissiveSparsity = 0.0f;
};

// ---- the procedural object ----------------------------------------------------------------------

using InstanceRecord = spatial::InstanceRecord; // the fixed point projection (spatial/point_cloud.hpp)

// Hierarchy (ADR-029): self-recursion. Level 0 = the distribution's placements; level k places a
// copy of level k-1 under each placement, transformed by `scalePerLevel`/`offsetPerLevel`/
// `rotationPerLevel` (degrees) per level. Total = count^(depth+1) instances, truncated to
// `maxInstances` in order. depth 0 = no recursion.
struct HierarchySpec {
    int recursionDepth = 0;              // 0..kMaxHierarchyDepth
    int maxInstances = 100000;
    float scalePerLevel = 0.5f;
    glm::vec3 offsetPerLevel{0.0f};
    glm::vec3 rotationPerLevelDegrees{0.0f};
    bool colorPerLevel = true;           // hue rotates by 1/(depth+1) turns per level
    [[nodiscard]] std::uint64_t structuralHash() const;
};
constexpr int kMaxHierarchyDepth = 4;

// ---- culling and LOD (ADR-029) -------------------------------------------------------------------

// Per-object GPU culling and level of detail. The defaults are "everything off": no cull pass is
// encoded, the draw keeps the plain `DrawIndexed(indexCount, instanceCount)` path and every buffer
// is byte-identical to a build without this feature, so existing scenes render bit-identically.
//
// `cull` enables the compute pass (shaders/cull.wgsl): each instance's world bounding sphere
// (centre = objectMatrix * record position, radius = the source's bounding radius x the largest
// absolute instance scale component x the object matrix scale) is tested against the six frustum
// planes of the frame's view-projection, then against `maxDistance` (0 = no limit) and
// `minScreenRadius` (projected radius in pixels, 0 = no limit).
//
// `lodCount` > 1 additionally picks a level per instance and draws one indirect draw per level
// from its own compacted list. The thresholds in `lodDistances` are LOD0->1, 1->2, 2->3; a
// threshold of 0 ends the ladder (the level stays at the last one reached), which is why the
// default (all zero) keeps everything at LOD0. With `lodByScreenSize` the thresholds are projected
// radii in pixels and a level is taken when the radius drops to or below the threshold (so they
// should descend); otherwise they are world distances and a level is taken when the distance
// reaches the threshold (so they should ascend).
//
// Only `lodCount` is structural (it decides how many meshes are generated); everything else is a
// per-frame uniform and can be modulated without a rebuild.
struct LodSettings {
    bool cull = false;
    float maxDistance = 0.0f;      // world units; 0 = no distance limit
    float minScreenRadius = 0.0f;  // pixels; 0 = no screen-size limit
    int lodCount = 1;              // 1..kMaxLodLevels
    float lodDistances[3] = {0.0f, 0.0f, 0.0f}; // LOD0->1, 1->2, 2->3
    bool lodByScreenSize = true;   // thresholds are projected radii in pixels, not distances
    // How wide a band the population migrates a LOD change across, as a fraction of the threshold
    // (ADR-082). Each instance gets its own offset threshold from a hash of its index, so a band
    // of the world stops changing mesh on one frame together. Deterministic -- a pure function of
    // the instance index -- so it is on by default. 0 restores the old hard-edged behaviour.
    float lodSpread = 0.12f;
    // Dead zone around every threshold, as a fraction of it. An instance keeps its current level
    // until the metric crosses by this much, which is what stops one sitting on a threshold from
    // strobing as the camera breathes.
    //
    // Off by default, because it is the one thing here that reads the previous frame: with it on,
    // what you see depends on how the camera got here and not only on where it is. That is a real
    // trade against this engine's frame-independence, so it is offered rather than assumed.
    float lodHysteresis = 0.0f;
    float impostorSize = 1.0f;     // multiplies the LOD2/LOD3 billboard size
    [[nodiscard]] std::uint64_t structuralHash() const; // lodCount only (the rest are uniforms)
};
constexpr int kMaxLodLevels = 4;

// Everything a cloud generation needs from outside the object (other objects for Procedural
// sources, splines for Spline distributions and Path deformers).
struct ProceduralGeometry;
struct GenerationContext {
    const std::vector<ProceduralGeometry>* objects = nullptr; // the scene's procedurals (for references)
    const spatial::SplineSet* splines = nullptr;
    int depth = 0;                                            // reference recursion guard
};

struct ProceduralGeometry {
    std::string name = "procedural";
    // ADR-108. Non-empty: this object is one material part of the object named here, and the two
    // share ONE spatial instance set -- the same cloud, the same seed, the same placements. Only
    // the mesh, the material and the per-instance colour differ. The renderer culls the lead's
    // records once and emits every part's draw from that one decision, so a spatial instance is
    // culled once and counted once however many materials the asset carries.
    //
    // Not structural: it changes nothing about what this object generates, only how the renderer
    // groups the work. Set where the parts are created (composition.cpp) and preserved across
    // applyProceduralParameters, which edits `live` in place rather than copying `rest` wholesale.
    std::string partOf;
    bool visible = true;
    SourceSpec source;
    Transform sourceTransform;
    Distribution distribution;
    Transform distributionTransform;
    Variation variation;
    std::vector<Deformer> deformers;     // ordered stack, at most kMaxDeformers
    Material material;
    MaterialVariation materialVariation;
    // ADR-055: how this species answers the wind. Purely a per-frame uniform -- two layers that
    // differ only in how they move share every mesh, every placement and every buffer -- so it is
    // deliberately not part of `structureVersion`.
    wind::VegetationMotion motion;
    // Spatial processing (ADR-024/025). Structural: `pointOps` run on the point cloud at rebuild
    // (after distribution + variation, before projection), in order. Per frame: `effectors`
    // (≤ kMaxEffectors) act on the instance records on the GPU (CPU reference:
    // spatial::applyEffectorsToRecords); `emissiveField` multiplies emission by the named scalar
    // field's sample at the instance origin (× emissiveFieldAmount, 0 = off).
    std::vector<spatial::PointOp> pointOps;
    std::vector<spatial::Effector> effectors;
    std::string emissiveField;
    float emissiveFieldAmount = 0.0f;
    std::string extraLane;               // attribute projected into InstanceRecord::emissive.a
    HierarchySpec hierarchy;             // self-recursion (structural)
    LodSettings lod;                     // GPU culling / LOD (only lodCount is structural)
    // Whether this object is drawn into the shadow maps. Ground cover is the case this exists for:
    // nineteen thousand grass clumps cast shadows that are, at the sizes they are drawn, smaller
    // than a shadow-map texel -- so the cost is real and the result is not visible. Never a
    // structural property: it changes which passes draw the object, not what the object is.
    bool castsShadow = true;
    Grammar grammar;                     // placements when distribution.kind == Grammar (structural)
    // Structural outputs (filled by rebuild()); the renderer uploads them when the version
    // changes. `structureVersion` is bumped by rebuild() whenever the structural hash changed.
    // `cloud` is the point cloud the records were projected from, retained for inspection and
    // tools when count <= kKeepCloudMax (cleared otherwise).
    spatial::PointCloud cloud;
    std::vector<InstanceRecord> instances;
    std::uint64_t structureVersion = 0;
    std::uint64_t meshHash = 0;           // hash of the last generated source (renderer cache key)
    std::uint64_t builtHash = 0;          // structuralHash() at the last rebuild() (internal bookkeeping)
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f}; // of instance origins + source extent (world of the object)

    [[nodiscard]] Result<void> validate() const;
    // Scene-level check: references resolve and do not cycle (depth <= kMaxHierarchyDepth).
    [[nodiscard]] static Result<void> validateReferences(const std::vector<ProceduralGeometry>& objects);
    // Regenerates instances (and reports whether the source mesh must be regenerated) when the
    // structural inputs changed since the last call; cheap when nothing changed. Returns true
    // when something was rebuilt. Pipeline: distribution + variation -> PointCloud (position,
    // rotation, scale, id, seed, index, color/emissive from material variation) -> pointOps ->
    // projectInstances(extraLane) -> bounds (+ effector strength padding).
    bool rebuild(const GenerationContext& ctx = {});
    // The base cloud before pointOps (pure; used by rebuild and tests). Spline distributions,
    // Procedural sources and hierarchy need the context (empty context -> identity/placeholder).
    [[nodiscard]] spatial::PointCloud generateCloud(const GenerationContext& ctx = {}) const;
    // The mesh this object draws: its primitive, or the referenced object's (recursively).
    [[nodiscard]] Result<MeshData> resolveSourceMesh(const GenerationContext& ctx = {}) const;
    // Structural hash including everything the context contributes (referenced objects, splines).
    [[nodiscard]] std::uint64_t contextualHash(const GenerationContext& ctx) const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    // Instance world matrix within the object (distribution * placement * variation * source).
    [[nodiscard]] glm::mat4 instanceMatrix(std::uint32_t index) const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<ProceduralGeometry> fromJson(const nlohmann::json& j);
};

// ---- parameters (rest/finals pattern, like particles) ------------------------------------------

// Every field that makes sense as a modulation target, registered under `prefix` (e.g.
// "procedural/columns/"): source/*, distribution/*, transform/*, variation/*, deform/<slot>/*
// (slot = 1-based, label "<kind>/<field>"), ops/<slot>/*, effector/<slot>/*, emissiveFieldAmount,
// lod/* (enabled, maxDistance, minScreenRadius, distance1..3), material/*, materialVariation/*. Enum/kind fields are int parameters; count/segment fields are
// ints. Group = prefix without the trailing '/'.
struct ProceduralParameters;
constexpr int kMaxEffectors = spatial::kMaxEffectors;
[[nodiscard]] ProceduralParameters registerProceduralParameters(params::ParameterSet& params,
                                                                const ProceduralGeometry& rest,
                                                                const std::string& prefix);
// Copies parameter finals into `live` (rest values are the scene-file authored values; the
// parameters' bases default to them). Calls live.rebuild() when structure changed. Returns true
// when a structural rebuild happened.
bool applyProceduralParameters(const ProceduralParameters& p, const ProceduralGeometry& rest, ProceduralGeometry& live);
void unregisterProceduralParameters(params::ParameterSet& params, const ProceduralParameters& p);

struct ProceduralParameters {
    std::string prefix;
    std::vector<params::IParameter*> all; // everything registered (for unregister)
    // Named handles for the showcase code and tests (nullptr when not registered).
    params::Parameter<float>* sourceRadius = nullptr;
    params::Parameter<float>* sourceHeight = nullptr;
    params::Parameter<glm::vec3>* sourceSize = nullptr;
    params::Parameter<int>* distributionCount = nullptr;
    params::Parameter<float>* distributionRadius = nullptr;
    params::Parameter<glm::vec3>* transformPosition = nullptr;
    params::Parameter<glm::vec3>* transformRotation = nullptr; // Euler degrees
    params::Parameter<glm::vec3>* transformScale = nullptr;
    params::Parameter<glm::vec3>* sourceScale = nullptr;
    params::Parameter<int>* seed = nullptr;
    std::array<params::Parameter<float>*, kMaxDeformers> deformerAmount{};
    std::array<params::Parameter<float>*, kMaxDeformers> deformerSpeed{};
    params::Parameter<glm::vec3>* baseColor = nullptr;
    params::Parameter<glm::vec3>* emissiveColor = nullptr;
    params::Parameter<float>* emissive = nullptr;
    params::Parameter<float>* roughness = nullptr;
    params::Parameter<float>* metallic = nullptr;
    params::Parameter<float>* hueShift = nullptr;
    params::Parameter<bool>* visible = nullptr;
    // ops/<slot>/amount|enabled|offset|angle|factor|threshold|probability|value (slot 1-based, structural)
    std::vector<params::Parameter<float>*> opAmount;
    // effector/<slot>/strength|weight|enabled (per frame)
    std::array<params::Parameter<float>*, kMaxEffectors> effectorStrength{};
    params::Parameter<float>* emissiveFieldAmount = nullptr;
    params::Parameter<int>* recursionDepth = nullptr;     // hierarchy/depth (structural)
    params::Parameter<float>* scalePerLevel = nullptr;    // hierarchy/scalePerLevel (structural)
    params::Parameter<float>* splineStart = nullptr;      // distribution/splineStart
    params::Parameter<float>* splineEnd = nullptr;        // distribution/splineEnd
    // Culling / LOD (per frame, ADR-029)
    params::Parameter<bool>* lodEnabled = nullptr;        // lod/enabled -> LodSettings::cull
    params::Parameter<float>* lodMaxDistance = nullptr;   // lod/maxDistance
    params::Parameter<float>* lodMinScreenRadius = nullptr; // lod/minScreenRadius
    params::Parameter<float>* lodSpread = nullptr;          // lod/spread
    params::Parameter<float>* lodHysteresis = nullptr;      // lod/hysteresis
    std::array<params::Parameter<float>*, 3> lodDistance{}; // lod/distance1..3
};

} // namespace avgen::scene
