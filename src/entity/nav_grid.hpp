#pragma once

// The discrete half of navigation (ADR-093, §2).
//
// §2 offers three shapes: a navmesh, a terrain-aware navigation grid, or a hybrid where terrain
// gives continuous height and a separate structure gives walkability and pathfinding. This is the
// hybrid, and the split is the point:
//
//   * **Continuous**, and already built: `WorldMap::sample` answers height, slope, normal and water
//     analytically, anywhere, exactly. Baking that into a mesh or a grid would be a second
//     description of the same ground, stale the moment a hill moved, and no more accurate than the
//     function it was baked from. `Navigator` keeps asking the world.
//   * **Discrete**, and what was missing: *reachability*. Straight-line steering with a fan of
//     deviations cannot get round a lake, a ridge or a thicket -- it finds every local way blocked
//     and gives up, which is exactly what Glowmere's walker did. Answering "can I get there, and
//     which way" needs a graph, and a coarse grid over walkability is the cheapest honest one.
//
// A navmesh was rejected for a specific reason rather than on taste. Building one means triangulating
// the walkable set, which means deciding the walkable set *once*, at bake time -- and this world's
// terrain is regenerated from a seed, its obstacles come from an ecology pass, and an editor is
// being built alongside this that moves both. A grid can be rebuilt in a few tens of milliseconds
// and is correct by construction; a navmesh would be a build step nobody remembered to run.
//
// The grid is deliberately coarse (metres, not centimetres) and deliberately *soft*: obstacles
// contribute a per-cell cost rather than closing a cell outright, so A* prefers open ground and
// routes around a thicket while still allowing a path between two trees that the fine steering
// layer will actually thread. A grid fine enough to resolve a tree trunk would be a hundred times
// the memory to answer a question the continuous layer already answers better.
//
// Everything here is a pure function of the navigator and the arguments. A* breaks ties on cell
// index, so the same world and the same request always produce the same path, on any machine.

#include "entity/navigation.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace avgen::entity {

enum NavCellFlag : std::uint8_t {
    NavWalkable = 1u << 0,  // a walker may stand here
    NavWater = 1u << 1,     // the surface here is under water
    NavSteep = 1u << 2,     // rejected for slope: a bank or a cliff
    NavBlocked = 1u << 3,   // rejected for a solid: a rock, a hero, a thicket of trunks
    NavEdge = 1u << 4,      // walkable, and orthogonally adjacent to something that is not
};

// One cell. Eight bytes, so a 640 m world at four-metre cells is 190 KB -- small enough that keeping
// it resident is cheaper than deciding when to, and small enough that A*'s inner loop walks two
// cells per cache line.
//
// Region labels live in a parallel array rather than in here. They are read twice per path request
// and never inside the search, so putting them in the cell would grow it to twelve bytes and make
// every expansion pay for a field the expansion does not use.
struct NavCell {
    float ground = 0.0f;          // world y of the surface at the cell's centre
    std::uint8_t flags = 0;
    std::uint8_t slope = 0;       // 0..255 over the navigator's 0..1 slope range
    std::uint8_t obstruction = 0; // 0..255: how much of the cell blocking solids cover
    // 0..255: how much of the cell is covered by solids this world's walker gets past only by
    // vaulting (ADR-194). Its own number rather than a share of `obstruction`, because a vault is
    // neither open ground nor a wall and A* has to price it as a third thing. Zero everywhere for a
    // body that cannot jump -- which is every body this project ships -- so the cost term it feeds
    // is exactly zero and no existing path moves. It fits in the byte that was `pad`.
    std::uint8_t vault = 0;
};

struct NavGridStats {
    int width = 0;
    int height = 0;
    float cellSize = 0.0f;
    std::size_t cells = 0;
    std::size_t walkable = 0;
    std::size_t water = 0;
    std::size_t blocked = 0;
    // How many connected regions the walkable set falls into, and how big the largest is. A world
    // whose ground is one region is a world a character can cross; one with forty is an archipelago,
    // and an author usually wants to know that before a character is stuck on an island in it.
    std::size_t regions = 0;
    std::size_t largestRegion = 0;
    double buildMs = 0.0;
};

