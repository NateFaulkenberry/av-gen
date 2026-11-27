#pragma once

// ADR-2225: what a scene says about its navigated crowd -- groups, authored walls and the
// time-keyed schedule -- as document data (the scene file's top-level `crowd` block).
//
// **Commands are data keyed by time, never live calls.** "At 12 s, group `visitors` goes to the
// plaza" is a fact about the piece, so the crowd at any second is a pure function of (that second,
// this block, the navigation surface), and a seek, a range render and a play from zero all agree
// (115 decision C3). A command arriving from outside during playback would make the crowd a
// function of when somebody clicked; the Session tools (after API level 3) therefore edit this
// block, they do not push commands into the simulation.
//
// **One crowd concept (115 decision O2).** A group is named like an ADR-2117 listening crowd and
// its members are exactly the nodes whose `animation.crowd` names it, in scene order, so a navigated
// agent is also a listening member: its head can turn to the Lantern while its feet walk.
#include "core/error.hpp"
#include "entity/gait.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace avgen::crowd {

// An authored wall: a polyline with a thickness, which the navigation surface sees as a chain of
// solids (the obstacle field) and the avoidance sees as discs. The lab's bottlenecks are these.
struct WallDesc {
    std::vector<glm::vec2> points; // XZ
    float thickness = 0.3f;
    float height = 2.5f;
    float base = 0.0f; // world y of its foot
};

struct AreaDesc {
    glm::vec2 center{0.0f};
    glm::vec2 halfExtents{1.0f};
};

enum class CommandKind : std::uint8_t {
    Goto,   // walk to a destination slot and stop there
    Wander, // walk to seeded points inside an area, dwelling between them
    Hold,   // stop where you are
};
[[nodiscard]] const char* commandKindName(CommandKind kind);

// How a group's members are assigned slots in a destination.
enum class SlotPolicy : std::uint8_t {
    Spread, // a jittered lattice over the area (or the points, in member order)
    Index,  // member m takes points[m % n]
};

struct ScheduleEntry {
    double at = 0.0; // timeline seconds; applied on the first 1/60 step at or after it
    std::string group;
    CommandKind command = CommandKind::Goto;
    std::vector<glm::vec2> points;
    std::optional<AreaDesc> area;
    SlotPolicy policy = SlotPolicy::Spread;
    float arriveRadius = 0.35f;
    std::optional<glm::vec2> face;  // where an arrived member looks
    float stagger = 0.0f;           // seconds over which departures are spread (seeded per member)
    float dwellMin = 2.0f;          // Wander: seconds at each point
    float dwellMax = 5.0f;
};

struct GroupDesc {
    std::string name;
    float radius = 0.3f;
    float speedMin = 1.2f;  // cruise speeds are drawn from this range...
    float speedMax = 1.4f;
    int speedBuckets = 3;   // ...quantised to this many values (ADR-2116 pose sharing, ADR-2226)
    float maxSpeed = 1.8f;  // what avoidance may use to get out of the way
    float neighbourDist = 3.0f;
    int maxNeighbours = 10;
    float timeHorizon = 2.0f;
    float turnRate = 6.0f;  // rad/s the body's facing may change at
    std::uint32_t seed = 1;
    entity::GaitSettings gait{};
    std::string idleClip = "Idle";
    std::string walkClip = "Walk";
    std::string runClip = "Run";
};

struct CrowdDesc {
    std::vector<WallDesc> walls;
    std::vector<GroupDesc> groups;
    std::vector<ScheduleEntry> schedule;
    int pathsPerStep = 8; // path queries served per step, a COUNT (never a time budget: ADR-2223)
    int threads = 1;      // Jacobi avoidance threads; any value gives the same bits
    [[nodiscard]] bool empty() const { return groups.empty() && walls.empty(); }
};

[[nodiscard]] Result<CrowdDesc> crowdFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json crowdToJson(const CrowdDesc& desc);

} // namespace avgen::crowd
