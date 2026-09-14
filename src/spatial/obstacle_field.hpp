#pragma once

// Navigation-relevant solids, and the questions a walker asks about them (ADR-093, §5).
//
// This is deliberately *not* a physics world. There are no bodies, no contacts, no integration and
// no callbacks: an obstacle is a vertical cylinder with a type and a blocking flag, and the only
// things asked of a set of them are "is this disc clear", "how far to the nearest solid", "does
// this segment cross one" and "push me out". That is the entire vocabulary a ground walker needs,
// and it costs a uniform grid and some arithmetic rather than a simulation.
//
// Why a cylinder rather than the asset's bounds. A fourteen-metre tree has a five-metre bounding
// radius and a half-metre trunk. Recording the bounds would close a forest to anything that walks;
// recording the trunk is both cheaper and correct, because the part of a tree a walker collides
// with is the part at its own height. `height` is therefore the height of the *solid*, not of the
// thing -- a canopy is not an obstacle, it is weather.
//
// Why this lives in spatial/ rather than entity/ or world/. Both of those need it -- the walker
// asks whether it may stand somewhere, and the terrain query surface (§3) has to answer
// `isOccupied(x, z, radius)` with the same facts -- and neither may include the other's headers
// without a cycle. This file includes glm and the standard library and nothing else, so anything
// in the project may hold one.
//
// Everything here is a pure function of the obstacles and the arguments, and every traversal runs
// in insertion order: the same set always answers the same way, on any machine, in any build.

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace avgen::spatial {

// What kind of thing is in the way. The type is not policy -- whether a walker may pass is decided
// by `blocking` and by the filter it queries with -- but it is what lets a diagnostic say "a rock"
// instead of "obstacle 4471", and what lets a future behaviour treat a creature differently from a
// boulder without a second field.
enum class ObstacleType : std::uint8_t {
    Vegetation, // growth: passable at walker scale unless it is large enough to have a stem
    Trunk,      // a tree: a narrow solid under a wide canopy
    Rock,
    Structure,  // built, monumental or authored geometry
    Creature,   // another character
    Custom,
};
[[nodiscard]] const char* obstacleTypeName(ObstacleType type);

// What getting past a solid takes, decided by the solid rather than by whoever is looking at it.
//
// `ObstacleType` above is *identity* and stays that way -- it is what lets a diagnostic say "a
// rock". This is *policy about the thing*: a 0.3 m rock is stepped over and a 3 m rock is walked
// round, and that is true of the rock however it is being asked. What is deliberately **not**
// decided here is what a particular body can do about the class -- a walker with no jump clears
// nothing, a deer clears a metre -- because that is a property of the body and it lives in
// `ObstacleFilter`.
//
// The split is the whole point of the class existing. Before it, the only thing resembling
// traversal was `ObstacleFilter::stepOver`, a float on the *query*: the field could say how tall a
// solid was and nothing at all about what kind of effort it took, so every obstacle a body could
// not step over was the same obstacle. One field now serves a walker and a jumper because the
// obstacle carries half the answer and the filter carries the other half.
enum class Traversal : std::uint8_t {
    Passable, // never in the way: recorded because it is navigation-relevant, not because it blocks
    StepOver, // low enough that a body of ordinary proportions steps onto or over it
    Jumpable, // clearable by a body that can jump, and a wall to one that cannot
    Blocking, // go round
};
[[nodiscard]] const char* traversalName(Traversal traversal);

// One solid. Positions are world space; `center` is XZ and `base` the world y of its foot, so an
// obstacle on a hillside is at the height of the hillside rather than at zero.
struct NavigationObstacle {
    glm::vec2 center{0.0f};
    float radius = 0.5f;  // the solid footprint, metres
    float base = 0.0f;    // world y of the foot
    float height = 1.0f;  // metres of solid above `base`
    ObstacleType type = ObstacleType::Custom;
    // What it takes to get past this one. `Passable` is the old `blocking = false`: something
    // recorded because it is navigation-relevant and not because it is in the way -- a glowing
    // plant a character walks to and through -- and it is invisible to every query unless the
    // filter asks for it. `Blocking` is the default because a solid nobody classified is a solid,
    // which is the safe end of the mistake.
    Traversal traversal = Traversal::Blocking;

    // Whether this is a solid at all, which is the only thing the plan-view queries can ask: they
    // take a radius and nothing else, so they cannot know a body's legs. Kept as a name rather
    // than open-coding the comparison in five places.
    [[nodiscard]] bool blocking() const { return traversal != Traversal::Passable; }
};

// What a particular walker counts as in its way. The field stores facts; the filter is policy, and
// it belongs to the thing doing the walking -- a deer and a cart disagree about a boulder.
struct ObstacleFilter {
    float bodyRadius = 0.45f; // added to every obstacle's radius
    float stepOver = 0.35f;   // solids shorter than this are stepped over rather than avoided
    // Where the walker's own body is, vertically. A solid whose top is below `footY + stepOver` is
    // stepped over; one whose base is above `footY + headHeight` is ducked under. Leave `headHeight`
    // at 0 to ignore the vertical extent entirely, which is what a plan-view query wants.
    float footY = 0.0f;
    float headHeight = 0.0f;
    // Metres of solid this body can clear by jumping or vaulting. Only `Traversal::Jumpable`
    // solids are eligible, and only up to this height.
    //
    // **0 -- the default -- is a body that cannot jump**, and it is what makes this addition inert
    // for everything that existed before it: `height <= 0` is false for every real obstacle, so a
    // walker sees a jumpable solid exactly as it saw it when there was no such class.
    float jumpOver = 0.0f;
    bool includeNonBlocking = false;
};

// What `filter`'s body must do about `o`. `Passable` means nothing at all: it is not in the way.
//
// The order of the tests is the physical one, and it is deliberate. A thing low enough to stand on
// is stepped on whatever it is made of, and a thing whose foot is above a body's head is ducked
// under whatever it is made of; the obstacle's own class only gets a say after those, and only to
// decide whether a jump is an option. That is why a nominal `StepOver` still blocks a body whose
// own step is lower than the thing is tall -- the class is what the solid is, not a promise to
// every body that asks.
[[nodiscard]] Traversal traversalFor(const NavigationObstacle& o, const ObstacleFilter& filter);

// One obstacle in the way, and how far in.
struct ObstacleHit {
    std::uint32_t index = 0;
    float penetration = 0.0f; // metres the query disc overlaps the obstacle's, >= 0
};

class ObstacleField {
public:
    ObstacleField() = default;

    void clear();
    void reserve(std::size_t count);
    void add(const NavigationObstacle& obstacle);
    // Bins the obstacles for querying. Adding invalidates the index; queries then fall back to a
    // linear scan, which is correct and slow rather than fast and wrong. Call it once, after the
    // last `add`. Nothing here mutates on query, so a built field may be shared across threads.
    void build(float cellSize = 0.0f);
    [[nodiscard]] bool built() const { return indexed_; }

    [[nodiscard]] bool empty() const { return obstacles_.empty(); }
    [[nodiscard]] std::size_t size() const { return obstacles_.size(); }
    [[nodiscard]] std::span<const NavigationObstacle> obstacles() const { return obstacles_; }
    [[nodiscard]] float cellSize() const { return cellSize_; }
    // How many obstacles the index holds. Equal to `size()` for a built field: one entry each.
    [[nodiscard]] std::size_t indexEntries() const { return entries_.size(); }
    [[nodiscard]] std::size_t blockingCount() const { return blocking_; }

    // ---- the query surface ---------------------------------------------------------------------

    // §3's seam. True when any blocking obstacle's footprint overlaps the disc of `radius` at
    // (x, z). Deliberately the plainest possible signature: it is the one the terrain query surface
    // forwards to, and a shared query with an opinionated argument list is a query nobody shares.
    [[nodiscard]] bool isOccupied(float x, float z, float radius) const;

    // The same test under a walker's own policy. Returns the deepest overlap, or an index past the
    // end when nothing is in the way.
    [[nodiscard]] bool blocker(glm::vec2 p, const ObstacleFilter& filter, ObstacleHit& out) const;

    // "What can this body get past here", answered with what it would have to *do* rather than
    // with a boolean. The hardest thing the disc at `p` overlaps: `Blocking` if anything must be
    // walked round, else `Jumpable` if anything must be vaulted, else `StepOver` if anything must
    // be stepped on, else `Passable` -- which is also the answer when the disc overlaps nothing.
    //
    // `out` describes whichever obstacle produced the answer, so a caller told `Jumpable` knows
    // what it is jumping. Ties within a class go to the deepest overlap and then to the lowest
    // index, the same rule `blocker` uses, so the grid never decides the answer.
    Traversal traversalAt(glm::vec2 p, const ObstacleFilter& filter, ObstacleHit& out) const;

    // Metres of open ground between the walker's rim and the nearest blocking solid, clamped to
    // `maxRange`. Negative when the walker is already inside one. This is what steering wants: a
    // boolean says "blocked", a distance says "blocked, and by how much".
    [[nodiscard]] float clearance(glm::vec2 p, const ObstacleFilter& filter,
                                  float maxRange = 12.0f) const;

    // Whether a straight walk from `a` to `b` crosses a blocking solid. Exact rather than sampled:
    // a point test every couple of metres walks straight through a tree trunk, which is the bug
    // this whole file exists to fix.
    [[nodiscard]] bool segmentBlocked(glm::vec2 a, glm::vec2 b, const ObstacleFilter& filter) const;
    // The first blocking solid along the segment, by distance from `a`.
    [[nodiscard]] bool segmentHit(glm::vec2 a, glm::vec2 b, const ObstacleFilter& filter,
                                  ObstacleHit& out) const;

    // The shortest XZ displacement that takes a disc at `p` out of everything it overlaps. Zero
    // when it overlaps nothing. Summed over the overlaps in index order, so it is reproducible
    // even where three obstacles meet and no single answer is obviously right.
    [[nodiscard]] glm::vec2 resolve(glm::vec2 p, const ObstacleFilter& filter) const;

    // Every obstacle whose footprint overlaps the disc, in index order. `out` is cleared first.
    void query(glm::vec2 p, float radius, std::vector<std::uint32_t>& out,
               bool includeNonBlocking = false) const;

    // Changes whenever a query could change. For tests and for a cache that has to know the world
    // moved under it.
    [[nodiscard]] std::uint64_t contentHash() const;

private:
    [[nodiscard]] glm::ivec2 cellOf(glm::vec2 p) const;
    // Calls `fn(index)` exactly once for every obstacle whose centre falls in the cells the box
    // covers. Callers pad the box by `maxRadius_`, which makes that an exact superset of the
    // obstacles that could overlap -- and makes every query duplicate-free, which `resolve`
    // depends on because it sums.
    template <typename F> void forEachNear(glm::vec2 lo, glm::vec2 hi, F&& fn) const;

    std::vector<NavigationObstacle> obstacles_;
    std::size_t blocking_ = 0;
    float maxRadius_ = 0.0f;

    // A uniform grid over the obstacles' own bounds, as a counting-sorted CSR: `starts_` indexes
    // `entries_`, which holds obstacle indices. Rebuilt wholesale, never patched, because an
    // obstacle set is built once per world and queried a great many times.
    bool indexed_ = false;
    float cellSize_ = 0.0f;
    glm::ivec2 origin_{0};
    glm::ivec2 dims_{0};
    std::vector<std::uint32_t> starts_;
    std::vector<std::uint32_t> entries_;
};

template <typename F>
void ObstacleField::forEachNear(glm::vec2 lo, glm::vec2 hi, F&& fn) const {
    if (!indexed_) {
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(obstacles_.size()); ++i) {
            fn(i);
        }
        return;
    }
    const glm::ivec2 a = cellOf(lo);
    const glm::ivec2 b = cellOf(hi);
    const int x0 = std::max(a.x, 0);
    const int x1 = std::min(b.x, dims_.x - 1);
    const int y0 = std::max(a.y, 0);
    const int y1 = std::min(b.y, dims_.y - 1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const std::size_t cell = static_cast<std::size_t>(y) * static_cast<std::size_t>(dims_.x) +
                                     static_cast<std::size_t>(x);
            for (std::uint32_t e = starts_[cell]; e < starts_[cell + 1]; ++e) {
                fn(entries_[e]);
            }
        }
    }
}

} // namespace avgen::spatial
