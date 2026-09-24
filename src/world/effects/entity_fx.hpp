#pragma once

// FXL: per-entity effect lanes (Effect Library Wave 1, package 1.4).
//
// An Entity-owned effect that changes how its owner SHADES -- a Glow, a Pulse, a Bloom Source --
// cannot be a material-program op, because a program is shared by every user of the material
// (`scene/material_params.hpp`): it cannot make one saucer of three glow. It is instead a set of
// per-draw lanes the lit shader reads:
//
//   * two inline lanes in the padding of `rendering::ObjectUniforms` (`fxA`, `fxB`): the gate,
//     the gain, the own-emission tint and the bloom share. Every entity has them; an entity with no
//     effect has them all zero, and `fxA.z == 0` is the shader's uniform early-out.
//   * a 256-byte record in a read-only storage buffer (`entityFx`, group 1 binding 2), indexed by
//     `fxA.w`, holding the sub-blocks that do not fit inline: the added emission, the rim and the
//     travelling band. Record 0 is neutral and is never referenced.
//
// **Composition** (rendering-architecture.md §7). Every FXL effect on one owner folds into ONE
// record, by fixed rules, so the result never depends on the order two additive things were listed:
//
//   gains multiply | added emission sums | rims sum (power weighted by rim strength) |
//   own-emission tints multiply | bloom share takes the max | the travelling band is EXCLUSIVE
//
// A second effect needing an occupied exclusive sub-block (two travelling Pulses on one owner) is
// `Dropped`, and its reason names the effect holding the block.
//
// The shader then computes, for a fragment of an affected draw:
//
//   emission = (ownEmission * ownTint + added + rim(N.V)) * gain * band(position)
//
// before fog, so a glow is seen through the air like the surface it is on, and into BOTH the HDR
// colour and the emission target, so selective bloom treats it as a light source (ADR-039). The
// bloom share then raises the emission target towards the surface's whole radiance without
// touching the colour: that is Bloom Source.
//
// **Wave 2 (the surface slice)** adds sub-blocks that change the owner's SHAPE and COVERAGE, not only
// its light, and so run in every pass that draws it:
//
//   * clip (Dissolve, Growth): a keep-value in the owner's own space against a threshold; the lit
//     pass discards and burns an edge, and `fs_depth` -- the depth prepass and every shadow map --
//     discards the same fragments, so a dissolving owner's shadow dissolves;
//   * displacement (Breathing's inflate, Organic Pulsation's travelling bulge, Motion Smear): the
//     vertex stage of the lit pass, the prepass and the shadows (pbr.wgsl `vs_entity`, and
//     procedural.wgsl `vs_proc`), evaluated again at the previous frame's time for the velocity;
//   * patterns (Bioluminescence, Pulsing Veins on Worley F2 - F1), hue (Color Cycling) and a
//     view-space kicker (Rim Light), in an EXTENSION record at index + 1 so an owner without them
//     still takes one record. Fresnel is the rim lane Glow already has.
//
// Their fold rules: clips of one kind take the max; the displacement modes sum; every other Wave 2
// sub-block is exclusive, and a second holder is Dropped naming the first. Each sets its own flag
// bit, and a sub-block whose amount is zero sets none -- the shader's per-draw gate for it.
//
// The builder is `buildEntityFxFrame`, called by `Engine::updateEffects` like every other stage's
// builder. It walks the evaluation order, asks each live EntityLanes instance for its contribution
// through the type's `resolve.lanes` hook (the type never touches a record), applies the instance's
// activation envelope GENERICALLY, folds per owner, and maps each scene entity in the owner's drawn
// range (`EffectSceneQuery::nodeView`) to the owner's record. A Glow's spill light is requested
// from LIGHTMOD's pool in the same walk (`effect_lights.hpp`).

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_lights.hpp"
#include "world/effects/effect_timing.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace avgen::world {

// ---- the GPU record ---------------------------------------------------------------------------

