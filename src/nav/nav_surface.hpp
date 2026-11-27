#pragma once

// ADR-2220: the navigation surface a crowd walks on, behind one interface.
//
// Two worlds want two representations, and both are already decided:
//
//   * **Heightfield worlds** (Glowmere, Musical Geography, every lab on a `terrain` node) have one
//     walkable surface per XZ point and an analytic ground. `entity::NavGrid` (ADR-093/268/295)
//     answers them, and `GridNavSurface` presents it here. Nothing about the grid changes.
//   * **Authored multi-level geometry** (Neon Dreams: decks over streets, roofs, gangways) needs a
//     navigation mesh. That backend is **pending the owner's approval of recastnavigation** (115
//     decision C2): until then it is not in the build at all -- no CPM entry, no fetch, no stub
//     that pretends -- and `NavBackend::NavMesh` exists only so the choice is named in one place.
//
// The crowd (`crowd::CrowdWorld`) asks only what is below, so the second backend arrives without
// the crowd changing. Everything here is const, allocation-light and a pure function of the
// surface and the arguments: the same question gives the same answer, which is what a seek replay
// needs (ADR-2225).
#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace avgen::nav {

enum class NavBackend : std::uint8_t {
    Grid,    // entity::NavGrid over a heightfield world (built)
    NavMesh, // a tiled voxel navmesh over authored geometry (pending approval, ADR-2220; not built)
};
[[nodiscard]] const char* navBackendName(NavBackend backend);

// Why a path request did or did not produce a route. The same reasons `entity::PathStatus` gives,
// so a crowd agent's state can say *why* it is stuck rather than only that it is.
enum class NavStatus : std::uint8_t { Ok, AlreadyThere, NoSurface, NoStart, NoGoal, Unreachable, Exhausted };
[[nodiscard]] const char* navStatusName(NavStatus status);

struct NavPath {
    NavStatus status = NavStatus::NoSurface;
    std::vector<glm::vec2> points; // XZ waypoints after the start, ending at the goal it reached
    glm::vec2 goal{0.0f};          // where the route actually ends (a goal inside a solid is moved)
    float length = 0.0f;
    std::uint32_t expansions = 0;  // what the request cost, in the backend's own unit
    [[nodiscard]] bool ok() const { return status == NavStatus::Ok || status == NavStatus::AlreadyThere; }
};

// A static solid a body steers round: a disc (a trunk, a post, one link of an authored wall).
struct NavDisc {
    glm::vec2 center{0.0f};
    float radius = 0.0f;
};

// The edge of the walkable set, as a segment a body must not cross (a cliff, water, the world's edge).
struct NavSegment {
    glm::vec2 a{0.0f};
    glm::vec2 b{0.0f};
};

class INavSurface {
public:
    virtual ~INavSurface() = default;
    [[nodiscard]] virtual NavBackend backend() const = 0;
    [[nodiscard]] virtual bool valid() const = 0;
    // A route from `from` to `to` (XZ). `tolerance`: how near the goal counts as arriving.
    [[nodiscard]] virtual NavPath findPath(glm::vec2 from, glm::vec2 to, float tolerance) const = 0;
    // The ground under `p`; false when there is none.
    [[nodiscard]] virtual bool ground(glm::vec2 p, float& y) const = 0;
    // The static solids and walkable-set edges within `range` of `p`, appended in a fixed order.
    virtual void staticObstacles(glm::vec2 p, float range, std::vector<NavDisc>& discs,
                                 std::vector<NavSegment>& segments) const = 0;
    // Whether a body of `radius` may walk the straight line a -> b (a corridor shortcut).
    [[nodiscard]] virtual bool lineClear(glm::vec2 a, glm::vec2 b, float radius) const = 0;
    // A hash of everything the answers above depend on. A crowd's checkpoints are keyed by it.
    [[nodiscard]] virtual std::uint64_t contentKey() const = 0;
};

} // namespace avgen::nav
