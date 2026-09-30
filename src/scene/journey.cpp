#include "scene/journey.hpp"

#include "core/noise.hpp"

#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace avgen::scene {

namespace {

constexpr float kTwoPi = 6.283185307179586f;
constexpr int kSamplesPerSegment = 128;

glm::vec3 rotateY(const glm::vec3& p, float angle) {
    // The atan2(z, x) direction, as the SDF screw and polarRepeat measure angles.
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {p.x * c - p.z * s, p.y, p.x * s + p.z * c};
}

float knot(const glm::vec3& a, const glm::vec3& b) {
    return std::max(std::sqrt(glm::length(b - a)), 1e-3f); // centripetal: |d|^0.5
}

} // namespace

glm::vec3 JourneyScrew::applyPoint(const glm::vec3& p, int k) const {
    if (count <= 0) {
        return p + static_cast<float>(k) * translation;
    }
    const float sector = kTwoPi / static_cast<float>(count);
    glm::vec3 q = rotateY(p, sector * static_cast<float>(k % count));
    q.y += static_cast<float>(k) * translation.y;
    return q;
}

glm::vec3 JourneyScrew::applyDirection(const glm::vec3& d, int k) const {
    if (count <= 0) {
        return d;
    }
    const float sector = kTwoPi / static_cast<float>(count);
    return rotateY(d, sector * static_cast<float>(k % count));
}

nlohmann::json JourneySettings::toJson() const {
    nlohmann::json j;
    nlohmann::json pts = nlohmann::json::array();
    for (const glm::vec3& p : path) {
        pts.push_back({p.x, p.y, p.z});
    }
    j["path"] = std::move(pts);
    j["screw"] = {{"translation", {screw.translation.x, screw.translation.y, screw.translation.z}},
                  {"count", screw.count}};
    if (wrapCells != 0) {
        j["wrapCells"] = wrapCells;
    }
    if (!collide.empty()) {
        j["collide"] = collide;
    }
    j["radius"] = radius;
    return j;
}

Result<JourneySettings> JourneySettings::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("journey: must be an object");
    }
    JourneySettings s;
    const auto vec3 = [](const nlohmann::json& a) -> std::optional<glm::vec3> {
        if (!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
            return std::nullopt;
        }
        return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
    };
    if (!j.contains("path") || !j["path"].is_array()) {
        return fail("journey: 'path' must be an array of [x, y, z] points");
    }
    for (const auto& p : j["path"]) {
        auto v = vec3(p);
        if (!v) {
            return fail("journey: every path point must be [x, y, z]");
        }
        s.path.push_back(*v);
    }
    if (j.contains("screw")) {
        const auto& sc = j["screw"];
        if (!sc.is_object()) {
            return fail("journey: 'screw' must be an object");
        }
        if (sc.contains("translation")) {
            auto v = vec3(sc["translation"]);
            if (!v) {
                return fail("journey: screw 'translation' must be [x, y, z]");
            }
            s.screw.translation = *v;
        }
        if (sc.contains("count")) {
            if (!sc["count"].is_number_integer() || sc["count"].get<int>() < 0) {
                return fail("journey: screw 'count' must be an integer >= 0");
            }
            s.screw.count = sc["count"].get<int>();
        }
    }
    if (j.contains("wrapCells")) {
        if (!j["wrapCells"].is_number_integer() || j["wrapCells"].get<int>() < 0) {
            return fail("journey: 'wrapCells' must be an integer >= 0");
        }
        s.wrapCells = j["wrapCells"].get<int>();
    }
    if (j.contains("collide")) {
        if (!j["collide"].is_string()) {
            return fail("journey: 'collide' must be an SDF node name");
        }
        s.collide = j["collide"].get<std::string>();
    }
    if (j.contains("radius")) {
        if (!j["radius"].is_number() || j["radius"].get<float>() < 0.0f) {
            return fail("journey: 'radius' must be a number >= 0");
        }
        s.radius = j["radius"].get<float>();
    }
    return s;
}

Result<JourneyPath> JourneyPath::build(const JourneySettings& settings) {
    if (settings.path.size() < 2) {
        return fail("journey: the path needs at least two points (got {})", settings.path.size());
    }
    JourneyPath jp;
    jp.settings_ = settings;
    const int base = settings.screw.count > 0 ? settings.screw.count : 1;
    jp.wrapCells_ = settings.wrapCells > 0 ? settings.wrapCells : base;
    if (jp.wrapCells_ % base != 0) {
        return fail("journey: wrapCells ({}) must be a multiple of the screw's cells per turn ({})", jp.wrapCells_, base);
    }
    const auto n = static_cast<int>(settings.path.size());
    const auto Q = [&](int i) {
        const int j = static_cast<int>(std::floor(static_cast<double>(i) / n));
        return settings.screw.applyPoint(settings.path[static_cast<std::size_t>(i - j * n)], j);
    };
    if (glm::length(Q(n) - Q(0)) < 1e-4f && settings.screw.count <= 0) {
        return fail("journey: the screw does not move the path (a zero translation)");
    }
    for (int i = 0; i < n; ++i) {
        Segment seg;
        seg.p0 = Q(i - 1);
        seg.p1 = Q(i);
        seg.p2 = Q(i + 1);
        seg.p3 = Q(i + 2);
        seg.t0 = 0.0f;
        seg.t1 = seg.t0 + knot(seg.p0, seg.p1);
        seg.t2 = seg.t1 + knot(seg.p1, seg.p2);
        seg.t3 = seg.t2 + knot(seg.p2, seg.p3);
        jp.segments_.push_back(seg);
    }
    float total = 0.0f;
    glm::vec3 prev = jp.segmentPoint(jp.segments_[0], 0.0f);
    jp.tableS_.push_back(0.0f);
    jp.tableU_.push_back(0.0f);
    for (int i = 0; i < n; ++i) {
        for (int k = 1; k <= kSamplesPerSegment; ++k) {
            const float u = static_cast<float>(k) / static_cast<float>(kSamplesPerSegment);
            const glm::vec3 p = jp.segmentPoint(jp.segments_[static_cast<std::size_t>(i)], u);
            total += glm::length(p - prev);
            prev = p;
            jp.tableS_.push_back(total);
            jp.tableU_.push_back(static_cast<float>(i) + u);
        }
    }
    if (total < 1e-3f) {
        return fail("journey: the path has no length");
    }
    jp.cellLength_ = total;
    return jp;
}

