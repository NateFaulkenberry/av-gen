#include "scene/flight.hpp"

#include "scene/tile_terrain.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

// ADR-1956. See the header for the five steps; this is them in order.

namespace avgen::scene {

namespace {

constexpr float kPi = 3.14159265358979f;

template <class T>
std::vector<T> smoothSamples(const std::vector<T>& in, float sigmaSamples) {
    // Gaussian, the ends extended by odd reflection (x_{-k} = 2 x_0 - x_k) so a linear end stays exactly linear.
    if (in.size() < 3 || !(sigmaSamples > 0.0f)) return in;
    const int r = static_cast<int>(std::ceil(3.0f * sigmaSamples));
    std::vector<float> k(static_cast<std::size_t>(2 * r + 1));
    float sum = 0.0f;
    for (int i = -r; i <= r; ++i) {
        k[static_cast<std::size_t>(i + r)] = std::exp(-0.5f * static_cast<float>(i * i) / (sigmaSamples * sigmaSamples));
        sum += k[static_cast<std::size_t>(i + r)];
    }
    std::vector<T> out(in.size());
    const int n = static_cast<int>(in.size());
    for (int i = 0; i < n; ++i) {
        T acc{};
        for (int j = -r; j <= r; ++j) {
            const int q = i + j;
            T v;
            if (q < 0) {
                v = in[0] * 2.0f - in[static_cast<std::size_t>(std::min(-q, n - 1))];
            } else if (q >= n) {
                v = in[static_cast<std::size_t>(n - 1)] * 2.0f -
                    in[static_cast<std::size_t>(std::max(2 * (n - 1) - q, 0))];
            } else {
                v = in[static_cast<std::size_t>(q)];
            }
            acc += v * (k[static_cast<std::size_t>(j + r)] / sum);
        }
        out[static_cast<std::size_t>(i)] = acc;
    }
    return out;
}

glm::vec2 catmull(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// The route as a dense polyline (x, z, altitude, speed per point), with its arc length.
struct Dense {
    std::vector<glm::vec2> xz;
    std::vector<float> altitude;
    std::vector<float> speed;
    std::vector<float> arc;
};

Dense densify(const FlightSpec& spec) {
    Dense d;
    const auto& r = spec.route;
    const int n = static_cast<int>(r.size());
    const int segments = spec.loop ? n : n - 1;
    const auto at = [&](int i) -> const FlightWaypoint& {
        if (spec.loop) return r[static_cast<std::size_t>(((i % n) + n) % n)];
        return r[static_cast<std::size_t>(std::clamp(i, 0, n - 1))];
    };
    const auto ghost = [&](int i) -> glm::vec2 {
        // the open route's ends: a reflected ghost point, so the end tangents run along the first and last legs
        if (spec.loop || (i >= 0 && i < n)) return at(i).position;
        if (i < 0) return 2.0f * r[0].position - r[1].position;
        return 2.0f * r[static_cast<std::size_t>(n - 1)].position - r[static_cast<std::size_t>(n - 2)].position;
    };
    const auto speedOf = [&](const FlightWaypoint& w) { return w.speed > 0.0f ? w.speed : spec.speed; };
    constexpr int kSteps = 64;
    for (int s = 0; s < segments; ++s) {
        for (int k = 0; k < kSteps; ++k) {
            const float t = static_cast<float>(k) / kSteps;
            const float e = t * t * (3.0f - 2.0f * t); // altitude and speed ease between waypoints
            d.xz.push_back(catmull(ghost(s - 1), ghost(s), ghost(s + 1), ghost(s + 2), t));
            d.altitude.push_back(lerpf(at(s).altitude, at(s + 1).altitude, e));
            d.speed.push_back(lerpf(speedOf(at(s)), speedOf(at(s + 1)), e));
        }
    }
    const FlightWaypoint& end = at(spec.loop ? 0 : n - 1);
    d.xz.push_back(end.position);
    d.altitude.push_back(end.altitude);
    d.speed.push_back(speedOf(end));
    d.arc.assign(d.xz.size(), 0.0f);
    for (std::size_t i = 1; i < d.xz.size(); ++i) d.arc[i] = d.arc[i - 1] + glm::length(d.xz[i] - d.xz[i - 1]);
    return d;
}

// The point at arc length `s` (past the end: straight on along the last tangent, or round again on a loop).
struct AlongOut {
    glm::vec2 xz;
    float altitude;
    float speed;
};

AlongOut along(const Dense& d, bool loop, float s) {
    const float total = d.arc.back();
    if (loop && total > 0.0f) {
        s = std::fmod(s, total);
        if (s < 0.0f) s += total;
    }
    if (s >= total) {
        const std::size_t last = d.xz.size() - 1;
        glm::vec2 tangent = d.xz[last] - d.xz[last - 1];
        tangent = glm::dot(tangent, tangent) > 1e-12f ? glm::normalize(tangent) : glm::vec2(1.0f, 0.0f);
        return {d.xz[last] + tangent * (s - total), d.altitude[last], d.speed[last]};
    }
    s = std::max(s, 0.0f);
    const auto it = std::upper_bound(d.arc.begin(), d.arc.end(), s);
    const std::size_t i = std::clamp<std::size_t>(static_cast<std::size_t>(it - d.arc.begin()), 1, d.arc.size() - 1);
    const float span = d.arc[i] - d.arc[i - 1];
    const float t = span > 1e-9f ? (s - d.arc[i - 1]) / span : 0.0f;
    return {glm::mix(d.xz[i - 1], d.xz[i], t), lerpf(d.altitude[i - 1], d.altitude[i], t),
            lerpf(d.speed[i - 1], d.speed[i], t)};
}

} // namespace

Result<void> validateFlight(const FlightSpec& f) {
    if (f.route.size() < 2) {
        return fail("flight: the route needs at least two waypoints");
    }
    for (const FlightWaypoint& w : f.route) {
        if (!std::isfinite(w.position.x) || !std::isfinite(w.position.y) || !std::isfinite(w.altitude) ||
            !std::isfinite(w.speed)) {
            return fail("flight: a waypoint's position, altitude and speed must be finite");
        }
    }
    const auto positive = [](float v) { return std::isfinite(v) && v > 0.0f; };
    if (!positive(f.speed) || !positive(f.duration) || f.duration > 7200.0f || !positive(f.climbRate) ||
        !positive(f.fallRate) || !positive(f.lookAhead) || !(f.clearanceRadius >= 0.0f) || !(f.smoothing >= 0.0f) ||
        !(f.groundSmoothing >= 0.0f) || !std::isfinite(f.minClearance) || !(f.amplitudeHeadroom >= 0.0f) ||
        !std::isfinite(f.startTime) || !std::isfinite(f.pitchBias)) {
        return fail("flight: speed, duration (<= 7200 s), climb and fall rates and lookAhead must be positive; the rest "
                    "finite and non-negative");
    }
    if (!(f.pitchMin < f.pitchMax) || f.pitchMin < -89.0f || f.pitchMax > 89.0f || !(f.bankLimit >= 0.0f) ||
        f.bankLimit > 60.0f || !std::isfinite(f.bankGain) || !(f.fov >= 0.0f) || f.fov > 150.0f) {
        return fail("flight: pitchMin < pitchMax within +-89, bankLimit 0..60, fov 0..150");
    }
    return {};
}

FlightGround tileFlightGround(const TileTerrainSpec& tiles, const TileDynamics& dyn, float pitch,
                              float amplitudeScale) {
    TileDynamics d = dyn;
    d.live.clear();
    d.amplitude *= amplitudeScale;
    return [tiles, d = std::move(d), pitch](float x, float z, float t) {
        return tileHeightAt(tiles, d, std::max(pitch, 1e-3f), x, z, t);
    };
}

float flightLiveMargin(const TileDynamics& dyn, float amplitudeHeadroom) {
    return std::abs(dyn.rings.amplitude * dyn.amplitude) * std::max(amplitudeHeadroom, 1.0f);
}

FlightTable buildFlightTable(const FlightSpec& spec, const FlightGround& ground, float liveMargin) {
    FlightTable out;
    if (!validateFlight(spec) || !ground) {
        return out;
    }
    const float rate = kFlightRate;
    const int n = static_cast<int>(spec.duration * rate) + 1;
    const Dense dense = densify(spec);
    // 1. the route by time: arc length advances at the local speed
    std::vector<glm::vec2> xz(static_cast<std::size_t>(n));
    std::vector<float> altitude(static_cast<std::size_t>(n));
    float s = 0.0f;
    for (int k = 0; k < n; ++k) {
        const AlongOut a = along(dense, spec.loop, s);
        xz[static_cast<std::size_t>(k)] = a.xz;
        altitude[static_cast<std::size_t>(k)] = a.altitude;
        s += a.speed / rate;
    }
    xz = smoothSamples(xz, spec.smoothing * rate);
    altitude = smoothSamples(altitude, spec.smoothing * rate);
    // 2. terrain following on the smoothed ground; 3. the clearance on the moving ground at each sample's time
    std::vector<float> under(static_cast<std::size_t>(n));
    std::vector<float> clear(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const float t = spec.startTime + static_cast<float>(k) / rate;
        const glm::vec2 p = xz[static_cast<std::size_t>(k)];
        const float here = ground(p.x, p.y, t);
        float highest = here;
        if (spec.clearanceRadius > 0.0f) {
            for (int a = 0; a < 12; ++a) {
                const float ang = static_cast<float>(a) * (2.0f * kPi / 12.0f);
                for (const float f : {0.5f, 1.0f}) {
                    const glm::vec2 q = p + spec.clearanceRadius * f * glm::vec2(std::cos(ang), std::sin(ang));
                    highest = std::max(highest, ground(q.x, q.y, t));
                }
            }
        }
        under[static_cast<std::size_t>(k)] = here;
        clear[static_cast<std::size_t>(k)] = highest + spec.minClearance + liveMargin;
    }
    under = smoothSamples(under, spec.groundSmoothing * rate);
    std::vector<float> ys(static_cast<std::size_t>(n));
    for (std::size_t k = 0; k < ys.size(); ++k) ys[k] = std::max(under[k] + altitude[k], clear[k]);
    // The cone envelope (falls at most fallRate, climbs at most climbRate), then smoothed. Never clamped to the
    // clearance after smoothing -- a clamp is a kink, and a kink is a jolt (a velocity step of metres per second).
    // Instead, rounds: each envelope is taken over max(path, clearance + a little), so whatever a smoothing shaved off
    // a peak is put back before the next, smaller smoothing, until the smooth path clears everywhere.
    const float kFall = spec.fallRate / rate;
    const float kClimb = spec.climbRate / rate;
    for (int round = 0; round < 8; ++round) {
        const float margin = round == 0 ? 0.0f : 0.25f * static_cast<float>(round);
        for (std::size_t i = 0; i < ys.size(); ++i) ys[i] = std::max(ys[i], clear[i] + margin);
        for (std::size_t i = 1; i < ys.size(); ++i) ys[i] = std::max(ys[i], ys[i - 1] - kFall);
        for (std::size_t i = ys.size() - 1; i-- > 0;) ys[i] = std::max(ys[i], ys[i + 1] - kClimb);
        ys = smoothSamples(ys, (round == 0 ? 0.9f : 0.5f) * rate);
        bool clears = true;
        for (std::size_t i = 0; i < ys.size() && clears; ++i) clears = ys[i] >= clear[i];
        if (clears) break;
    }
    std::vector<glm::vec3> eyes(static_cast<std::size_t>(n));
    for (std::size_t i = 0; i < eyes.size(); ++i) eyes[i] = glm::vec3(xz[i].x, ys[i], xz[i].y);
    // 4. aim along the path ahead, a little down, the pitch softly held, smoothed
    const int ahead = std::max(1, static_cast<int>(spec.lookAhead * rate));
    const int last = n - 1;
    std::vector<glm::vec3> aim(static_cast<std::size_t>(n));
    const float lo = glm::radians(spec.pitchMin);
    const float hi = glm::radians(spec.pitchMax);
    const float mid = 0.5f * (lo + hi);
    const float half = 0.5f * (hi - lo);
    for (int i = 0; i <= last; ++i) {
        glm::vec3 target;
        if (i + ahead <= last) {
            target = eyes[static_cast<std::size_t>(i + ahead)];
        } else {
            const int back = std::max(0, last - 15);
            const glm::vec3 tangent = (eyes[static_cast<std::size_t>(last)] - eyes[static_cast<std::size_t>(back)]) /
                                      static_cast<float>(std::max(last - back, 1));
            target = eyes[static_cast<std::size_t>(last)] + tangent * static_cast<float>(i + ahead - last);
        }
        glm::vec3 d = target - eyes[static_cast<std::size_t>(i)];
        d.y -= spec.pitchBias * glm::length(d);
        const float flat = std::max(glm::length(glm::vec2(d.x, d.z)), 1e-3f);
        const float raw = std::atan2(d.y, flat);
        const float pitch = half > 0.0f ? mid + half * std::tanh((raw - mid) / half) : mid;
        const glm::vec2 h = glm::vec2(d.x, d.z) / flat;
        aim[static_cast<std::size_t>(i)] = glm::vec3(h.x * std::cos(pitch), std::sin(pitch), h.y * std::cos(pitch));
    }
    {
        const std::vector<glm::vec3> sm = smoothSamples(aim, 0.5f * rate);
        for (std::size_t i = 0; i < aim.size(); ++i) aim[i] = glm::normalize(sm[i]);
    }
    // 5. bank toward the turn, from the turn rate, through a tanh limit
    std::vector<float> heading(aim.size());
    for (std::size_t i = 0; i < aim.size(); ++i) heading[i] = std::atan2(aim[i].z, aim[i].x);
    std::vector<float> turn(aim.size(), 0.0f);
    for (std::size_t i = 1; i + 1 < aim.size(); ++i) {
        float dh = heading[i + 1] - heading[i - 1];
        while (dh > kPi) dh -= 2.0f * kPi;
        while (dh < -kPi) dh += 2.0f * kPi;
        turn[i] = dh * rate * 0.5f; // radians per second
    }
    turn = smoothSamples(turn, 0.8f * rate);
    const float limit = glm::radians(spec.bankLimit);
    out.eye.resize(eyes.size());
    out.aim = aim;
    out.clearance = clear;
    for (std::size_t i = 0; i < eyes.size(); ++i) {
        // degrees of bank per degree/second of turn = bankGain, so radians/radians the same ratio
        const float roll = limit > 0.0f ? limit * std::tanh(-spec.bankGain * turn[i] / limit) : 0.0f;
        out.eye[i] = glm::vec4(eyes[i], roll);
    }
    return out;
}

FlightPose flightPoseAt(const FlightSpec& spec, const FlightTable& table, double t) {
    FlightPose pose;
    if (!table.valid()) return pose;
    const auto n = static_cast<int>(table.eye.size());
    const double f = std::clamp((t - static_cast<double>(spec.startTime)) * kFlightRate, 0.0, static_cast<double>(n - 1));
    const int i = std::min(static_cast<int>(f), n - 2);
    const auto a = static_cast<float>(f - i);
    const glm::vec4 e = glm::mix(table.eye[static_cast<std::size_t>(i)], table.eye[static_cast<std::size_t>(i) + 1], a);
    const glm::vec3 d = glm::normalize(
        glm::mix(table.aim[static_cast<std::size_t>(i)], table.aim[static_cast<std::size_t>(i) + 1], a));
    pose.eye = glm::vec3(e);
    pose.target = pose.eye + 100.0f * d;
    pose.rollDegrees = glm::degrees(e.w);
    return pose;
}

// ---- JSON ----

namespace {

constexpr std::pair<const char*, float FlightSpec::*> kFlightFloats[] = {
    {"startTime", &FlightSpec::startTime},     {"duration", &FlightSpec::duration},
    {"speed", &FlightSpec::speed},             {"minClearance", &FlightSpec::minClearance},
    {"clearanceRadius", &FlightSpec::clearanceRadius}, {"amplitudeHeadroom", &FlightSpec::amplitudeHeadroom},
    {"climbRate", &FlightSpec::climbRate},     {"fallRate", &FlightSpec::fallRate},
    {"smoothing", &FlightSpec::smoothing},     {"groundSmoothing", &FlightSpec::groundSmoothing},
    {"lookAhead", &FlightSpec::lookAhead},     {"pitchBias", &FlightSpec::pitchBias},
    {"pitchMin", &FlightSpec::pitchMin},       {"pitchMax", &FlightSpec::pitchMax},
    {"bankLimit", &FlightSpec::bankLimit},     {"bankGain", &FlightSpec::bankGain},
    {"fov", &FlightSpec::fov},
};

} // namespace

nlohmann::json flightToJson(const FlightSpec& f) {
    nlohmann::json j = nlohmann::json::object();
    j["enabled"] = f.enabled;
    j["ground"] = f.ground;
    j["loop"] = f.loop;
    for (const auto& [key, field] : kFlightFloats) j[key] = f.*field;
    nlohmann::json route = nlohmann::json::array();
    for (const FlightWaypoint& w : f.route) {
        nlohmann::json o = {{"position", nlohmann::json::array({w.position.x, w.position.y})}, {"altitude", w.altitude}};
        if (w.speed > 0.0f) o["speed"] = w.speed;
        route.push_back(std::move(o));
    }
    j["route"] = std::move(route);
    return j;
}

Result<FlightSpec> flightFromJson(const nlohmann::json& j) {
    if (!j.is_object()) return fail("'flight' must be an object");
    FlightSpec f;
    f.enabled = true;
    for (const auto& [key, value] : j.items()) {
        if (key == "enabled" || key == "loop") {
            if (!value.is_boolean()) return fail("'flight.{}' must be true or false", key);
            (key == "enabled" ? f.enabled : f.loop) = value.get<bool>();
            continue;
        }
        if (key == "ground") {
            if (!value.is_string()) return fail("'flight.ground' must be a node name");
            f.ground = value.get<std::string>();
            continue;
        }
        if (key == "route") {
            if (!value.is_array()) return fail("'flight.route' must be an array of waypoints");
            for (const auto& w : value) {
                FlightWaypoint p;
                if (!w.is_object()) return fail("'flight.route' waypoints must be objects");
                for (const auto& [wk, wv] : w.items()) {
                    if (wk == "position") {
                        if (!wv.is_array() || wv.size() != 2 || !wv[0].is_number() || !wv[1].is_number()) {
                            return fail("'flight.route[].position' must be [x, z]");
                        }
                        p.position = {wv[0].get<float>(), wv[1].get<float>()};
                    } else if (wk == "altitude" || wk == "speed") {
                        if (!wv.is_number()) return fail("'flight.route[].{}' must be a number", wk);
                        (wk == "altitude" ? p.altitude : p.speed) = wv.get<float>();
                    } else {
                        return fail("'flight.route[].{}' is not a key this build reads", wk);
                    }
                }
                f.route.push_back(p);
            }
            continue;
        }
        bool found = false;
        for (const auto& [name, field] : kFlightFloats) {
            if (key == name) {
                if (!value.is_number()) return fail("'flight.{}' must be a number", key);
                f.*field = value.get<float>();
                found = true;
                break;
            }
        }
        if (!found) return fail("'flight.{}' is not a key this build reads", key);
    }
    if (auto r = validateFlight(f); !r) return std::unexpected(r.error());
    return f;
}

} // namespace avgen::scene
