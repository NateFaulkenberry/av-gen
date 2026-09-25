#pragma once

// BOLT: seeded fractal electric paths (Effect Library Wave 3, shared-infrastructure.md "BOLT";
// catalog-energy.md). The CPU generator behind Lightning, Arc, Electric Field and Discharge.
//
// **The shape.** Midpoint displacement with branches [R13]: the main channel runs between its two
// endpoints and is refined `depth` times, each refinement pushing every segment's midpoint sideways
// (in the plane across the segment) by about `jaggedness` times the segment's length -- so each
// level is half the size of the one above it, which is the scale hierarchy "random segments" lack.
// Branches leave the channel at its coarse vertices with a probability that falls towards the far
// end, deviate 20-60 degrees from the local direction, and are themselves smaller bolts: their
// length and intensity fall by `branchDecay` per generation.
//
// **Canonical space.** A path is generated ONCE per key in a canonical frame -- the main channel from
// (0,0,0) to (0,0,1) -- and placed in the world by a similarity transform (`BoltPlacement`). So an
// arc whose endpoints move every frame re-uses the same cached shape instead of regenerating it, and
// a strike's shape does not depend on where its target happened to be sampled.
//
// **Determinism.** Every random number is a hash of (seed, index, path, level, vertex, channel)
// through the engine's own `noise::pcg3d` -- never an `std::` distribution, whose output is
// implementation-defined -- and there is no sequential generator state. Two consequences:
//   * the same key gives the same path bit for bit, however often and in whatever order it is asked;
//   * lowering `depth` (level of detail) is a PREFIX of the full refinement: the coarse vertices of a
//     depth-4 bolt are exactly those of the depth-8 bolt with the same key, and branches spawn only at
//     those coarse vertices, so a bolt that shrinks on screen loses detail, not its shape. (While the
//     vertex cap is not what limits the branches: when it binds, a finer bolt fits fewer of them.)
// The arithmetic is plain IEEE float (+, *, /, sqrt); no libm transcendental is on the path.
//
// **Limits.** At most `kBoltMaxVertices` (512) vertices and `kBoltMaxBranches` branches per path; a
// branch that would not fit is not generated (the main channel always fits: 2^8 + 1 = 257).
//
// **Cache.** `BoltCache` is a small LRU keyed by (seed, index, the shape parameters). Its slots keep
// their storage, so once warm a frame allocates nothing. It is a memo, not state: a miss regenerates
// exactly what a hit returns.
//
// **Drawing.** `appendBoltStrips` writes a placed path into RIBBON as TWO strips -- a wide Gaussian
// glow and a narrow hot core, each clamped to at least 1.5 px on screen by the ribbon's vertex stage
// -- with every branch in the same strip, joined by zero-opacity bridges, so a whole bolt is two
// draws however many branches it has.

#include "world/effects/ribbon_frame.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace avgen::world {

inline constexpr std::uint32_t kBoltMaxVertices = 512;
inline constexpr int kBoltMaxDepth = 8;
inline constexpr std::uint32_t kBoltMaxBranches = 24;
inline constexpr int kBoltMaxGenerations = 3;

struct BoltParams {
    int depth = 6;                  // main-channel refinements: 2^depth segments (clamped 1..8)
    float jaggedness = 0.22f;       // sideways offset of a midpoint, as a fraction of its segment
    float branchProbability = 0.4f; // chance a coarse vertex of the channel throws a branch (0..1)
    float branchDecay = 0.5f;       // a branch's length and intensity relative to its parent's
    int generations = 2;            // how many levels of branches of branches (0 = none, max 3)
    std::uint32_t maxVertices = kBoltMaxVertices;
};

struct BoltVertex {
    glm::vec3 position{0.0f}; // canonical space: the main channel runs (0,0,0) -> (0,0,1)
    float s = 0.0f;           // arc length from the root of the bolt, canonical units
    float intensity = 1.0f;   // 1 on the main channel, falling per generation and along a branch
    float width = 1.0f;       // 1 on the main channel, thinner on branches, tapering to the tip
};

struct BoltBranch {
    std::uint32_t first = 0;  // into BoltPath::vertices
    std::uint32_t count = 0;
    std::uint32_t generation = 0; // 0 = the main channel
    std::uint32_t parent = 0;     // the path it leaves (itself for the main channel)
    std::uint32_t root = 0;       // the vertex of the parent it leaves from (0 for the main channel)
};

struct BoltPath {
    std::vector<BoltVertex> vertices;
    std::vector<BoltBranch> paths; // [0] is the main channel
    float maxS = 0.0f;             // the largest arc length: a leader reaching this has fully grown
    [[nodiscard]] std::span<const BoltVertex> path(std::size_t i) const {
        return {vertices.data() + paths[i].first, paths[i].count};
    }
};

// The shape for (params, seed, index). Pure: writes only `out`, and allocates only while `out` grows
// to its first-ever size.
void generateBolt(const BoltParams& params, std::uint64_t seed, std::uint32_t index, BoltPath& out);

// The main channel of `a` and `b` (same depth) blended so the displacement from the chord keeps its
// size -- a plain lerp would make the half-way arc straighter than either end -- and the branches of
// each kept with their intensity scaled by (1 - f) and f. What makes an Arc writhe instead of pop
// between two re-seeds.
void blendBolts(const BoltPath& a, const BoltPath& b, float f, BoltPath& out);

