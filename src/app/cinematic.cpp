#include "app/cinematic.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace avgen::app {
namespace {
using nlohmann::json;

constexpr std::array<std::pair<ShotKind, const char*>, 14> kShotNames{{
    {ShotKind::Establish, "establish"},
    {ShotKind::Approach, "approach"},
    {ShotKind::Reveal, "reveal"},
    {ShotKind::Entry, "entry"},
    {ShotKind::Passage, "passage"},
    {ShotKind::Descent, "descent"},
    {ShotKind::Ascent, "ascent"},
    {ShotKind::Orbit, "orbit"},
    {ShotKind::Track, "track"},
    {ShotKind::Discovery, "discovery"},
    {ShotKind::HeroReveal, "heroReveal"},
    {ShotKind::Flyby, "flyby"},
    {ShotKind::Drift, "drift"},
    {ShotKind::Transition, "transition"},
}};

// The words a director says for the same moves. Read-only: `shotKindName` never returns one of
// these, so a sequence that has been through JSON once does not change spelling on the second pass.
constexpr std::array<std::pair<ShotKind, const char*>, 6> kShotAliases{{
    {ShotKind::Establish, "establishing"},
    {ShotKind::Track, "follow"},
    {ShotKind::HeroReveal, "hero-reveal"},
    {ShotKind::Drift, "environmental-drift"},
    {ShotKind::Discovery, "discover"},
    {ShotKind::Flyby, "fly-by"},
}};

constexpr std::array<std::pair<LookMode, const char*>, 5> kLookNames{{
    {LookMode::Subject, "subject"}, {LookMode::Ahead, "ahead"},   {LookMode::Fixed, "fixed"},
    {LookMode::Parallel, "parallel"}, {LookMode::Handoff, "handoff"},
}};

constexpr std::array<std::pair<MovementCurve, const char*>, 4> kCurveNames{{
    {MovementCurve::Straight, "straight"},
    {MovementCurve::Arc, "arc"},
    {MovementCurve::Rise, "rise"},
    {MovementCurve::Dip, "dip"},
}};

// A 36x24 mm frame, so `focalLength` in millimetres means what a photographer means by it. Only the
// half-height is ever needed: coverage is measured against frame height because that is the
// dimension a subject's silhouette has to survive.
constexpr float kSensorHalfHeightMm = 12.0f;

// Below this the subject spans a sliver of the frame, and a shot that calls itself a hero shot at
// that size is a mistake somebody will only find in a render. Deliberately generous -- it catches
// "the hero is a speck", not "the hero could be bigger".
constexpr float kMinHeroCoverage = 0.06f;

// Smoothstep on whichever ends asked for it. A move that eases at both ends is the default because
// constant velocity between two points is the one thing that always reads as a machine.
float ease(float t, bool in, bool out) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (in && out) {
        return t * t * (3.0f - 2.0f * t);
    }
    if (in) {
        return t * t;
    }
    if (out) {
        return 1.0f - (1.0f - t) * (1.0f - t);
    }
    return t;
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// The defaults each kind wants, so authoring a shot is naming its kind and its subject and nothing
// else. A shot may override any of them; most never need to.
struct KindDefaults {
    float startDistance;
    float endDistance;
    float azimuthSweep;   // radians travelled around the subject
    float startElevation;
    float endElevation;
    LookMode look = LookMode::Subject;
    MovementCurve curve = MovementCurve::Straight;
    float bow = 0.0f;     // midpoint deflection, as a fraction of the chord
};

KindDefaults defaultsFor(ShotKind k) {
    switch (k) {
    case ShotKind::Establish:
        return {14.0f, 13.0f, 0.06f, 0.22f, 0.20f};   // barely moves; the world is the subject
    case ShotKind::Approach:
        return {16.0f, 3.2f, 0.18f, 0.20f, 0.10f};
    case ShotKind::Reveal:
        return {0.9f, 11.0f, 0.10f, 0.02f, 0.26f};    // starts too close to read, pulls back
    case ShotKind::Entry:
        return {5.0f, 0.35f, 0.05f, 0.08f, 0.0f};     // ends inside
    case ShotKind::Passage:
        return {3.0f, -3.0f, 0.0f, 0.0f, 0.0f, LookMode::Ahead};  // negative: out the other side
    case ShotKind::Descent:
        return {6.0f, 5.0f, 0.12f, 0.85f, -0.35f};
    case ShotKind::Ascent:
        return {6.0f, 5.0f, 0.12f, -0.30f, 0.80f};
    case ShotKind::Orbit:
        return {7.0f, 7.0f, 2.1f, 0.18f, 0.24f};
    case ShotKind::Track:
        return {5.0f, 5.0f, 0.25f, 0.12f, 0.12f};

    // The five added in milestone 9. Each carries a bow, because each of them is a move whose
    // point is the parallax: taken as a straight line they collapse into one of the first nine.
    case ShotKind::Discovery:
        // Comes in from the side and from above, so the subject slides out from behind whatever is
        // in front of it. An approach on the same axis just makes the obstruction bigger.
        return {12.0f, 4.2f, 0.55f, 0.30f, 0.16f, LookMode::Subject, MovementCurve::Arc, 0.20f};
    case ShotKind::HeroReveal:
        // Opening out *and* going round: distance alone gives scale, sweep alone gives silhouette,
        // and the hero moment needs both or it reads as an ordinary pull-back.
        return {1.8f, 6.5f, 1.15f, 0.05f, 0.30f, LookMode::Subject, MovementCurve::Arc, 0.08f};
    case ShotKind::Flyby:
        // Through and out the far side, holding the subject in frame -- which is the only thing
        // separating a flyby from a passage. The large bow is what keeps the path off the subject
        // instead of through it.
        return {4.5f, -4.5f, 0.75f, 0.22f, 0.22f, LookMode::Subject, MovementCurve::Arc, 0.30f};
    case ShotKind::Drift:
        // Lateral travel with the aim held. No pull-in, no sweep worth naming: the shot is about
        // what passes between the camera and the subject, not about the subject.
        return {5.0f, 5.0f, 0.34f, 0.10f, 0.10f, LookMode::Parallel};
    case ShotKind::Transition:
        return {5.0f, 5.0f, 0.20f, 0.18f, 0.18f, LookMode::Handoff, MovementCurve::Arc, 0.12f};
    }
    return {8.0f, 8.0f, 0.1f, 0.2f, 0.2f};
}

glm::vec3 orbitPoint(const FocalTarget& subject, float distance, float azimuth, float elevation) {
    // Distance is in radii, so a shot works against a subject of any size.
    const float r = std::max(subject.radius, 0.01f) * distance;
    return subject.position + glm::vec3(std::cos(azimuth) * r, elevation * std::abs(r),
                                        std::sin(azimuth) * r);
}

// One end of a shot as a point. The far end anchors on the handoff subject when there is one: a
// shot that names a second subject is going to it, which is what naming it means.
glm::vec3 polarPose(const Shot& s, bool atStart) {
    const FocalTarget& anchor = (!atStart && s.handoff) ? *s.handoff : s.subject;
    return orbitPoint(anchor, atStart ? s.startDistance : s.endDistance,
                      atStart ? s.startAzimuth : s.endAzimuth,
                      atStart ? s.startElevation : s.endElevation);
}

// The midpoint deflection, zero at both ends so the shot still begins and finishes where it said it
// would. sin rather than a parabola because its slope at the ends is finite: a parabola meets the
// chord at an angle and the camera visibly kinks into its first frame.
glm::vec3 bowOffset(const Shot& s, const glm::vec3& from, const glm::vec3& to, float e) {
    const float amount = s.bowAmount();
    if (std::abs(amount) < 1e-5f) {
        return glm::vec3(0.0f);
    }
    const glm::vec3 chord = to - from;
    const float len = glm::length(chord);
    if (len < 1e-4f) {
        return glm::vec3(0.0f);
    }
    const float profile = std::sin(e * std::numbers::pi_v<float>) * len * amount;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    switch (s.movementCurve()) {
    case MovementCurve::Straight:
        return glm::vec3(0.0f);
    case MovementCurve::Rise:
        return up * profile;
    case MovementCurve::Dip:
        return up * -profile;
    case MovementCurve::Arc:
        break;
    }
    glm::vec3 side = glm::cross(chord / len, up);
    if (glm::length(side) < 1e-4f) {
        // A purely vertical move has no horizontal perpendicular; pick one rather than returning a
        // NaN normalisation, which is how an ascent used to put the camera at the origin.
        side = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    return glm::normalize(side) * profile;
}
} // namespace

const char* shotKindName(ShotKind k) {
    for (const auto& [kind, name] : kShotNames) {
        if (kind == k) {
            return name;
        }
    }
    return "establish";
}

std::optional<ShotKind> shotKindFromName(std::string_view name) {
    for (const auto& [kind, text] : kShotNames) {
        if (name == text) {
            return kind;
        }
    }
    for (const auto& [kind, text] : kShotAliases) {
        if (name == text) {
            return kind;
        }
    }
    return std::nullopt;
}

const char* lookModeName(LookMode m) {
    for (const auto& [mode, name] : kLookNames) {
        if (mode == m) {
            return name;
        }
    }
    return "subject";
}

std::optional<LookMode> lookModeFromName(std::string_view name) {
    for (const auto& [mode, text] : kLookNames) {
        if (name == text) {
            return mode;
        }
    }
    return std::nullopt;
}

const char* movementCurveName(MovementCurve c) {
    for (const auto& [curve, name] : kCurveNames) {
        if (curve == c) {
            return name;
        }
    }
    return "straight";
}

std::optional<MovementCurve> movementCurveFromName(std::string_view name) {
    for (const auto& [curve, text] : kCurveNames) {
        if (name == text) {
            return curve;
        }
    }
    return std::nullopt;
}

LookMode Shot::lookMode() const { return look.value_or(defaultsFor(kind).look); }
MovementCurve Shot::movementCurve() const { return curve.value_or(defaultsFor(kind).curve); }
float Shot::bowAmount() const {
    // A shot that named a curve but no bow means the curve; a shot that named neither means its
    // kind's. Splitting these lets a caller say "arc it" without also having to invent a number.
    if (curveBow) {
        return *curveBow;
    }
    const KindDefaults d = defaultsFor(kind);
    return curve && *curve != d.curve ? 0.12f : d.bow;
}

glm::vec3 Shot::cameraAt(float t) const {
    const float e = ease(t, easeIn, easeOut);
    const glm::vec3 from = startPosition ? *startPosition : polarPose(*this, true);
    const glm::vec3 to = endPosition ? *endPosition : polarPose(*this, false);

    glm::vec3 p;
    if (startPosition || endPosition || handoff) {
        // An authored end is a point, and a point has no distance-in-radii or azimuth to
        // interpolate. Sliding between the two points is the only reading of "go from here to
        // there" that survives a subject the shot is not actually orbiting.
        p = from + (to - from) * e;
    } else {
        p = orbitPoint(subject, lerp(startDistance, endDistance, e),
                       lerp(startAzimuth, endAzimuth, e), lerp(startElevation, endElevation, e));
    }
    if (heightRange) {
        // Absolute metres, replacing the elevation term entirely. Elevation-in-radii is right for a
        // shot about an object and wrong for a shot about a place: a valley traverse is authored as
        // "two metres up, rising to fourteen", and nobody should have to divide that by a radius.
        p.y = lerp(heightRange->x, heightRange->y, e);
    }
    return p + bowOffset(*this, from, to, e);
}

glm::vec3 Shot::targetAt(float t) const {
    const float e = ease(t, easeIn, easeOut);
    switch (lookMode()) {
    case LookMode::Subject:
        return subject.position;
    case LookMode::Fixed:
        return lookAt;
    case LookMode::Parallel: {
        // The aim direction is frozen at whatever it was on the first frame, so the camera
        // translates and the world slides across it. Panning to hold something during a lateral
        // move cancels exactly the parallax that was the reason for making the move.
        return cameraAt(t) + (subject.position - cameraAt(0.0f));
    }
    case LookMode::Handoff: {
        const glm::vec3 to = handoff ? handoff->position : subject.position;
        // The swing happens across the middle of the shot rather than the whole of it. A target
        // already drifting on the first frame means the first subject is never actually held, and
        // the shot reads as an error instead of as leaving one thing to find another.
        const float s = std::clamp((e - 0.25f) / 0.5f, 0.0f, 1.0f);
        return subject.position + (to - subject.position) * (s * s * (3.0f - 2.0f * s));
    }
    case LookMode::Ahead:
        break;
    }
    // Looking where the move is going. Aiming back at what you are flying through reads as a
    // mistake, which is why Passage has never aimed at its subject.
    const glm::vec3 from = startPosition ? *startPosition : polarPose(*this, true);
    const glm::vec3 to = endPosition ? *endPosition : polarPose(*this, false);
    const glm::vec3 travel = to - from;
    const float len = glm::length(travel);
    if (len < 1e-4f) {
        return subject.position;
    }
    return cameraAt(t) + (travel / len) * std::max(subject.radius * 4.0f, 1.0f) * (0.6f + 0.4f * e);
}

float Shot::focusDistanceAt(float t) const {
    const glm::vec3 focus = composition.focusOnSubject ? subject.position : targetAt(t);
    return std::max(glm::length(cameraAt(t) - focus), 0.01f);
}

float Shot::subjectCoverageAt(float t) const {
    const float d = std::max(glm::length(cameraAt(t) - subject.position), 1e-3f);
    const float f = std::max(composition.focalLength, 1.0f);
    const float halfFrame = std::atan(kSensorHalfHeightMm / f);
    if (halfFrame < 1e-6f) {
        return 1.0f;
    }
    return std::clamp(std::atan(std::max(subject.radius, 0.0f) / d) / halfFrame, 0.0f, 1.0f);
}

float Shot::pathLength(int samples) const {
    const int n = std::clamp(samples, 2, 512);
    float total = 0.0f;
    glm::vec3 previous = cameraAt(0.0f);
    for (int i = 1; i < n; ++i) {
        const glm::vec3 p = cameraAt(static_cast<float>(i) / static_cast<float>(n - 1));
        total += glm::length(p - previous);
        previous = p;
    }
    return total;
}

float Shot::peakSpeed(int samples) const {
    const int n = std::clamp(samples, 2, 512);
    if (!(durationSeconds > 0.0)) {
        return 0.0f;
    }
    // Sampled uniformly in *time*, so the answer includes the easing: a shot that covers its ground
    // in the middle third is faster there than its average, and the middle third is where a move
    // looks like a teleport if it is going to.
    const float dt = static_cast<float>(durationSeconds) / static_cast<float>(n - 1);
    float fastest = 0.0f;
    glm::vec3 previous = cameraAt(0.0f);
    for (int i = 1; i < n; ++i) {
        const glm::vec3 p = cameraAt(static_cast<float>(i) / static_cast<float>(n - 1));
        fastest = std::max(fastest, glm::length(p - previous) / dt);
        previous = p;
    }
    return fastest;
}

Result<void> Sequence::validate() const {
    if (shots.empty()) {
        return fail("sequence '{}' has no shots", name);
    }
    double previousEnd = -1.0;
    for (const auto& s : shots) {
        if (s.durationSeconds <= 0.0) {
            return fail("sequence '{}': shot '{}' has a duration of {}", name, s.name,
                        s.durationSeconds);
        }
        if (s.startSeconds < 0.0) {
            return fail("sequence '{}': shot '{}' starts at {}", name, s.name, s.startSeconds);
        }
        if (s.startSeconds < previousEnd - 1e-6) {
            // Two cameras at once is not something a single-camera engine can honour, and quietly
            // picking one of them is worse than saying so.
            return fail("sequence '{}': shot '{}' starts at {} but the previous shot runs to {}",
                        name, s.name, s.startSeconds, previousEnd);
        }
        if (!(s.subject.radius > 0.0f)) {
            return fail("sequence '{}': shot '{}' has a subject radius of {}; distances are in "
                        "radii, so a subject with no size has no shot",
                        name, s.name, s.subject.radius);
        }
        if ((s.kind == ShotKind::Transition || s.lookMode() == LookMode::Handoff) && !s.handoff) {
            return fail("sequence '{}': shot '{}' is a transition with nothing to transition to; a "
                        "handoff subject is the second half of the move",
                        name, s.name);
        }
        if (s.spotlight.active) {
            if (!(s.spotlight.emphasis >= 0.0f && s.spotlight.emphasis <= 1.0f)) {
                return fail("sequence '{}': shot '{}' has a spotlight emphasis of {}; it is a 0..1 "
                            "share of the frame's attention, not a gain",
                            name, s.name, s.spotlight.emphasis);
            }
            // A hero shot in which the hero is a speck is the failure this check exists for, and it
            // is one that only shows up in a render otherwise. Sampled rather than taken at the
            // ends, because a reveal is at its readable size in the middle.
            float best = 0.0f;
            for (int i = 0; i <= 8; ++i) {
                best = std::max(best, s.subjectCoverageAt(static_cast<float>(i) / 8.0f));
            }
            if (best < kMinHeroCoverage) {
                return fail("sequence '{}': shot '{}' spotlights '{}' but it never spans more than "
                            "{:.3f} of the frame; at {:.0f} mm it is too far away to be the point "
                            "of the shot",
                            name, s.name, s.subject.name, best, s.composition.focalLength);
            }
        }
        previousEnd = s.endSeconds();
    }
    return {};
}

double Sequence::cutsPerMinute() const {
    const double total = durationSeconds();
    if (shots.size() < 2 || !(total > 0.0)) {
        return 0.0;
    }
    return static_cast<double>(shots.size() - 1) / (total / 60.0);
}

Result<void> Sequence::validateCadence(double minShotSeconds, double maxCutsPerMinute) const {
    for (const auto& s : shots) {
        if (s.durationSeconds < minShotSeconds - 1e-6) {
            return fail("sequence '{}': shot '{}' runs {:.2f} s against a floor of {:.2f} s; below "
                        "that a viewer registers the cut rather than the shot",
                        name, s.name, s.durationSeconds, minShotSeconds);
        }
    }
    const double rate = cutsPerMinute();
    if (rate > maxCutsPerMinute + 1e-6) {
        return fail("sequence '{}' cuts {:.1f} times a minute against a ceiling of {:.1f}", name,
                    rate, maxCutsPerMinute);
    }
    return {};
}

Result<void> Sequence::retime() {
    if (shots.empty()) {
        return fail("sequence '{}' has no shots to retime", name);
    }
    double cursor = shots.front().startSeconds;
    for (auto& s : shots) {
        if (s.speed > 0.0f) {
            const float length = s.pathLength();
            if (!(length > 1e-3f)) {
                return fail("sequence '{}': shot '{}' asks to travel at {} m/s but does not move",
                            name, s.name, s.speed);
            }
            s.durationSeconds = static_cast<double>(length / s.speed);
        }
        if (!(s.durationSeconds > 0.0)) {
            return fail("sequence '{}': shot '{}' has a duration of {}", name, s.name,
                        s.durationSeconds);
        }
        s.startSeconds = cursor;
        cursor += s.durationSeconds;
    }
    return {};
}

const FocalTarget* Sequence::spotlightAt(double seconds) const {
    const Shot* s = shotAt(seconds);
    return (s != nullptr && s->spotlight.active) ? &s->subject : nullptr;
}

json Sequence::spotlightSpans() const {
    json spans = json::array();
    for (const auto& s : shots) {
        if (!s.spotlight.active) {
            continue;
        }
        // Consecutive shots on the same subject are one span. A rig that dims and re-lights the
        // hero across a cut it is still the subject of is worse than one that never touched it.
        if (!spans.empty() && spans.back()["subject"] == s.subject.name &&
            std::abs(spans.back()["end"].get<double>() - s.startSeconds) < 1e-6) {
            spans.back()["end"] = s.endSeconds();
            spans.back()["emphasis"] =
                std::max(spans.back()["emphasis"].get<float>(), s.spotlight.emphasis);
            continue;
        }
        spans.push_back(json{{"subject", s.subject.name},
                             {"position", json::array({s.subject.position.x, s.subject.position.y,
                                                       s.subject.position.z})},
                             {"radius", s.subject.radius},
                             {"start", s.startSeconds},
                             {"end", s.endSeconds()},
                             {"emphasis", s.spotlight.emphasis}});
    }
    return spans;
}

double Sequence::durationSeconds() const {
    double end = 0.0;
    for (const auto& s : shots) {
        end = std::max(end, s.endSeconds());
    }
    return end;
}

const Shot* Sequence::shotAt(double seconds) const {
    for (const auto& s : shots) {
        if (seconds >= s.startSeconds && seconds < s.endSeconds()) {
            return &s;
        }
    }
    return nullptr;
}

json Sequence::toTimelineTracks(int samplesPerShot) const {
    const int samples = std::clamp(samplesPerShot, 2, 64);
    json positionKeys = json::array();
    json targetKeys = json::array();
    json focalKeys = json::array();
    json apertureKeys = json::array();
    json focusKeys = json::array();
    json emphasisKeys = json::array();

    for (const auto& s : shots) {
        for (int i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
            const double time = s.startSeconds + s.durationSeconds * static_cast<double>(t);
            const glm::vec3 p = s.cameraAt(t);
            const glm::vec3 q = s.targetAt(t);
            // The keys carry the eased positions, so the interpolation between them is linear and
            // the shape of the move is entirely the shot's own. Handing eased values to a smooth
            // interpolator would ease them twice.
            positionKeys.push_back(json{{"time", time},
                                        {"value", json::array({p.x, p.y, p.z})},
                                        {"interp", "linear"}});
            targetKeys.push_back(json{{"time", time},
                                      {"value", json::array({q.x, q.y, q.z})},
                                      {"interp", "linear"}});
            // Focus *is* per sample, unlike focal length: a rack focus that follows the subject
            // through a move is not a zoom, it is the lens doing the one thing it has to do to keep
            // the subject sharp. ADR-062 recorded `focusOnSubject` and never wired it; this is it.
            focusKeys.push_back(json{{"time", time},
                                     {"value", s.focusDistanceAt(t)},
                                     {"interp", "linear"}});
        }
        // The lens is per shot rather than per sample: a focal length that slides through a shot is
        // a zoom, and a zoom is a thing you ask for, not a thing you get by accident.
        focalKeys.push_back(json{{"time", s.startSeconds},
                                 {"value", s.composition.focalLength},
                                 {"interp", "linear"}});
        apertureKeys.push_back(json{{"time", s.startSeconds},
                                    {"value", s.composition.aperture},
                                    {"interp", "linear"}});
        // Emphasis is a state, not a curve. Two keys hold it flat to the last millisecond of the
        // shot so the change happens *at* the cut; one key per shot would ramp the hero's
        // importance across the whole of the shot before it.
        const float emphasis = s.spotlight.active ? s.spotlight.emphasis : 0.0f;
        emphasisKeys.push_back(
            json{{"time", s.startSeconds}, {"value", emphasis}, {"interp", "linear"}});
        emphasisKeys.push_back(json{{"time", std::max(s.endSeconds() - 1e-3, s.startSeconds)},
                                    {"value", emphasis},
                                    {"interp", "linear"}});
    }

    json tracks = json::array();
    tracks.push_back(json{{"target", "camera/position"},
                          {"timeBase", "seconds"},
                          {"mode", "replace"},
                          {"keys", std::move(positionKeys)}});
    tracks.push_back(json{{"target", "camera/target"},
                          {"timeBase", "seconds"},
                          {"mode", "replace"},
                          {"keys", std::move(targetKeys)}});
    tracks.push_back(json{{"target", "camera/lens/focalLength"},
                          {"timeBase", "seconds"},
                          {"mode", "replace"},
                          {"keys", std::move(focalKeys)}});
    tracks.push_back(json{{"target", "camera/lens/aperture"},
                          {"timeBase", "seconds"},
                          {"mode", "replace"},
                          {"keys", std::move(apertureKeys)}});
    tracks.push_back(json{{"target", "camera/lens/focusDistance"},
                          {"timeBase", "seconds"},
                          {"mode", "replace"},
                          {"keys", std::move(focusKeys)}});
    tracks.push_back(json{{"target", "camera/focus/emphasis"},
                          {"timeBase", "seconds"},
                          {"mode", "replace"},
                          {"keys", std::move(emphasisKeys)}});
    return tracks;
}

json Sequence::toJson() const {
    json j = json::object();
    j["name"] = name;
    json arr = json::array();
    for (const auto& s : shots) {
        json e = json::object();
        e["name"] = s.name;
        e["kind"] = shotKindName(s.kind);
        e["start"] = s.startSeconds;
        e["duration"] = s.durationSeconds;
        e["subject"] = json{{"name", s.subject.name},
                            {"position", json::array({s.subject.position.x, s.subject.position.y,
                                                      s.subject.position.z})},
                            {"radius", s.subject.radius}};
        e["distance"] = json::array({s.startDistance, s.endDistance});
        e["azimuth"] = json::array({s.startAzimuth, s.endAzimuth});
        e["elevation"] = json::array({s.startElevation, s.endElevation});
        e["composition"] = json{{"framing", json::array({s.composition.framing.x, s.composition.framing.y})},
                                {"headroom", s.composition.headroom},
                                {"focalLength", s.composition.focalLength},
                                {"aperture", s.composition.aperture},
                                {"focusOnSubject", s.composition.focusOnSubject}};
        e["easeIn"] = s.easeIn;
        e["easeOut"] = s.easeOut;
        // Only what the shot actually asked for is written back. Serialising a resolved default
        // would freeze it: the shot would stop following its kind the first time it round-tripped.
        if (s.look) {
            e["look"] = lookModeName(*s.look);
        }
        if (s.curve) {
            e["curve"] = movementCurveName(*s.curve);
        }
        if (s.curveBow) {
            e["bow"] = *s.curveBow;
        }
        if (s.lookMode() == LookMode::Fixed) {
            e["lookAt"] = json::array({s.lookAt.x, s.lookAt.y, s.lookAt.z});
        }
        if (s.startPosition) {
            e["startPosition"] =
                json::array({s.startPosition->x, s.startPosition->y, s.startPosition->z});
        }
        if (s.endPosition) {
            e["endPosition"] = json::array({s.endPosition->x, s.endPosition->y, s.endPosition->z});
        }
        if (s.heightRange) {
            e["height"] = json::array({s.heightRange->x, s.heightRange->y});
        }
        if (s.handoff) {
            e["handoff"] = json{{"name", s.handoff->name},
                                {"position", json::array({s.handoff->position.x,
                                                          s.handoff->position.y,
                                                          s.handoff->position.z})},
                                {"radius", s.handoff->radius}};
        }
        if (s.speed > 0.0f) {
            e["speed"] = s.speed;
        }
        if (s.spotlight.active) {
            e["spotlight"] = json{{"active", true}, {"emphasis", s.spotlight.emphasis}};
        }
        arr.push_back(std::move(e));
    }
    j["shots"] = std::move(arr);
    return j;
}

Result<Sequence> Sequence::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("a sequence must be a JSON object");
    }
    Sequence seq;
    if (j.contains("name") && j.at("name").is_string()) {
        seq.name = j.at("name").get<std::string>();
    }
    if (!j.contains("shots") || !j.at("shots").is_array()) {
        return fail("sequence '{}' needs a 'shots' array", seq.name);
    }
    double cursor = 0.0;
    for (const auto& e : j.at("shots")) {
        if (!e.is_object()) {
            return fail("sequence '{}': every shot must be an object", seq.name);
        }
        Shot s;
        if (e.contains("name") && e.at("name").is_string()) {
            s.name = e.at("name").get<std::string>();
        }
        if (e.contains("kind") && e.at("kind").is_string()) {
            const auto kindName = e.at("kind").get<std::string>();
            auto k = shotKindFromName(kindName);
            if (!k) {
                return fail("sequence '{}': shot '{}' has unknown kind '{}'", seq.name, s.name,
                            kindName);
            }
            s.kind = *k;
        }
        const KindDefaults d = defaultsFor(s.kind);
        s.startDistance = d.startDistance;
        s.endDistance = d.endDistance;
        s.startElevation = d.startElevation;
        s.endElevation = d.endElevation;

        if (e.contains("duration") && e.at("duration").is_number()) {
            s.durationSeconds = e.at("duration").get<double>();
        }
        // A shot without an explicit start follows the previous one, which is how a cut list is
        // written by hand and the only reason a nine-shot sequence is readable.
        s.startSeconds = e.contains("start") && e.at("start").is_number()
                             ? e.at("start").get<double>()
                             : cursor;
        cursor = s.startSeconds + s.durationSeconds;

        const auto readTarget = [](const json& sub, FocalTarget& out) {
            if (sub.contains("name") && sub.at("name").is_string()) {
                out.name = sub.at("name").get<std::string>();
            }
            if (sub.contains("position") && sub.at("position").is_array() &&
                sub.at("position").size() == 3) {
                const auto& p = sub.at("position");
                out.position = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
            }
            if (sub.contains("radius") && sub.at("radius").is_number()) {
                out.radius = sub.at("radius").get<float>();
            }
        };
        if (e.contains("subject")) {
            const json& sub = e.at("subject");
            if (!sub.is_object()) {
                return fail("sequence '{}': shot '{}' subject must be an object", seq.name, s.name);
            }
            readTarget(sub, s.subject);
        }
        if (e.contains("handoff")) {
            const json& sub = e.at("handoff");
            if (!sub.is_object()) {
                return fail("sequence '{}': shot '{}' handoff must be an object", seq.name, s.name);
            }
            FocalTarget other;
            readTarget(sub, other);
            s.handoff = other;
        }
        const auto readPair = [&e](const char* key, float& a, float& b) {
            if (e.contains(key) && e.at(key).is_array() && e.at(key).size() == 2) {
                a = e.at(key)[0].get<float>();
                b = e.at(key)[1].get<float>();
            }
        };
        readPair("distance", s.startDistance, s.endDistance);
        readPair("elevation", s.startElevation, s.endElevation);
        if (e.contains("azimuth") && e.at("azimuth").is_array() && e.at("azimuth").size() == 2) {
            s.startAzimuth = e.at("azimuth")[0].get<float>();
            s.endAzimuth = e.at("azimuth")[1].get<float>();
        } else {
            // Sweep the kind's own arc, from wherever the shot starts.
            s.endAzimuth = s.startAzimuth + d.azimuthSweep;
        }
        if (e.contains("composition")) {
            const json& c = e.at("composition");
            if (c.contains("focalLength") && c.at("focalLength").is_number()) {
                s.composition.focalLength = c.at("focalLength").get<float>();
            }
            if (c.contains("aperture") && c.at("aperture").is_number()) {
                s.composition.aperture = c.at("aperture").get<float>();
            }
            if (c.contains("headroom") && c.at("headroom").is_number()) {
                s.composition.headroom = c.at("headroom").get<float>();
            }
            if (c.contains("focusOnSubject") && c.at("focusOnSubject").is_boolean()) {
                s.composition.focusOnSubject = c.at("focusOnSubject").get<bool>();
            }
            if (c.contains("framing") && c.at("framing").is_array() && c.at("framing").size() == 2) {
                s.composition.framing =
                    glm::vec2(c.at("framing")[0].get<float>(), c.at("framing")[1].get<float>());
            }
        }
        if (e.contains("easeIn") && e.at("easeIn").is_boolean()) {
            s.easeIn = e.at("easeIn").get<bool>();
        }
        if (e.contains("easeOut") && e.at("easeOut").is_boolean()) {
            s.easeOut = e.at("easeOut").get<bool>();
        }
        if (e.contains("look") && e.at("look").is_string()) {
            const auto text = e.at("look").get<std::string>();
            auto m = lookModeFromName(text);
            if (!m) {
                return fail("sequence '{}': shot '{}' has unknown look mode '{}'", seq.name, s.name,
                            text);
            }
            s.look = *m;
        }
        if (e.contains("curve") && e.at("curve").is_string()) {
            const auto text = e.at("curve").get<std::string>();
            auto c = movementCurveFromName(text);
            if (!c) {
                return fail("sequence '{}': shot '{}' has unknown curve '{}'", seq.name, s.name,
                            text);
            }
            s.curve = *c;
        }
        if (e.contains("bow") && e.at("bow").is_number()) {
            s.curveBow = e.at("bow").get<float>();
        }
        const auto readVec3 = [&e](const char* key, glm::vec3& out) {
            if (e.contains(key) && e.at(key).is_array() && e.at(key).size() == 3) {
                const auto& v = e.at(key);
                out = glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
                return true;
            }
            return false;
        };
        readVec3("lookAt", s.lookAt);
        glm::vec3 scratch{0.0f};
        if (readVec3("startPosition", scratch)) {
            s.startPosition = scratch;
        }
        if (readVec3("endPosition", scratch)) {
            s.endPosition = scratch;
        }
        if (e.contains("height") && e.at("height").is_array() && e.at("height").size() == 2) {
            // A pair or nothing. A half-given height range would have to invent the other end from
            // the polar elevation it was brought in to replace, and the result is neither.
            s.heightRange = glm::vec2(e.at("height")[0].get<float>(), e.at("height")[1].get<float>());
        }
        if (e.contains("speed") && e.at("speed").is_number()) {
            s.speed = e.at("speed").get<float>();
        }
        if (e.contains("spotlight")) {
            const json& sp = e.at("spotlight");
            if (sp.is_boolean()) {
                s.spotlight.active = sp.get<bool>();
                s.spotlight.emphasis = s.spotlight.active ? 1.0f : 0.0f;
            } else if (sp.is_object()) {
                if (sp.contains("active") && sp.at("active").is_boolean()) {
                    s.spotlight.active = sp.at("active").get<bool>();
                }
                if (sp.contains("emphasis") && sp.at("emphasis").is_number()) {
                    s.spotlight.emphasis = sp.at("emphasis").get<float>();
                    s.spotlight.active = true;
                }
            }
        }
        seq.shots.push_back(std::move(s));
    }
    if (auto ok = seq.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return seq;
}