// Mirrors `struct EntityFx` in shaders/common.wgsl; the CPU/WGSL layout guard holds them together.
inline constexpr std::size_t kEntityFxLanes = 16;
struct EntityFxRecord {
    glm::vec4 lanes[kEntityFxLanes];
};
static_assert(sizeof(EntityFxRecord) == 256);

// Which lane holds what. Lanes 0 and 1 are ALSO the two inline lanes the renderer copies into
// `ObjectUniforms::fxA/fxB`; they are in the record so the record is the one description of the
// owner's state (and so a later vertex-stage sub-block can read them from the buffer).
//   0  fxA: x = emission gain, y = bloom share (0..1), z = flags, w = this record's index
//   1  fxB: rgb = own-emission tint (multiplies the material's emission), w = 0
//   2  added emission: rgb = radiance added in the effect's colour, w = 0
//   3  rim: rgb = rim radiance at grazing, w = rim power
//   4  band axis: xyz = world axis / owner extent, w = offset (u = dot(p, xyz) + w, 0..1 across)
//   5  band: x = centre (in u), y = half width (in u), z = waveform, w = depth (0..1)
// Wave 2 (the surface slice) -- the owner's frame and the vertex/clip sub-blocks:
//   6..8  owner frame: world -> "owner space" q, rows (q_i = dot(row.xyz, p) + row.w). q is the
//         owner's node-local position, centred on its drawn bounds and divided by their half
//         diagonal, so it rides the owner (moving, turning, scaling) and spans about [-1, 1].
//   9   shape: xyz = the node's origin in q, w = gy (the height coordinate h = q.y * gy + 0.5 runs
//       0 at the bottom of the bounds to 1 at the top)
//   10  clip: x = mode (EntityFxClipMode), y = threshold (keep where k >= threshold), z = edge
//       width (in k), w = noise scale (cells per unit of q)
//   11  clip edge: rgb = edge radiance, w = noise breakup (Growth) / direction bias (Dissolve)
//   12  inflate: x = amplitude (m), y = rate (Hz), z = asymmetry (-1..1), w = phase (cycles)
//   13  x = inflate region centre (h), y = region width (h; 0 = the whole body),
//       z = travelling amplitude (m), w = travelling bulge width (h)
//   14  travelling: x = speed (h per second), y = interval (h between bulges), z = direction
//       (+1 root to tip, -1 back), w = 1 / the largest distance from the origin in q (radial clip)
//   15  smear: xyz = the smear vector (m; where a fully trailing vertex is pushed), w = sharpness
// When `kFxExt` is set the owner has a SECOND record at index + 1 (the extension), holding the
// pattern sub-blocks that did not fit (EntityFxExtLane below). An owner without one uses one record.
enum EntityFxLaneIndex : std::uint32_t {
    kFxLaneA = 0,
    kFxLaneB = 1,
    kFxLaneAdd = 2,
    kFxLaneRim = 3,
    kFxLaneBandAxis = 4,
    kFxLaneBand = 5,
    kFxLaneFrame0 = 6,
    kFxLaneFrame1 = 7,
    kFxLaneFrame2 = 8,
    kFxLaneShape = 9,
    kFxLaneClip = 10,
    kFxLaneClipEdge = 11,
    kFxLaneInflate = 12,
    kFxLaneRegion = 13,
    kFxLaneTravel = 14,
    kFxLaneSmear = 15,
};