// Why a path request did not produce a route. A reason rather than `false`, because an action layer
// has different things to do about each of these: a goal that has become unstandable wants a new
// goal, an unreachable one wants a different *kind* of goal, and an exhausted search wants trying
// again from somewhere else. §6 asks that a character not freeze forever when its destination
// becomes unavailable, and it cannot avoid freezing if all it is told is "no".
enum class PathStatus : std::uint8_t {
    Ok,              // `waypoints` is a route
    AlreadyThere,    // the goal is where the mover already is
    NoGraph,         // this world has no navigation graph; only a straight line could be checked
    NoStart,         // the mover is not standing anywhere the graph recognises, and nothing near is
    NoGoal,          // the destination is not standable and nothing near it is either
    Unreachable,     // both ends are fine, and no route connects them -- a different region
    SearchExhausted, // a route may exist; the expansion budget ran out before it was found
};
[[nodiscard]] const char* pathStatusName(PathStatus status);

struct PathRequest {
    glm::vec2 from{0.0f};
    glm::vec2 to{0.0f};
    // How far from `to` still counts as arriving. A character with a body does not need to stand on
    // the exact point, and insisting it does is how a goal one metre inside a rock becomes a goal
    // nothing can satisfy.
    float goalTolerance = 0.0f;
};

struct PathResult {
    PathStatus status = PathStatus::NoGraph;
    // The route after `from`, ending at `goal`. Empty on every failure.
    std::vector<glm::vec2> waypoints;
    // Where the route actually ends. Not always `to`: a destination inside a solid or under water
    // is answered with the nearest place a body could stand, and a caller that assumed otherwise
    // would walk a character into the thing it was heading for.
    glm::vec2 goal{0.0f};
    float length = 0.0f;        // metres along the polyline
    std::size_t expansions = 0; // cells the search opened; what this request cost

    [[nodiscard]] bool ok() const {
        return status == PathStatus::Ok || status == PathStatus::AlreadyThere;
    }
};

// How a path is scored. Distance is not the only thing a character minimises -- one that took the
// shortest line would cross every scree slope and thread every thicket, which is efficient and
// reads as a robot.
struct NavPathCost {
    float obstructionPenalty = 3.0f; // multiplies distance through a fully obstructed cell
    // What a vault costs, over a cell entirely covered by solids this body clears by jumping
    // (ADR-194). Lower than `obstructionPenalty` because the cell *is* passable -- the route
    // exists and the character takes it -- and above zero because clearing a boulder is not the
    // same as walking round nothing: a path that priced it at zero would send a character
    // hurdling a field of rocks to save two metres.
    //
    // Half of `obstructionPenalty` is a starting point and not a measurement. It cannot be
    // measured on this project's content yet, because nothing in it can jump; what it is worth
    // will be settled the first time something can, against a route a viewer watches.
    float vaultPenalty = 1.5f;
    float slopePenalty = 1.6f;       // multiplies distance up a maximally steep walkable cell
    int maxExpansions = 24000;       // give up rather than search a whole world for an island
};

class NavGrid {
public:
    NavGrid() = default;

    // Samples the world once per cell. Deterministic and reasonably expensive (it is one
    // `Navigator::sample` per cell), so it is called explicitly rather than lazily: a structure
    // that builds itself on first use builds itself at the least predictable moment.
    void build(const Navigator& nav, float cellSize = 4.0f);
    void clear();

    [[nodiscard]] bool valid() const { return !cells_.empty(); }
    [[nodiscard]] const NavGridStats& stats() const { return stats_; }
    // How many cells the last `findPath` expanded. A diagnostic, and the number to watch when a
    // world gets bigger: it is what a path costs.
    [[nodiscard]] std::size_t lastExpansions() const { return lastExpansions_; }
    [[nodiscard]] float cellSize() const { return stats_.cellSize; }
    [[nodiscard]] std::span<const NavCell> cells() const { return cells_; }

