#pragma once

// ADR-2223: a navigated crowd -- a lean, fixed-step population, not an `Entity` per agent.
//
// Why not entities. An `explore` character costs about 89 us a step and "exceeds the whole frame
// somewhere around 180" (docs/character-ai-research.md E.1); a crowd agent has to cost a few. So an
// agent here is a row in a set of arrays (SoA) with exactly what walking somewhere needs: a place,
// a velocity, a goal, a corridor, a gait and a clip phase. Perception, decision and behaviours stay
// with the characters that need them. The animation is the existing one: the gait (`entity::Gait`)
// picks the state and the rate from the speed, and the composition pushes it to the rig.
//
// **Determinism (ADR-2225).** The state at timeline second t is the state after `floor(t * 60)`
// steps of a fixed 1/60 step, a pure function of (t, the crowd block, the navigation surface):
//   * every step is Jacobi -- each agent reads only the previous step -- with neighbours in a fixed
//     (distance, index) order, so any thread count gives the same bits;
//   * all amortisation is counted, never timed: `pathsPerStep` queries a step, in agent order from a
//     cursor that is itself state;
//   * all randomness is a hash of (group seed, schedule entry, member), never a stream.
// So a play, a seek and a range render are the same simulation; `advanceTo` only decides how much of
// it to run. Seeks restore the nearest checkpoint at or before the target (taken every second of
// film, as ADR-700 does for entities) and replay forward. The drawn pose between two grid instants
// is extrapolated from the earlier one and never fed back.
//
// The simulation never reads the view mode (115 rule: Draft never changes simulation).
#include "crowd/crowd_desc.hpp"
#include "crowd/orca.hpp"
#include "entity/gait.hpp"
#include "entity/locomotion.hpp"
#include "nav/nav_surface.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace avgen::crowd {

inline constexpr int kStepsPerSecond = 60;

enum class AgentMode : std::uint8_t {
    Idle,        // standing where it is: no command yet, or held
    Departing,   // commanded, waiting out its seeded stagger
    Moving,      // walking its corridor
    Arrived,     // at its slot (Goto), standing
    Dwelling,    // at a wander point, waiting to pick the next
    Unreachable, // its goal could not be routed to; standing, reported
};
[[nodiscard]] const char* agentModeName(AgentMode mode);

struct AgentInit {
    std::string node;
    int group = 0;
    glm::vec2 position{0.0f}; // XZ
    float yaw = 0.0f;         // radians about +Y, forward = (sin, cos)
};

// Everything that changes. A checkpoint is a copy of this and nothing else.
struct CrowdState {
    std::int64_t step = 0;
    std::vector<glm::vec2> pos, vel, goal, face, anchor; // anchor: where a mover was at anchorStep
    std::vector<float> y, yaw, cruise, slowFor;
    std::vector<AgentMode> mode;
    std::vector<std::uint8_t> hasFace, needPath;
    std::vector<std::int64_t> departAt, dwellUntil, anchorStep;
    std::vector<std::int32_t> command; // the schedule entry driving each agent, -1 none
    std::vector<std::uint32_t> wanderCount;
    std::vector<std::vector<glm::vec2>> path;
    std::vector<std::uint16_t> cursor;
    std::vector<nav::NavStatus> pathStatus;
    std::vector<entity::Gait> gait;
    std::vector<entity::Activity> activity;
    std::vector<std::int64_t> activityStep;
    std::vector<double> clipPhase; // clip seconds of the current activity's clip at this step
    std::vector<float> clipRate;
    std::size_t scheduleCursor = 0;
    std::size_t pathCursor = 0;
    std::uint64_t replans = 0;
};

// What the composition draws for one agent at a timeline second.
struct AgentPose {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    entity::Activity activity = entity::Activity::Idle;
    const std::string* clip = nullptr; // the group's clip for the activity; null when none
    double clipOrigin = 0.0;           // timeline second at which the clip's second 0 falls
    float clipRate = 1.0f;
    double activityStart = 0.0;        // when the activity began (the cross-fade's start)
};

struct StepStats {
    double microseconds = 0.0; // thread CPU of the last step
    std::uint32_t pathQueries = 0;
    std::uint32_t pathExpansions = 0;
};

class CrowdWorld {
public:
    CrowdWorld() = default;

    // Replaces everything. `surface` is borrowed and must outlive the world (null: agents stand).
    void configure(CrowdDesc desc, std::vector<AgentInit> agents, const nav::INavSurface* surface);
    void clear();

    [[nodiscard]] bool empty() const { return agents_.empty(); }
    [[nodiscard]] std::size_t size() const { return agents_.size(); }
    [[nodiscard]] const CrowdDesc& desc() const { return desc_; }
    [[nodiscard]] const std::vector<AgentInit>& agents() const { return agents_; }
    [[nodiscard]] const CrowdState& state() const { return state_; }
    [[nodiscard]] std::int64_t step() const { return state_.step; }

    // Runs the simulation to the last 1/60 instant at or before `seconds`: forward from where it is,
    // or from the nearest checkpoint at or before the target. Returns the steps it ran.
    std::int64_t advanceTo(double seconds);
    // The same, to an exact step.
    std::int64_t advanceToStep(std::int64_t step);

    // The drawn pose of agent `i` at `seconds` (>= the current step's instant; extrapolated from it).
    [[nodiscard]] AgentPose pose(std::size_t i, double seconds) const;

    // A hash of every agent's state, bit for bit: the determinism tests compare these.
    [[nodiscard]] std::uint64_t stateHash() const;
    // What the checkpoints were taken under: the block, the agents, the surface.
    [[nodiscard]] std::uint64_t inputKey() const { return inputKey_; }

    [[nodiscard]] const StepStats& lastStep() const { return lastStep_; }
    [[nodiscard]] std::size_t checkpointCount() const { return checkpoints_.size(); }
    [[nodiscard]] std::size_t checkpointBytes() const;
    // Test control: whether checkpoints are taken and used (off: every backward seek replays from 0).
    void setCheckpoints(bool on) { checkpointsOn_ = on; }
    void setThreads(int threads) { threads_ = threads < 1 ? 1 : threads; }

    // Static walls as the avoidance sees them (segments), for the debug view.
    [[nodiscard]] const std::vector<OrcaSegment>& walls() const { return walls_; }

private:
    void reset();
    void stepOnce();
    void applySchedule(std::int64_t k);
    void assignCommand(std::size_t scheduleIndex, std::int64_t k);
    void servePaths();
    [[nodiscard]] glm::vec2 clearOfStatics(glm::vec2 p, float body) const;
    [[nodiscard]] glm::vec2 preferredVelocity(std::size_t i);
    void recordCheckpoint();

    CrowdDesc desc_;
    std::vector<AgentInit> agents_;
    std::vector<std::vector<std::size_t>> members_; // per group, agent indices in scene order
    std::vector<std::int64_t> scheduleStep_;         // per entry, the step it applies on
    const nav::INavSurface* surface_ = nullptr;
    std::vector<OrcaSegment> walls_;
    CrowdState state_;
    std::map<std::int64_t, CrowdState> checkpoints_;
    std::uint64_t inputKey_ = 0;
    bool checkpointsOn_ = true;
    int threads_ = 1;
    StepStats lastStep_;
};

} // namespace avgen::crowd