// The extension record's lanes (record index + 1).
//   0  bio: x = pattern (EntityFxBioPattern), y = scale (cells per unit of q), z = coverage, w = colour variation
//   1  bio: rgb = radiance, w = breathe rate (Hz)
//   2  bio: x = breathe depth, y = wave speed (h per second), z = wave interval (s), w = wave gain
//   3  veins: x = scale, y = width (in F2 - F1), z = noise (domain warp), w = coordinate (0 height, 1 radial)
//   4  veins: rgb = radiance near the source, w = pulse speed (h per second)
//   5  veins: rgb = radiance far from it, w = pulse width (h)
//   6  veins: x = pulse interval (h), y = glow between pulses (0..1), zw = 0
//   7  hue: x = speed (turns per second), y = range (turns; >= 1 is the whole wheel), z = spatial
//      frequency (cycles per unit of q), w = channel (0 base colour, 1 emission, 2 both)
//   8  hue: xyz = spatial axis (unit, in q), w = phase (turns)
//   9  rim light: rgb = radiance, w = power
//   10 rim light: xyz = direction (unit, VIEW space: x right, y up, z away from the camera), w = threshold
//   11 rim light: x = softness, yzw = 0
enum EntityFxExtLane : std::uint32_t {
    kFxExtBio0 = 0,
    kFxExtBio1 = 1,
    kFxExtBio2 = 2,
    kFxExtVeins0 = 3,
    kFxExtVeins1 = 4,
    kFxExtVeins2 = 5,
    kFxExtVeins3 = 6,
    kFxExtHue0 = 7,
    kFxExtHue1 = 8,
    kFxExtRim0 = 9,
    kFxExtRim1 = 10,
    kFxExtRim2 = 11,
};

// Bits of `fxA.z`. Zero is "no effect on this draw" and the shader returns before any FXL work.
// A float lane carries them, so they stay below 2^24.
enum EntityFxFlag : std::uint32_t {
    kFxOn = 1u << 0,         // the inline lanes (gain, tint) apply
    kFxRecord = 1u << 1,     // read lanes 2 and 3 from the record (added emission, rim)
    kFxBand = 1u << 2,       // read lanes 4 and 5 (the travelling band)
    kFxBloomShare = 1u << 3, // raise the emission target by the bloom share
    // Wave 2. Each is also the shader's per-draw (uniform) gate for its sub-block.
    kFxClip = 1u << 4,       // lanes 6..11: discard below the threshold, in EVERY pass (lit, depth, shadow)
    kFxInflate = 1u << 5,    // lanes 6..9, 12, 13: swell along the normal (vertex, every pass)
    kFxTravel = 1u << 6,     // lanes 6..9, 13, 14: a bulge travelling along the owner (vertex, every pass)
    kFxSmear = 1u << 7,      // lane 15: the trailing side pushed back along the path (vertex, every pass)
    kFxExt = 1u << 8,        // the owner has an extension record at index + 1
    kFxBio = 1u << 9,        // ext 0..2: a living light pattern
    kFxVeins = 1u << 10,     // ext 3..6: light pulsing through a vein network
    kFxHue = 1u << 11,       // ext 7, 8: hue rotation
    kFxRimLight = 1u << 12,  // ext 9..11: a directional back-light rim
};
inline constexpr std::uint32_t kFxDisplaceFlags = kFxInflate | kFxTravel | kFxSmear;

// How the clip's keep-value k is formed (lane 10.x). The numbers reach the shader: append-only.
enum class EntityFxClipMode : std::uint8_t { None = 0, Noise = 1, Height = 2, Radial = 3 };
// Bioluminescence's pattern (ext lane 0.x). Append-only.
enum class EntityFxBioPattern : std::uint8_t { Spots = 1, Stripes = 2, Cells = 3 };

// The record budget. 1 MiB of records (4,096 records) -- one record per affected OWNER (two for an
// owner with an extension), shared by every entity in its range, so this is not a per-entity number
// and no scene is near it. It exists
// so the buffer has a documented ceiling; an owner past it is `Dropped` with this number.
inline constexpr std::uint32_t kEntityFxBudgetMiB = 1;
inline constexpr std::uint32_t kMaxEntityFxRecords =
    kEntityFxBudgetMiB * 1024u * 1024u / static_cast<std::uint32_t>(sizeof(EntityFxRecord));

