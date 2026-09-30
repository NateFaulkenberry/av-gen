#pragma once

// ADR-1042: the journey -- a camera (and anything anchored to it) that walks an effectively infinite,
// repeating world. Pure maths, no parameters: the composition owns the parameters and calls this.
//
// The world repeats by a screw S (the SDF `screw` node's transform, ADR-1040): a translation T
// (count 0), or a turn of 360/n degrees about +Y in the atan2(z, x) direction plus a rise T.y
// (count n). The path is authored as control points in cell 0; the point after the last is the first
// carried into cell 1 by S, so the path continues forever with a continuous tangent across the seam.
// Points are joined by a centripetal Catmull-Rom spline (no cusps through uneven spacing) and
// parameterised by arc length.
//
// The wrap: after `wrapCells` cells (n for a helix, 1 for a translation, so that S^wrapCells is a pure
// translation) the camera is carried back by S^wrapCells. Because the world is invariant under S, the
// frame it sees is the same frame, and the camera never leaves the first few cells: lights and props
// placed there are never outrun, and float precision never degrades.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace avgen::scene {

struct JourneyScrew {
    glm::vec3 translation{0.0f}; // count 0: the cell step; count n: translation.y is the rise per cell
    int count = 0;               // 0 = a translation; n > 0 = a helix of n cells per turn about +Y

    // S^k applied to a point / a direction.
    [[nodiscard]] glm::vec3 applyPoint(const glm::vec3& p, int k) const;
    [[nodiscard]] glm::vec3 applyDirection(const glm::vec3& d, int k) const;
};

struct JourneySettings {
    std::vector<glm::vec3> path; // control points in cell 0 (at least 2)
    JourneyScrew screw;
    int wrapCells = 0;           // 0 = automatic (count for a helix, 1 for a translation)
    std::string collide;         // the SDF node the collision guard queries ("" = none)
    float radius = 0.35f;        // the guard's clearance, metres

    [[nodiscard]] nlohmann::json toJson() const;
    static Result<JourneySettings> fromJson(const nlohmann::json& j);
};

struct JourneySample {
    glm::vec3 position{0.0f};
    glm::vec3 tangent{1.0f, 0.0f, 0.0f}; // unit
};

class JourneyPath {
public:
    // Fails when there are fewer than two points, the cell is degenerate (the path does not advance)
    // or wrapCells is not a whole number of turns.
    static Result<JourneyPath> build(const JourneySettings& settings);

    [[nodiscard]] const JourneySettings& settings() const { return settings_; }
    [[nodiscard]] float cellLength() const { return cellLength_; } // arc length of one cell
    [[nodiscard]] int wrapCells() const { return wrapCells_; }
    [[nodiscard]] double wrapLength() const { return static_cast<double>(cellLength_) * wrapCells_; }

    // The path at arc length s (any real s; cell k = floor(s / cellLength) is carried by S^k).
    [[nodiscard]] JourneySample sample(double s) const;
    // How many whole wraps `s` is past the origin: floor(s / wrapLength).
    [[nodiscard]] long long wraps(double s) const;
    // The same path point seen after the wrap: sample(s - wraps(reference) * wrapLength). Use one
    // reference (the camera's distance) for everything drawn in a frame, so they wrap together.
    [[nodiscard]] JourneySample sampleWrapped(double s, double reference) const;

private:
    struct Segment {
        glm::vec3 p0, p1, p2, p3; // Catmull-Rom control points (the segment runs p1 -> p2)
        float t0 = 0, t1 = 0, t2 = 0, t3 = 0; // centripetal knots
    };
    [[nodiscard]] glm::vec3 segmentPoint(const Segment& seg, float u) const;
    [[nodiscard]] glm::vec3 local(double u) const; // u in [0, cellLength]

    JourneySettings settings_;
    std::vector<Segment> segments_;
    // Arc-length table: at sample i, the cumulative length and (segment, u).
    std::vector<float> tableS_;
    std::vector<float> tableU_; // segment index + u in [0, 1)
    float cellLength_ = 0.0f;
    int wrapCells_ = 1;
};

// A camera's placement on the journey: where it stands and where it looks, before any guard.
struct JourneyView {
    double distance = 0.0;   // metres along the path
    float lookAhead = 4.0f;  // metres ahead the aim is taken from
    float height = 1.6f;     // eye height above the path
    float yawDegrees = 0.0f;   // look-around relative to the path (positive turns left, about +Y)
    float pitchDegrees = 0.0f; // positive looks up
    float bob = 0.0f;        // walk bob amplitude, metres
    float stride = 1.4f;     // metres per bob cycle
    float swayDegrees = 0.0f; // amplitude of a slow drift of the aim (the searching gaze)
    float swayRate = 0.07f;   // Hz
    double time = 0.0;        // seconds (the sway's clock)
};

struct JourneyPose {
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f, 0.0f, -1.0f};
};

// The eye at distance (wrapped with the camera), its aim along the path turned by yaw/pitch and the sway,
// and the walk bob. A pure function of its arguments.
[[nodiscard]] JourneyPose journeyPose(const JourneyPath& path, const JourneyView& view);

} // namespace avgen::scene
