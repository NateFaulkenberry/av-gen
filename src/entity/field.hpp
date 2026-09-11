#pragma once

// Trigger volumes and music influence fields (ADR-097).
//
// Two things live here and they are deliberately the same thing seen twice.
//
//   * A **TriggerVolume** is a region -- sphere, box or capsule -- that answers one question about
//     a point: *how far inside am I*, as a number between 1 at the centre and 0 at the surface.
//     It is not a boolean. A boolean volume is what produces a crowd that snaps on at a line in
//     the air, and the whole reason this file exists is that binary in/out is the wrong answer.
//
//   * A **MusicField** is a TriggerVolume with a strength and a reaction arc. It does not carry a
//     second audio analysis, a second modulation graph or a second reactivity system: an entity
//     already declares `reactions`, those already compile to ordinary `params::ModRoute`s, and a
//     field's only job is to **scale the depth of the reactions the entity already has**. The same
//     `audio.bass -> emissiveBoost` an author wrote for a static prop becomes spatial for free.
//
// A field is also a *publisher*: `signals::SignalBus::declare` takes a name at runtime, so a field
// exposes `field.<name>.occupancy`, `field.<name>.enter` and `field.<name>.exit` on the ordinary
// bus. That is how a light, a material or a particle system reacts to a volume without any of them
// learning what a volume is -- they route from a signal, which is the only reactivity path there
// has ever been here.
//
// ## Determinism (ADR-091)
//
// A field's purity follows its source. A field with no source, or one that follows a node the
// timeline drives, is a pure function of time: scrub-safe and offline-exact. A field that follows
// an entity with behaviours is not, because that entity accumulates. `FieldAuthority` records
// which one an author actually has, it is reported by name at install, and `requireScrubExact`
// turns the impure case into a stated problem instead of a difference somebody finds by rendering
// the same frame twice.

#include "core/error.hpp"
#include "core/rng.hpp"
#include "signals/signal_bus.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::entity {

// ---- the region ------------------------------------------------------------------------------

enum class VolumeShape : std::uint8_t { Sphere, Box, Capsule };
[[nodiscard]] const char* volumeShapeName(VolumeShape shape);
[[nodiscard]] bool volumeShapeFromName(std::string_view name, VolumeShape& out);

// How influence decays from the inner edge to the surface. `Constant` is the honest spelling of
// binary in/out: it exists because a *trigger* legitimately wants a hard edge (a doorway, a stage
// boundary), and it is not the default because a *field* never does.
enum class Falloff : std::uint8_t { Constant, Linear, Smooth, InverseSquare };
[[nodiscard]] const char* falloffName(Falloff falloff);
[[nodiscard]] bool falloffFromName(std::string_view name, Falloff& out);

struct TriggerVolume {
    VolumeShape shape = VolumeShape::Sphere;
    glm::vec3 center{0.0f};
    float radius = 1.0f;             // Sphere and Capsule
    float height = 0.0f;             // Capsule: the length of the segment between the cap centres
    glm::vec3 halfExtents{1.0f};     // Box
    glm::vec3 rotation{0.0f};        // Euler degrees; orients the Box and the Capsule's axis
    // The fraction of the way out at which influence is still exactly 1. 0 means the centre alone
    // is full strength; 0.5 means the inner half is a plateau. Clamped below 1.
    float inner = 0.0f;
    Falloff falloff = Falloff::Smooth;

    // The largest distance from `center` any point of the volume reaches. What the broad phase
    // uses, and what makes a capsule's bounding box right without unpacking its orientation twice.
    [[nodiscard]] float reach() const;
    // 1 at the centre, 0 at the surface and everywhere outside, smooth between.
    //
    // **A point exactly on the surface is outside.** Influence there is exactly 0 and `contains`
    // is false. A half-open rule is the only one that makes an enter edge and an exit edge
    // agree at the same point rather than depending on which side the floating point landed.
    [[nodiscard]] float influenceAt(glm::vec3 point) const;
    [[nodiscard]] bool contains(glm::vec3 point) const { return influenceAt(point) > 0.0f; }
    // Axis-aligned bounds, for the broad phase.
    void bounds(glm::vec3& lo, glm::vec3& hi) const;
};