// The waveform a pulse or a band cross-section follows. Its numeric value reaches the shader
// (`fxWave` in pbr_shade.wgsl) and must match `pulseWave` below, so it is append-only.
enum class FxWaveform : std::uint8_t { Sine, Triangle, Square, Saw, Heartbeat };
inline constexpr std::size_t kFxWaveformCount = 5;
// One cycle of `w`, x in [0, 1). Every shape is 0 at both ends, so a band cross-section built from
// it has no hard edge, and 1 at its crest. The CPU twin of `fxWave` in shaders/pbr_shade.wgsl.
[[nodiscard]] float pulseWave(FxWaveform w, float x);

// ---- what a type hands the builder ---------------------------------------------------------------

// The travelling band, resolved against the owner: an axis in world space and the owner's extent
// along it, the band's centre and half-width in that normalised coordinate, and its depth.
struct EntityFxBand {
    glm::vec3 axis{0.0f, 1.0f, 0.0f}; // unit, world space
    float u0 = 0.0f;                  // dot(p, axis) at the start of the owner...
    float u1 = 1.0f;                  // ...and at its end
    float centre = 0.5f;              // 0..1 along the owner (may run past either end to enter/leave)
    float halfWidth = 0.1f;           // in the same units
    FxWaveform waveform = FxWaveform::Sine;
    float depth = 1.0f;               // 0 = no band, 1 = only the band emits
};

// One effect's contribution to its owner. Neutral as default-constructed: folding a default
// contribution into anything changes nothing, which is what the tests hold the fold rules against.
struct EntityLaneContribution {
    float gain = 1.0f;              // multiplies ALL of the owner's emission, own and added
    glm::vec3 ownTint{1.0f};        // multiplies the owner's own (material) emission
    glm::vec3 add{0.0f};            // radiance added over the surface
    glm::vec3 rim{0.0f};            // radiance added at grazing angles
    float rimPower = 0.0f;          // the rim's falloff exponent
    float bloomShare = 0.0f;        // 0..1: how much of the surface's radiance the bloom sees
    bool hasBand = false;
    EntityFxBand band;
    // LIGHTMOD: a spill light at the owner's centre. `spillIntensity` is scaled by the owner's
    // FOLDED gain by the builder, so a Pulse on the same owner pulses the spill too.
    bool spill = false;
    glm::vec3 spillColor{1.0f};
    float spillIntensity = 0.0f;   // candela at gain 1
    float spillRange = 3.0f;       // multiples of the owner's radius
    float spillFog = 0.0f;         // 0..1, the light's volumetric strength

    // ---- Wave 2: the surface sub-blocks. Each is EXCLUSIVE per owner (one clip, one inflate, one
    // travelling bulge, one smear, one pattern of each kind, one hue, one rim light); a second holder
    // is Dropped by the builder with the holder's name, except the clip, whose thresholds take the
    // max when the modes agree (rendering-architecture §7: "clip takes the max"). The displacement
    // MODES sum -- a Breathing and an Organic Pulsation and a Motion Smear on one owner all move it.
    bool hasClip = false;
    EntityFxClipMode clipMode = EntityFxClipMode::None;
    float clipThreshold = 0.0f;   // in k (see the shader); the builder's envelope scales `clipHidden`
    float clipHidden = 0.0f;      // 0 = nothing clipped .. 1 = everything; the threshold follows it
    float clipEdgeWidth = 0.05f;
    float clipNoiseScale = 3.0f;
    glm::vec3 clipEdge{0.0f};     // edge radiance
    float clipBreakup = 0.0f;     // Growth: noise on the front; Dissolve: bias toward a height sweep

    bool hasInflate = false;
    float inflateAmp = 0.0f;      // metres along the normal at the crest
    float inflateRate = 0.25f;    // Hz
    float inflateAsym = 0.0f;
    float inflatePhase = 0.0f;
    float regionCentre = 0.5f;    // in h
    float regionWidth = 0.0f;     // 0 = the whole body

    bool hasTravel = false;
    float travelAmp = 0.0f;
    float travelWidth = 0.08f;    // in h
    float travelSpeed = 0.5f;     // h per second
    float travelInterval = 1.0f;  // h between bulges
    float travelDirection = 1.0f; // +1 root to tip