// ---- direction ---------------------------------------------------------------------------------

ShotKind shotKindForSection(signals::MusicalSection section) {
    using S = signals::MusicalSection;
    switch (section) {
    case S::Intro:
        return ShotKind::Establish;   // nothing has happened; show where we are
    case S::Build:
        return ShotKind::Discovery;   // a build is the camera setting off towards something
    case S::Phrase:
        return ShotKind::Drift;       // an ordinary passage is the world going past
    case S::Drop:
        return ShotKind::HeroReveal;  // the drop lands on the reveal it was built for
    case S::Verse:
        return ShotKind::Transition;  // the section changed; leave what we were on and find another
    case S::Breakdown:
        return ShotKind::Approach;    // quiet and close: the shot a loud section could not hold
    case S::FinalBuild:
        return ShotKind::Ascent;      // the last run-up goes up, so the last drop has somewhere to fall from
    case S::FinalDrop:
        return ShotKind::Reveal;      // the widest opening-out in the film, kept for the end
    case S::Outro:
        return ShotKind::Establish;
    }
    return ShotKind::Establish;
}

namespace {
using signals::MusicalSection;

bool isDropSection(MusicalSection s) {
    return s == MusicalSection::Drop || s == MusicalSection::FinalDrop;
}

// The hero owns the payoffs and the run-ups to them, and the wide shots that bracket the film.
// Everything in between belongs to the supporting cast, because a film in which every shot is of
// the same object has no scale: the hero is only big if something else was small.
bool heroOwns(MusicalSection s) {
    switch (s) {
    case MusicalSection::Intro:
    case MusicalSection::Build:
    case MusicalSection::Drop:
    case MusicalSection::FinalBuild:
    case MusicalSection::FinalDrop:
    case MusicalSection::Outro:
        return true;
    default:
        return false;
    }
}

float emphasisFor(MusicalSection s, float intensity) {
    switch (s) {
    case MusicalSection::FinalDrop:
        return std::clamp(0.80f + 0.20f * intensity, 0.0f, 1.0f);
    case MusicalSection::Drop:
        return std::clamp(0.65f + 0.35f * intensity, 0.0f, 1.0f);
    case MusicalSection::FinalBuild:
        return std::clamp(0.35f + 0.30f * intensity, 0.0f, 1.0f);
    default:
        return 0.0f;
    }
}

// One shot's worth of the structure. `opener` is the section that decided what the shot is; the
// span may cover sections after it that were too short to be worth a cut of their own.
struct ShotSpan {
    const signals::StructureSection* opener = nullptr;
    double start = 0.0;
    double end = 0.0;
};

std::vector<ShotSpan> groupSections(const signals::MusicalStructure& structure,
                                    const DirectionBrief& brief) {
    std::vector<ShotSpan> spans;
    for (const auto& section : structure.sections) {
        if (spans.empty()) {
            spans.push_back(ShotSpan{&section, section.startSeconds, section.endSeconds()});
            continue;
        }
        const double runSoFar = section.startSeconds - spans.back().start;
        if (isDropSection(section.kind)) {
            // A drop always opens its own shot, exactly on the drop. That is the one hard rule
            // here: the whole point of reading the structure is that the reveal lands on the beat
            // the music lands on, and any smoothing that moves it is smoothing away the reason.
            if (runSoFar < brief.minBuildShotSeconds && spans.size() >= 2) {
                // The shot in front of it was a flash rather than a shot. Give its time back to the
                // one before, rather than keeping a one-second cut that reads as a glitch.
                spans.pop_back();
            }
            spans.back().end = section.startSeconds;
            spans.push_back(ShotSpan{&section, section.startSeconds, section.endSeconds()});
            continue;
        }
        if (runSoFar >= brief.minShotSeconds) {
            spans.back().end = section.startSeconds;
            spans.push_back(ShotSpan{&section, section.startSeconds, section.endSeconds()});
        } else {
            // Absorbed. Not every section the analyser found deserves a cut, and this is where "do
            // not cut constantly" is actually enforced rather than merely intended.
            spans.back().end = section.endSeconds();
        }
    }
    return spans;
}
} // namespace