[[nodiscard]] float applyFalloff(Falloff falloff, float x); // x: 1 at the inner edge, 0 at the surface

// ---- the arc (§21) ---------------------------------------------------------------------------
//
// What crossing into a field does to a character. The contract the addendum states in one line:
// `Walking -> Dance -> Walking`, never `Walking -> Dance -> Idle`. This is enforced structurally
// rather than by remembering to restore something -- the arc *overrides* an activity for a while
// and then stops overriding it, and the behaviours underneath were never reset, so what comes back
// is whatever they are doing now.
//
// The jitters are what stop a crowd from being a chorus line. Each is drawn from a stream seeded
// from the entity's own seed, the field's name and how many times this entity has entered this
// field -- never from the entity's behaviour stream, which advances at a rate that depends on how
// many frames have been drawn.
struct ReactionArc {
    bool enabled = false;
    float delaySeconds = 0.0f;       // before the reaction starts
    float delayJitter = 0.0f;        // +- seconds, per entity
    float holdSeconds = 4.0f;        // how long it is held
    float holdJitter = 0.0f;         // +- seconds, per entity
    float releaseSeconds = 0.6f;     // fade back to whatever was happening before
    float intensity = 1.0f;          // the reaction level at full hold
    float intensityJitter = 0.0f;    // +- fraction of `intensity`, per entity
    float refractorySeconds = 0.0f;  // the shortest gap between two arcs on one entity
    // The activity held for the duration, by name ("react", "observe", "idle", ...). The clip it
    // plays is the entity's own `clips` mapping: a behaviour never names a clip and neither does
    // a field, because a clip name belongs to an asset.
    std::string activity = "react";
    // Hold the body still while the arc runs. The travel and the speed are *restored*, not zeroed
    // at the source, so the behaviour driving them keeps its destination and resumes toward it.
    bool holdStill = false;
};

// ---- the field -------------------------------------------------------------------------------

// Which determinism guarantee this field actually has (ADR-091). Derived, never authored.
enum class FieldAuthority : std::uint8_t {
    Static, // no source: the volume is where the scene put it. Pure.
    Baked,  // follows a node, or an entity with no behaviours: position comes from tracks. Pure.
    Live,   // follows an entity that integrates. Not scrub-exact, and said so at install.
};
[[nodiscard]] const char* fieldAuthorityName(FieldAuthority authority);

struct FieldDesc {
    std::string name;
    TriggerVolume volume;
    // The entity, node or hero whose position the volume follows. Empty: the volume stays where
    // `volume.center` puts it.
    std::string source;
    float strength = 1.0f;   // gain at full influence; >1 boosts the reactions it governs
    float floorGain = 0.0f;  // gain outside the volume, for a field that never fully lets go
    // Which entities this field governs. Empty matches every entity. An entity matches when the
    // tag is one of its own `tags` or the name of the profile it was built from -- so "every NPC
    // built from the dancer profile" is a tag, not a list of names.
    std::vector<std::string> tags;
    // §19: scale the depth of the governed entities' existing reactions. Off makes this a pure
    // trigger volume that publishes signals and fires an arc without touching modulation.
    bool scaleReactions = true;
    // ADR-091: refuse to resolve to a live source in silence. With this set, a field that ends up
    // following something stateful is a problem reported by name at install.
    bool requireScrubExact = false;
    ReactionArc arc;
    bool enabled = true;
};

// What a field resolved to this frame. Rebuilt by the field pass; read by the editor and the log.
struct FieldRuntime {
    FieldAuthority authority = FieldAuthority::Static;
    glm::vec3 center{0.0f};      // the resolved centre, after following `source`
    std::size_t inside = 0;      // governed entities strictly inside this frame
    std::size_t governed = 0;    // entities whose tags this field matches at all
    float maxInfluence = 0.0f;   // the deepest any governed entity is, 0..1
    bool sourceResolved = true;  // false when `source` named nothing; the field is then disabled
    signals::SignalId occupancy = signals::kInvalidSignal;
    signals::SignalId enterSignal = signals::kInvalidSignal;
    signals::SignalId exitSignal = signals::kInvalidSignal;
};