    bool hasSmear = false;
    glm::vec3 smear{0.0f};        // metres
    float smearSharpness = 1.5f;

    bool hasBio = false;
    EntityFxBioPattern bioPattern = EntityFxBioPattern::Spots;
    float bioScale = 6.0f;
    float bioCoverage = 0.5f;
    float bioVariation = 0.2f;
    glm::vec3 bioColor{0.0f};     // radiance
    float bioBreatheRate = 0.2f;
    float bioBreatheDepth = 0.6f;
    float bioWaveSpeed = 0.4f;
    float bioWaveInterval = 6.0f;
    float bioWaveGain = 1.5f;

    bool hasVeins = false;
    float veinsScale = 5.0f;
    float veinsWidth = 0.06f;
    float veinsNoise = 0.3f;
    float veinsCoordinate = 0.0f; // 0 height, 1 radial
    glm::vec3 veinsNear{0.0f};
    glm::vec3 veinsFar{0.0f};
    float veinsPulseSpeed = 0.3f;
    float veinsPulseWidth = 0.08f;
    float veinsPulseInterval = 0.5f;
    float veinsBaseline = 0.15f;

    bool hasHue = false;
    float hueSpeed = 0.1f;
    float hueRange = 1.0f;
    float hueFrequency = 0.0f;
    float hueChannel = 2.0f;
    glm::vec3 hueAxis{0.0f, 1.0f, 0.0f};
    float huePhase = 0.0f;

    bool hasRimLight = false;
    glm::vec3 rimLight{0.0f};     // radiance
    float rimLightPower = 2.0f;
    glm::vec3 rimLightDir{-0.6f, 0.45f, 0.65f}; // view space, unit
    float rimLightThreshold = 0.0f;
    float rimLightSoftness = 0.25f;

    // Filled by the BUILDER, from the owner's drawn view, when a sub-block above needs the owner
    // frame (the types never write these).
    bool hasFrame = false;
    glm::vec4 frame[3]{};         // world -> q rows
    glm::vec4 shape{0.0f};        // xyz = node origin in q, w = gy
    float radialInv = 1.0f;       // 1 / the largest origin-to-corner distance in q

    [[nodiscard]] bool needsFrame() const {
        return hasClip || hasInflate || hasTravel || hasBio || hasVeins || hasHue;
    }
};

// The hook a type with `EffectBucket::EntityLanes` declares (`EffectResolve::lanes`): given a LIVE
// instance (its activation window is open and its envelope above zero -- the builder has checked),
// its owner's drawn view and the seconds since its window opened (after its delay), write its
// contribution. Return false for "nothing this frame" (a degenerate owner, a zero amount). The
// envelope is applied afterwards by the builder, so a type never implements fading. `local` is a
// function of the transport second only (ADR-091), so a pulse's phase is the same played or scrubbed.
using EntityLanesHook = bool (*)(const EffectInstance&, const EffectContext&, const NodeView&, double local,
                                 EntityLaneContribution&);

