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
//   6..15 reserved for later sub-blocks (pattern, clip, displace, hue, hologram)
enum EntityFxLaneIndex : std::uint32_t {
    kFxLaneA = 0,
    kFxLaneB = 1,
    kFxLaneAdd = 2,
    kFxLaneRim = 3,
    kFxLaneBandAxis = 4,
    kFxLaneBand = 5,
};

// Bits of `fxA.z`. Zero is "no effect on this draw" and the shader returns before any FXL work.
enum EntityFxFlag : std::uint32_t {
    kFxOn = 1u << 0,        // the inline lanes (gain, tint) apply
    kFxRecord = 1u << 1,    // read lanes 2 and 3 from the record (added emission, rim)
    kFxBand = 1u << 2,      // read lanes 4 and 5 (the travelling band)
    kFxBloomShare = 1u << 3 // raise the emission target by the bloom share
};

// The record budget. 1 MiB of records (4,096 owners) -- one record per affected OWNER, shared by
// every entity in its range, so this is not a per-entity number and no scene is near it. It exists
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