glm::vec3 JourneyPath::segmentPoint(const Segment& s, float u) const {
    // Barry-Goldman pyramid for the centripetal Catmull-Rom between p1 and p2.
    const float t = s.t1 + (s.t2 - s.t1) * u;
    const glm::vec3 a1 = (s.t1 - t) / (s.t1 - s.t0) * s.p0 + (t - s.t0) / (s.t1 - s.t0) * s.p1;
    const glm::vec3 a2 = (s.t2 - t) / (s.t2 - s.t1) * s.p1 + (t - s.t1) / (s.t2 - s.t1) * s.p2;
    const glm::vec3 a3 = (s.t3 - t) / (s.t3 - s.t2) * s.p2 + (t - s.t2) / (s.t3 - s.t2) * s.p3;
    const glm::vec3 b1 = (s.t2 - t) / (s.t2 - s.t0) * a1 + (t - s.t0) / (s.t2 - s.t0) * a2;
    const glm::vec3 b2 = (s.t3 - t) / (s.t3 - s.t1) * a2 + (t - s.t1) / (s.t3 - s.t1) * a3;
    return (s.t2 - t) / (s.t2 - s.t1) * b1 + (t - s.t1) / (s.t2 - s.t1) * b2;
}

glm::vec3 JourneyPath::local(double u) const {
    const float s = static_cast<float>(std::clamp(u, 0.0, static_cast<double>(cellLength_)));
    const auto it = std::upper_bound(tableS_.begin(), tableS_.end(), s);
    const std::size_t hi = std::min<std::size_t>(static_cast<std::size_t>(it - tableS_.begin()), tableS_.size() - 1);
    const std::size_t lo = hi == 0 ? 0 : hi - 1;
    const float span = tableS_[hi] - tableS_[lo];
    const float f = span > 0.0f ? (s - tableS_[lo]) / span : 0.0f;
    const float g = tableU_[lo] + (tableU_[hi] - tableU_[lo]) * f;
    const int seg = std::min(static_cast<int>(std::floor(g)), static_cast<int>(segments_.size()) - 1);
    return segmentPoint(segments_[static_cast<std::size_t>(seg)], g - static_cast<float>(seg));
}

JourneySample JourneyPath::sample(double s) const {
    const auto position = [&](double at) {
        const double L = static_cast<double>(cellLength_);
        const double cell = std::floor(at / L);
        return settings_.screw.applyPoint(local(at - cell * L), static_cast<int>(cell));
    };
    JourneySample out;
    out.position = position(s);
    const glm::vec3 d = position(s + 0.02) - position(s - 0.02);
    out.tangent = glm::length(d) > 1e-8f ? glm::normalize(d) : glm::vec3(1.0f, 0.0f, 0.0f);
    return out;
}

long long JourneyPath::wraps(double s) const {
    return static_cast<long long>(std::floor(s / wrapLength()));
}

JourneySample JourneyPath::sampleWrapped(double s, double reference) const {
    return sample(s - static_cast<double>(wraps(reference)) * wrapLength());
}

JourneyPose journeyPose(const JourneyPath& path, const JourneyView& view) {
    const JourneySample at = path.sampleWrapped(view.distance, view.distance);
    const JourneySample ahead = path.sampleWrapped(view.distance + std::max(view.lookAhead, 0.05f), view.distance);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    JourneyPose pose;
    const float phase = kTwoPi * static_cast<float>(view.distance / std::max(view.stride, 0.05f));
    pose.eye = at.position + up * (view.height + view.bob * std::sin(phase));
    glm::vec3 dir = ahead.position - at.position;
    if (glm::length(dir) < 1e-6f) {
        dir = at.tangent;
    }
    dir = glm::normalize(dir);
    const float t = static_cast<float>(view.time) * view.swayRate;
    const float swayYaw = view.swayDegrees * (noise::valueNoise(glm::vec3(t, 0.5f, 0.25f), 7u) * 2.0f - 1.0f);
    const float swayPitch = 0.4f * view.swayDegrees * (noise::valueNoise(glm::vec3(t, 3.5f, 1.75f), 11u) * 2.0f - 1.0f);
    const float yaw = glm::radians(view.yawDegrees + swayYaw);
    const float horizontal = glm::length(glm::vec2(dir.x, dir.z));
    float elevation = std::atan2(dir.y, horizontal);
    glm::vec2 heading = horizontal > 1e-6f ? glm::vec2(dir.x, dir.z) / horizontal : glm::vec2(0.0f, -1.0f);
    // +yaw turns left (counter-clockwise seen from above): -Z goes to -X.
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    heading = glm::vec2(heading.x * c + heading.y * s, -heading.x * s + heading.y * c);
    elevation = std::clamp(elevation + glm::radians(view.pitchDegrees + swayPitch), glm::radians(-85.0f),
                           glm::radians(85.0f));
    const glm::vec3 look(std::cos(elevation) * heading.x, std::sin(elevation), std::cos(elevation) * heading.y);
    pose.target = pose.eye + look * std::max(view.lookAhead, 1.0f);
    return pose;
}

} // namespace avgen::scene
