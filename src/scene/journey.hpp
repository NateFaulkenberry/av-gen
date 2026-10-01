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
    // A world point the gaze eases towards (the beacon): the aim is the path's look turned towards it by
    // `lookAtWeight` (0 = the path's look, 1 = straight at the point). The point is in the journey's
    // unwrapped frame (as if the camera never wrapped), so it holds still across a wrap.
    glm::vec3 lookAt{0.0f};
    float lookAtWeight = 0.0f;
};

struct JourneyPose {
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f, 0.0f, -1.0f};
};

// The eye at distance (wrapped with the camera), its aim along the path turned by yaw/pitch and the sway,
// and the walk bob. A pure function of its arguments.
[[nodiscard]] JourneyPose journeyPose(const JourneyPath& path, const JourneyView& view);

// ADR-1042 (chapters): one continuous journey through several worlds. Chapter c owns the global
// distances [start_c, start_{c+1}); inside it the camera walks the chapter's own path at local distance
// `from + (d - start)`, wrapped by the chapter's own screw, and everything is carried into the world by the
// chapter frame (world = rotY(yaw) * local + offset, the composition-node convention, so the chapter's SDF
// nodes take the same position and rotation). At a chapter's start the camera jumps from the last
// chapter's world to this one: author both so the view is the same there (an identical threshold -- a
// doorway flooded with light -- whose geometry matches in both), and nothing else can tell.
// Nodes and lights a chapter lists are shown only while it is active (the others do not march).
struct JourneyChapter {
    std::string name;
    double start = 0.0;          // the global distance where the chapter begins
    double from = 0.0;           // the chapter path's own distance at `start`
    JourneySettings world;       // its path, screw, wrap, collide object and radius
    glm::vec3 offset{0.0f};      // the chapter frame
    float yawDegrees = 0.0f;
    std::vector<std::string> nodes;
    std::vector<std::string> lights;

    [[nodiscard]] glm::vec3 toWorldPoint(const glm::vec3& p) const;
    [[nodiscard]] glm::vec3 toWorldDirection(const glm::vec3& d) const;
};

class Journey {
public:
    // The journey block: {"chapters": [ {chapter}, ... ]} (sorted by start), or one chapter's fields at the
    // top level (`path`, `screw`, ...), which is a single chapter starting at 0.
    static Result<Journey> fromJson(const nlohmann::json& j);
    [[nodiscard]] nlohmann::json toJson() const;

    [[nodiscard]] std::size_t size() const { return chapters_.size(); }
    [[nodiscard]] const JourneyChapter& chapter(std::size_t i) const { return chapters_[i]; }
    [[nodiscard]] const JourneyPath& path(std::size_t i) const { return paths_[i]; }
    // The chapter a global distance falls in (the first one before its start).
    [[nodiscard]] std::size_t chapterAt(double distance) const;
    [[nodiscard]] double localDistance(std::size_t chapter, double distance) const {
        return chapters_[chapter].from + (distance - chapters_[chapter].start);
    }
    // The camera: `view.distance` is global.
    [[nodiscard]] JourneyPose pose(JourneyView view) const;
    // A point riding the journey at `distance`, in the world, wrapped with the camera at `camera` when they
    // share a chapter.
    [[nodiscard]] JourneySample anchor(double distance, double camera) const;
    // False for a node or light another chapter owns while the camera is not in it.
    [[nodiscard]] bool nodeActive(const std::string& name, double camera) const;
    [[nodiscard]] bool lightActive(const std::string& id, double camera) const;

private:
    std::vector<JourneyChapter> chapters_;
    std::vector<JourneyPath> paths_;
    bool singleForm_ = false;
};

} // namespace avgen::scene