    [[nodiscard]] glm::ivec2 cellOf(glm::vec2 p) const;
    [[nodiscard]] glm::vec2 centerOf(glm::ivec2 c) const;
    [[nodiscard]] bool inside(glm::ivec2 c) const;
    [[nodiscard]] const NavCell& at(glm::ivec2 c) const;
    [[nodiscard]] bool walkable(glm::ivec2 c) const;
    [[nodiscard]] bool walkable(glm::vec2 p) const;

    // The nearest walkable cell centre to `p` within `maxRange` metres, by a spiral outward so the
    // answer is the closest one and not merely a close one.
    [[nodiscard]] bool nearestWalkable(glm::vec2 p, float maxRange, glm::vec2& out) const;

    // Which connected region `p` is in; 0 when it is not walkable. Flood-filled once at build time
    // with exactly the connectivity A* uses, so "different regions" and "A* will not find a route"
    // are the same statement rather than two that can disagree.
    [[nodiscard]] std::uint16_t regionAt(glm::vec2 p) const;
    // Whether two points are reachable from each other at all. A lookup, not a search.
    [[nodiscard]] bool connected(glm::vec2 a, glm::vec2 b) const;
    // How many cells a region holds. Region 0 is the unwalkable set.
    [[nodiscard]] std::size_t regionSize(std::uint16_t region) const;

    // A* from `from` to `to`, string-pulled. `out` is the waypoints *after* `from`, ending at `to`
    // when it is walkable and at the nearest walkable cell to it otherwise. False when no route
    // exists inside the expansion budget -- which a caller must treat as "choose somewhere else",
    // never as "walk at it anyway".
    [[nodiscard]] bool findPath(glm::vec2 from, glm::vec2 to, std::vector<glm::vec2>& out,
                                const NavPathCost& cost = {}) const;
    // The same search, with a reason when it does not find one.
    [[nodiscard]] PathResult path(const PathRequest& request, const NavPathCost& cost = {}) const;

    // Places worth going to that the grid can see for free while it is being built (§6). Cheaper
    // and more honest than a second pass over the world: the shore is where a walkable cell meets
    // a wet one, and a vista is a walkable local maximum.
    [[nodiscard]] std::span<const glm::vec3> shorePoints() const { return shore_; }
    [[nodiscard]] std::span<const glm::vec3> vistaPoints() const { return vistas_; }

private:
    [[nodiscard]] std::size_t index(glm::ivec2 c) const;
    // Whether the straight line between two cells stays walkable, walked over the grid rather than
    // over the world. Cheap by design: it is the inner loop of the string pull, and the continuous
    // layer re-checks whatever survives.
    [[nodiscard]] bool lineOfSight(glm::ivec2 a, glm::ivec2 b) const;
    void extractInterestPoints();
    void buildRegions();

    std::vector<NavCell> cells_;
    glm::vec2 origin_{0.0f}; // world XZ of cell (0,0)'s lower corner
    NavGridStats stats_{};
    std::vector<glm::vec3> shore_;
    std::vector<glm::vec3> vistas_;

    // Scratch for A*, sized once at build. Kept here rather than allocated per query because a
    // behaviour asks for a path every few seconds per character and allocating a 25,000-element
    // vector each time is a cost nobody can see and everybody pays. Mutable, and guarded by the
    // fact that entity updates are single-threaded; `findPath` is documented as not reentrant.
    mutable std::vector<float> gScore_;
    mutable std::vector<std::int32_t> cameFrom_;
    mutable std::vector<std::uint32_t> visitStamp_;
    mutable std::uint32_t stamp_ = 0;
    mutable std::vector<glm::ivec2> cellPath_;
    mutable std::size_t lastExpansions_ = 0;
    // Which connected region each cell belongs to; 0 when it is not walkable at all. Two cells with
    // different non-zero regions are both standable and *not reachable from each other*, and that is
    // a fact no amount of searching should have to rediscover.
    std::vector<std::uint16_t> regions_;
    std::vector<std::size_t> regionSizes_; // indexed by region id; [0] is the unwalkable set
    mutable std::vector<std::int32_t> floodStack_;
};

} // namespace avgen::entity