// §7's rules, `into` = `into` then `c`. The band is NOT folded here: it is exclusive, and the
// builder decides the conflict (it needs to name the effect holding the block).
void foldEntityLanes(EntityLaneContribution& into, const EntityLaneContribution& c);
// Fades a contribution by its activation envelope (0 = neutral, 1 = as authored). Every term moves
// towards its neutral value: a gain towards 1, a tint towards white, sums towards 0.
void applyEntityEnvelope(EntityLaneContribution& c, float envelope);
// The record for a folded contribution. `index` is the record's own position, written to `fxA.w`.
[[nodiscard]] EntityFxRecord packEntityFx(const EntityLaneContribution& c, std::uint32_t index);
// The extension record (index + 1): the pattern sub-blocks (Bioluminescence, Pulsing Veins, hue,
// rim light) of a contribution whose surface flags include `kFxExt`.
[[nodiscard]] EntityFxRecord packEntityFxExt(const EntityLaneContribution& c);
// Writes the owner frame (`frame`, `shape`, `radialInv`, `hasFrame`) from the owner's drawn view.
void setEntityFxFrame(EntityLaneContribution& c, const NodeView& view);
// Where the owner frame puts a world point: q, in about [-1, 1]. The CPU twin of the shader's.
[[nodiscard]] glm::vec3 entityFxOwnerSpace(const EntityLaneContribution& c, const glm::vec3& world);
// The clip threshold for a hidden fraction (0 nothing .. 1 everything), for a clip mode and its
// breakup and edge: chosen so that 0 discards nothing and draws no edge, and 1 discards all of it.
[[nodiscard]] float entityFxClipThreshold(EntityFxClipMode mode, float hidden, float edgeWidth, float breakup);
// Worley's F1 and F2 (the distances to the nearest and second-nearest jittered cell points) and a
// hash of the nearest cell in [0, 1): the CPU twin of `worleyF1F2` in shaders/noise.wgsl, whose F1
// is `voronoiF1`'s (same lattice, same jitter). F2 - F1 is small exactly on the cell edges.
[[nodiscard]] glm::vec3 worleyF1F2(const glm::vec3& p, std::uint32_t seed);
// The sub-block flags a contribution's Wave 2 terms set (a subset of kFxClip .. kFxRimLight, plus
// kFxExt when any extension block is live). A block whose amount is zero sets nothing.
[[nodiscard]] std::uint32_t entityFxSurfaceFlags(const EntityLaneContribution& c);

// ---- the frame block -----------------------------------------------------------------------------

struct EntityFxFrame {
    // Record 0 is neutral when there are any records at all; empty means no FXL effect is live, and
    // the renderer uploads nothing and writes zero lanes.
    std::vector<EntityFxRecord> records;
    // Scene entity index -> record index (0 = no effect). Sized to the highest affected entity + 1.
    std::vector<std::uint32_t> entityRecord;
    // `scene.procedurals` index -> record (0 = no effect): a procedural owner (Glowmere's `visitor`
    // saucer, a hero cap) draws through the procedural renderer, and every instance of each of its
    // material parts takes the owner's record. The same records, a second address space.
    std::vector<std::uint32_t> proceduralRecord;
    // LIGHTMOD's pool this frame (Glow spills). Nested here rather than a second scene field; when
    // a Light- or World-owned client lands it becomes the Lighting stage's own block.
    EffectLightFrame lights;
    std::uint32_t dropped = 0;      // instances this builder dropped (budget or band conflict)

    [[nodiscard]] bool empty() const { return records.size() <= 1; }
    [[nodiscard]] std::uint32_t recordFor(std::size_t entity) const {
        return entity < entityRecord.size() ? entityRecord[entity] : 0u;
    }
    [[nodiscard]] std::uint32_t recordForProcedural(std::size_t procedural) const {
        return procedural < proceduralRecord.size() ? proceduralRecord[procedural] : 0u;
    }
    // Empties the frame but keeps every vector's storage, so a steady frame allocates nothing.
    void clear();
};

// How many records ONE live instance contributes in `ctx` (0 or 1), through the same code the
// builder runs: the conformance probe's `resolve.records` hook for every EntityLanes type.
[[nodiscard]] std::size_t entityLaneRecords(const EffectInstance& e, const EffectContext& ctx);

// The builder (RenderStage::Material for the lanes; the spill lights it requests are the Lighting
// stage's). Writes the status of every EntityLanes instance: Disabled, Dormant, Drawn, Dropped
// (budget, band conflict, an owner that draws nothing) or Partial (its spill light did not fit),
// with a reason for the last two.
void buildEntityFxFrame(std::span<const EffectInstance> effects, const EffectContext& ctx, EntityFxFrame& out,
                        std::span<const std::uint32_t> order, std::span<EffectStatus> status,
                        std::span<std::string> reasons);

} // namespace avgen::world