Result<Sequence> directFromStructure(const signals::MusicalStructure& structure,
                                     const DirectionBrief& brief) {
    if (structure.sections.empty()) {
        return fail("cannot direct a sequence from a structure with no sections");
    }
    if (!(brief.hero.radius > 0.0f)) {
        return fail("the hero '{}' has a radius of {}; distances are in radii, so a hero with no "
                    "size has no film",
                    brief.hero.name, brief.hero.radius);
    }

    const auto spans = groupSections(structure, brief);
    Sequence seq;
    seq.name = "directed";
    seq.shots.reserve(spans.size());

    // A deterministic walk through the supporting cast rather than a draw from a stream. A PRNG
    // stream would make every later choice depend on how many earlier ones were made, so adding one
    // section to the structure would re-cast the whole rest of the film.
    std::uint32_t h = brief.seed * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    const std::size_t offset = brief.supporting.empty() ? 0 : (h >> 8) % brief.supporting.size();
    std::size_t supportingIndex = 0;

    for (std::size_t i = 0; i < spans.size(); ++i) {
        const auto& span = spans[i];
        const auto sectionKind = span.opener->kind;

        Shot shot;
        shot.name = fmt::format("{}-{}", signals::musicalSectionName(sectionKind), i + 1);
        shot.kind = shotKindForSection(sectionKind);
        shot.startSeconds = span.start;
        shot.durationSeconds = span.end - span.start;

        const bool hero = heroOwns(sectionKind) || brief.supporting.empty();
        shot.subject = hero ? brief.hero
                            : brief.supporting[(offset + supportingIndex) % brief.supporting.size()];
        if (!hero) {
            ++supportingIndex;
        }

        if (shot.kind == ShotKind::Transition) {
            // A transition needs somewhere to come from. With no previous shot, or no supporting
            // cast to have left, there is nothing to transition out of, so it becomes the follow
            // shot it would have ended as. Resolved before the defaults are taken, so the shot gets
            // the defaults of the kind it actually is.
            if (i > 0 && seq.shots.back().subject.name != shot.subject.name) {
                shot.handoff = shot.subject;
                shot.subject = seq.shots.back().subject;
            } else {
                shot.kind = ShotKind::Track;
            }
        }

        const KindDefaults d = defaultsFor(shot.kind);
        shot.startDistance = d.startDistance;
        shot.endDistance = d.endDistance;
        shot.startElevation = d.startElevation;
        shot.endElevation = d.endElevation;
        // The golden angle, so consecutive shots approach from unrelated directions and the film is
        // not nine views down the same axis. A multiple of a right angle would have every third
        // shot repeat the first one's geometry, which reads as the camera going back on itself.
        shot.startAzimuth = static_cast<float>(i) * 2.39996f;
        shot.endAzimuth = shot.startAzimuth + d.azimuthSweep;

        switch (shot.kind) {
        case ShotKind::Establish:
        case ShotKind::Drift:
            shot.composition.focalLength = brief.wideFocalLength;
            break;
        case ShotKind::HeroReveal:
        case ShotKind::Reveal:
        case ShotKind::Approach:
            shot.composition.focalLength = brief.heroFocalLength;
            break;
        default:
            break;
        }
        if (sectionKind == MusicalSection::Breakdown) {
            // Slow and close. The distance the camera covers is what makes a shot feel fast, not
            // the music under it, so an intimate shot has to actually travel less ground -- pulling
            // a normal approach through a quiet section just makes a quiet approach.
            shot.startDistance = 4.0f;
            shot.endDistance = 2.0f;
            shot.endAzimuth = shot.startAzimuth + 0.10f;
        }

        shot.spotlight.emphasis = emphasisFor(sectionKind, span.opener->intensity);
        shot.spotlight.active = shot.spotlight.emphasis > 0.0f;

        // A breakdown is the one place a cut belongs. The music has stopped, so the cut is invisible
        // -- and carrying a continuous camera across a break makes the break look like nothing
        // happened, as well as forcing the quiet shot to sprint in from wherever the last loud one
        // finished. Slow and close cannot be reached at a run.
        const bool carryOn = sectionKind != MusicalSection::Breakdown;
        if (brief.continuous && carryOn && !seq.shots.empty()) {
            // The reference camera "never orbits, never zooms, and holds its final pose" (audit
            // 1.4), and section 8 lists that restraint among the things not to change. Pinning each
            // shot's start to the last one's end makes the sequence one unbroken move whose
            // *intent* changes at the section boundaries, rather than a cut list.
            //
            // The cost is real and worth stating: a pinned start replaces the kind's polar path
            // with a line from the pinned point to the kind's end, so a shot that was an arc
            // becomes a chord. Giving the straight ones a bow buys the parallax back.
            shot.startPosition = seq.shots.back().cameraAt(1.0f);
            if (shot.movementCurve() == MovementCurve::Straight) {
                shot.curve = MovementCurve::Arc;
                shot.curveBow = 0.10f;
            }
        }
        seq.shots.push_back(std::move(shot));
    }

    if (auto ok = seq.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return seq;
}

} // namespace avgen::app
