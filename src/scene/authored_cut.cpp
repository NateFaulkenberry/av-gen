#include "scene/authored_cut.hpp"

#include <algorithm>
#include <string>

// ADR-947. See the header for the rule; this is its arithmetic.

namespace avgen::scene {
namespace {

// The shot that has the frame at `seconds`, as `resolveActiveCamera` picks it with no events: the
// last one containing the instant whose camera exists.
const CameraShot* shotAt(const CameraDirection& direction, double seconds) {
    const CameraShot* chosen = nullptr;
    for (const CameraShot& shot : direction.shots) {
        if (shot.contains(seconds) && direction.find(shot.camera) != nullptr) {
            chosen = &shot;
        }
    }
    return chosen;
}

// One stretch of the timeline with one live camera and one shot (or the default camera).
struct Segment {
    double start = 0.0;
    double end = 0.0;
    CameraId camera = kMainCamera;
    const CameraShot* shot = nullptr; // null: the default camera has the frame
    std::string subject;
};

void place(world::ShotSpan& span, const SubjectLocator& locate) {
    if (!locate || span.subject.empty()) {
        return;
    }
    glm::vec3 at{0.0f};
    float radius = span.subjectRadius;
    if (locate(span.subject, at, radius)) {
        span.subjectPosition = at;
        span.subjectRadius = radius;
    }
}

} // namespace

std::string_view cameraSubject(const CameraRig& rig) {
    if (rig.id == kMainCamera) {
        return {};
    }
    if (!rig.aimNode.empty()) {
        return rig.aimNode;
    }
    return rig.followNode;
}

std::string_view shotSubject(const CameraDirection& direction, const CameraShot& shot) {
    if (!shot.subject.empty()) {
        return shot.subject;
    }
    const CameraRig* rig = direction.find(shot.camera);
    return rig != nullptr ? cameraSubject(*rig) : std::string_view{};
}

std::vector<world::ShotSpan> authoredShotSpans(const CameraDirection& direction, const SubjectLocator& locate) {
    std::vector<world::ShotSpan> spans;
    if (direction.shots.empty()) {
        return spans;
    }
    // Every instant the answer can change at, from the start of the piece to the last shot's end.
    // After that the default camera holds the frame without end, which a span cannot say.
    std::vector<double> cuts{0.0};
    for (const CameraShot& shot : direction.shots) {
        if (direction.find(shot.camera) == nullptr || !(shot.endSeconds > shot.startSeconds)) {
            continue;
        }
        cuts.push_back(std::max(0.0, shot.startSeconds));
        cuts.push_back(std::max(0.0, shot.endSeconds));
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

    const CameraId fallback = direction.defaultCamera != kNoCamera && direction.find(direction.defaultCamera) != nullptr
                                  ? direction.defaultCamera
                                  : kMainCamera;
    std::vector<Segment> segments;
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const double a = cuts[i];
        const double b = cuts[i + 1];
        const CameraShot* shot = shotAt(direction, 0.5 * (a + b));
        // The same shot on both sides of a boundary (another shot's edge inside it) is one stretch.
        if (!segments.empty() && segments.back().shot == shot && segments.back().end == a) {
            segments.back().end = b;
            continue;
        }
        Segment s;
        s.start = a;
        s.end = b;
        s.shot = shot;
        s.camera = shot != nullptr ? shot->camera : fallback;
        if (shot != nullptr) {
            s.subject = std::string(shotSubject(direction, *shot));
        } else if (const CameraRig* rig = direction.find(fallback)) {
            s.subject = std::string(cameraSubject(*rig));
        }
        segments.push_back(std::move(s));
    }

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const Segment& s = segments[i];
        // The cut into this stretch, when it changes the camera and hands off to somebody.
        if (i > 0 && segments[i - 1].camera != s.camera && !s.subject.empty()) {
            const double blend = s.shot != nullptr && s.shot->transition == ShotTransition::Blend ? s.shot->blendSeconds : 0.0;
            world::ShotSpan travel;
            travel.start = s.start;
            travel.end = std::min(s.end, s.start + std::max(kCutTravelSeconds, blend));
            travel.travel = true;
            travel.spotlight = false;
            travel.subject = segments[i - 1].subject;
            place(travel, locate);
            travel.handoff = s.subject;
            world::ShotSpan to;
            to.subject = s.subject;
            place(to, locate);
            travel.handoffPosition = to.subjectPosition;
            spans.push_back(std::move(travel));
        }
        if (!s.subject.empty()) {
            world::ShotSpan hold;
            hold.start = s.start;
            hold.end = s.end;
            hold.travel = false;
            hold.spotlight = true;
            hold.subject = s.subject;
            place(hold, locate);
            spans.push_back(std::move(hold));
        }
    }
    return spans;
}

} // namespace avgen::scene