// One edge, for whoever wants edges rather than levels (§17's event dispatch, when it lands).
struct TriggerEvent {
    std::uint32_t field = 0;
    std::uint32_t entity = 0;
    bool enter = false;
    double time = 0.0;
    float influence = 0.0f; // the influence at the frame the edge was detected
};

// ---- the broad phase -------------------------------------------------------------------------
//
// A uniform grid over the entity positions, counting-sorted into two flat arrays. Rebuilt every
// frame because entities move, and every frame is O(n) with no allocation once the arrays have
// grown: the alternative -- testing every entity against every volume -- is the O(entities x
// volumes) the brief forbids, and it is the one that stops being affordable exactly when a scene
// gets interesting.
class EntityGrid {
public:
    // `cellSize` is a hint; it is inflated if the point cloud would need more than `maxCells`.
    void build(std::span<const glm::vec3> points, float cellSize, std::size_t maxCells = 1u << 16u);
    void clear();

    // Calls fn(index) for every point in a cell the box touches. Points *near* the box, not in it:
    // this is the broad phase, and the caller does the exact test.
    template <typename F>
    void forEachNear(const glm::vec3& lo, const glm::vec3& hi, F&& fn) const {
        if (points_ == 0) {
            return;
        }
        const glm::ivec3 a = cellOf(lo);
        const glm::ivec3 b = cellOf(hi);
        for (int z = a.z; z <= b.z; ++z) {
            for (int y = a.y; y <= b.y; ++y) {
                for (int x = a.x; x <= b.x; ++x) {
                    const std::size_t cell = static_cast<std::size_t>((z * dims_.y + y) * dims_.x + x);
                    for (std::size_t i = starts_[cell]; i < starts_[cell + 1]; ++i) {
                        fn(indices_[i]);
                    }
                }
            }
        }
    }

    [[nodiscard]] std::size_t cellCount() const { return starts_.empty() ? 0 : starts_.size() - 1; }
    [[nodiscard]] std::size_t pointCount() const { return points_; }
    [[nodiscard]] float cellSize() const { return cellSize_; }

private:
    [[nodiscard]] glm::ivec3 cellOf(const glm::vec3& p) const;

    glm::vec3 origin_{0.0f};
    glm::ivec3 dims_{1};
    float cellSize_ = 1.0f;
    float invCell_ = 1.0f;
    std::size_t points_ = 0;
    std::vector<std::size_t> starts_;   // cellCount + 1 prefix sums
    std::vector<std::uint32_t> indices_; // point indices, grouped by cell
    // Scratch, kept between builds. The engine counts allocations per frame, and a rebuild that
    // allocated two vectors every time would show up there as the field pass's cost even though
    // the arrays it needs are the same size every frame.
    std::vector<std::size_t> counts_;
    std::vector<std::size_t> cellOfPoint_;
    std::vector<std::size_t> cursor_;
};

// ---- seeded per-entity variation -------------------------------------------------------------

// A stream for (entity seed, field name, how many times this entity has entered this field). Pure:
// the same entity entering the same field for the second time always draws the same numbers,
// whatever the frame rate, whatever else has run.
[[nodiscard]] Rng arcStream(std::uint32_t entitySeed, std::string_view fieldName, std::uint32_t count);

// ---- serialisation ---------------------------------------------------------------------------
//
// The `fields` array of an avgen-scene document, a sibling of `entities`.
//
//     { "name": "stage", "shape": "sphere", "center": [0, 0, -12], "radius": 14,
//       "falloff": "smooth", "inner": 0.25, "strength": 1.4, "tags": ["dancer"],
//       "arc": { "enabled": true, "holdSeconds": 6, "holdJitter": 1.5, "delayJitter": 0.8 } }
[[nodiscard]] Result<FieldDesc> fieldFromJson(const nlohmann::json& j);
[[nodiscard]] Result<std::vector<FieldDesc>> fieldsFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json fieldToJson(const FieldDesc& field);
[[nodiscard]] nlohmann::json fieldsToJson(const std::vector<FieldDesc>& fields);

} // namespace avgen::entity
