#include "app/cinematic.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::app {
namespace {
using nlohmann::json;

constexpr std::array<std::pair<ShotKind, const char*>, 9> kShotNames{{
    {ShotKind::Establish, "establish"},
    {ShotKind::Approach, "approach"},
    {ShotKind::Reveal, "reveal"},
    {ShotKind::Entry, "entry"},
    {ShotKind::Passage, "passage"},
    {ShotKind::Descent, "descent"},
    {ShotKind::Ascent, "ascent"},
    {ShotKind::Orbit, "orbit"},
    {ShotKind::Track, "track"},
}};

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
        return {3.0f, -3.0f, 0.0f, 0.0f, 0.0f};       // negative: out the other side
    case ShotKind::Descent:
        return {6.0f, 5.0f, 0.12f, 0.85f, -0.35f};
    case ShotKind::Ascent:
        return {6.0f, 5.0f, 0.12f, -0.30f, 0.80f};
    case ShotKind::Orbit:
        return {7.0f, 7.0f, 2.1f, 0.18f, 0.24f};
    case ShotKind::Track:
        return {5.0f, 5.0f, 0.25f, 0.12f, 0.12f};
    }
    return {8.0f, 8.0f, 0.1f, 0.2f, 0.2f};
}

glm::vec3 orbitPoint(const FocalTarget& subject, float distance, float azimuth, float elevation) {
    // Distance is in radii, so a shot works against a subject of any size.
    const float r = std::max(subject.radius, 0.01f) * distance;
    return subject.position + glm::vec3(std::cos(azimuth) * r, elevation * std::abs(r),
                                        std::sin(azimuth) * r);
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
    return std::nullopt;
}

glm::vec3 Shot::cameraAt(float t) const {
    const float e = ease(t, easeIn, easeOut);
    const float d = lerp(startDistance, endDistance, e);
    const float a = lerp(startAzimuth, endAzimuth, e);
    const float el = lerp(startElevation, endElevation, e);
    return orbitPoint(subject, d, a, el);
}

glm::vec3 Shot::targetAt(float t) const {
    // Every kind aims at its subject except Passage, which aims past it: the camera is going
    // somewhere, and looking back at what it is flying through reads as a mistake.
    if (kind != ShotKind::Passage) {
        return subject.position;
    }
    const float e = ease(t, easeIn, easeOut);
    const glm::vec3 from = orbitPoint(subject, startDistance, startAzimuth, startElevation);
    const glm::vec3 to = orbitPoint(subject, endDistance, endAzimuth, endElevation);
    const glm::vec3 travel = to - from;
    const float len = glm::length(travel);
    if (len < 1e-4f) {
        return subject.position;
    }
    return cameraAt(t) + (travel / len) * std::max(subject.radius * 4.0f, 1.0f) * (0.6f + 0.4f * e);
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
        previousEnd = s.endSeconds();
    }
    return {};
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
        }
        // The lens is per shot rather than per sample: a focal length that slides through a shot is
        // a zoom, and a zoom is a thing you ask for, not a thing you get by accident.
        focalKeys.push_back(json{{"time", s.startSeconds},
                                 {"value", s.composition.focalLength},
                                 {"interp", "linear"}});
        apertureKeys.push_back(json{{"time", s.startSeconds},
                                    {"value", s.composition.aperture},
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

        if (e.contains("subject")) {
            const json& sub = e.at("subject");
            if (!sub.is_object()) {
                return fail("sequence '{}': shot '{}' subject must be an object", seq.name, s.name);
            }
            if (sub.contains("name") && sub.at("name").is_string()) {
                s.subject.name = sub.at("name").get<std::string>();
            }
            if (sub.contains("position") && sub.at("position").is_array() &&
                sub.at("position").size() == 3) {
                const auto& p = sub.at("position");
                s.subject.position = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
            }
            if (sub.contains("radius") && sub.at("radius").is_number()) {
                s.subject.radius = sub.at("radius").get<float>();
            }
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
        seq.shots.push_back(std::move(s));
    }
    if (auto ok = seq.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return seq;
}

} // namespace avgen::app