// A small LRU of generated paths. `get` returns a reference that stays valid until `capacity` other
// keys have been asked for. Thread-confined: `boltCache()` is one per thread.
class BoltCache {
public:
    static constexpr std::size_t kCapacity = 48;
    const BoltPath& get(const BoltParams& params, std::uint64_t seed, std::uint32_t index);
    [[nodiscard]] std::uint64_t hits() const { return hits_; }
    [[nodiscard]] std::uint64_t misses() const { return misses_; }
    void clear();

private:
    struct Slot {
        std::uint64_t seed = 0;
        std::uint32_t index = 0;
        std::uint32_t paramsKey = 0;
        std::uint64_t lastUse = 0;
        bool used = false;
        BoltParams params;
        BoltPath path;
    };
    std::array<Slot, kCapacity> slots_{};
    std::uint64_t clock_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
};
[[nodiscard]] BoltCache& boltCache();

// The key a parameter set is cached under (every field, bit for bit).
[[nodiscard]] std::uint32_t boltParamsKey(const BoltParams& params);

// ---- placing and drawing ------------------------------------------------------------------------

// Where a canonical path goes: its root at `start`, its main channel's end at `end`, and `bow` added
// as a parabola along the chord (4u(1-u) * bow at chord fraction u) -- an arc's sag, or a hop bulging
// off a surface. The roll about the chord is the minimal rotation from +Z, which is continuous as the
// endpoints move (no flip when the chord passes the vertical).
struct BoltPlacement {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f, 0.0f, -1.0f};
    glm::vec3 bow{0.0f};
    [[nodiscard]] float length() const { return glm::length(end - start); }
    // The rotation taking canonical +Z to the chord's direction (computed once per placement).
    [[nodiscard]] glm::mat3 rotation() const;
    [[nodiscard]] glm::vec3 place(const glm::vec3& canonical, const glm::mat3& rotation) const;
    [[nodiscard]] glm::vec3 place(const glm::vec3& canonical) const { return place(canonical, rotation()); }
};

// A world-space depth that keeps the finest segments of a bolt `length` metres long, seen from
// `distance` metres, at a few pixels or more on screen: the authored depth, lowered for a bolt that
// is small in the frame (never below 2).
[[nodiscard]] int boltLodDepth(int authored, float length, float distance);

struct BoltLook {
    glm::vec3 core{0.0f};  // HDR radiance of the hot core (the white-hot channel)
    glm::vec3 glow{0.0f};  // HDR radiance of the glow around it
    float coreWidth = 0.1f; // metres, full width, on the main channel
    float glowWidth = 1.5f; // metres, full width, on the main channel
    float opacity = 1.0f;   // the envelope: everything scales with it
    float branchGain = 1.0f; // extra scale on every branch (restrikes follow the main channel only)
    float reveal = 1e30f;   // arc-length cutoff, canonical units: only s <= reveal is drawn (a leader)
    // The glow follows every n-th vertex: soft light has no business kinking, and a camera-facing
    // strip wider than its segments folds over itself at every sharp turn. 0 (the default) picks n
    // so the glow's segments are about as long as it is wide.
    int glowStride = 0;
    // Where the camera is. A camera-facing strip seen down its own length is a flat slab as wide as
    // the glow, not a line, so the glow fades where the channel runs towards the eye (the core, a
    // hair wide, does not need to), and is held to about 55 px across. Without an eye, neither.
    glm::vec3 eye{0.0f};
    bool hasEye = false;
};

enum class BoltFit : std::uint8_t {
    Written,  // both strips as asked
    CoreOnly, // the core fitted and the glow did not (the strip budget ran out between them)
    NoRoom,   // not written: the vertex budget or the strip budget has no room for it
    Nothing, // nothing to draw (the reveal has not started, zero opacity, degenerate placement)
};

// How many ribbon vertices `appendBoltStrips` would write for `path` (both strips, bridges included).
[[nodiscard]] std::uint32_t boltStripVertices(const BoltPath& path, int glowStride);

// Writes `path`, placed, as the core strip then the glow strip (additive). When the vertex budget
// has no room for both, nothing is written; the core goes first so that a strip budget running out
// between the two loses the glow, not the bolt. `scratch` is the caller's, kept across frames.
BoltFit appendBoltStrips(RibbonSink& sink, const BoltPath& path, const BoltPlacement& placement,
                         const BoltLook& look, std::vector<RibbonPoint>& scratch);

// Short straight streaks (sparks) as ONE additive strip, bridged like a bolt's branches. Each streak
// is `tail -> head` with its own radiance and opacity. Returns false (writing nothing) when there is
// no room.
struct BoltStreak {
    glm::vec3 tail{0.0f};
    glm::vec3 head{0.0f};
    glm::vec3 color{0.0f};
    float width = 0.05f;
    float opacity = 1.0f;
};
BoltFit appendStreaks(RibbonSink& sink, std::span<const BoltStreak> streaks, std::vector<RibbonPoint>& scratch);

// ---- hashing, shared by the bolt types ----------------------------------------------------------

// A uniform in [0, 1) from a key of five integers, through `noise::pcg3d`. The bolt types' one source
// of randomness, so every choice they make is a function of its key and nothing else.
[[nodiscard]] float boltHash(std::uint64_t seed, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                             std::uint32_t channel);
// An event's index: the event's time on the transport, quantised to 1/960 s. A strike keyed by its
// own instant is the same strike whichever way the transport reached it.
[[nodiscard]] std::uint32_t boltEventIndex(double t0);

} // namespace avgen::world
