#include "scene/corruption.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace avgen::scene {

namespace {

using json = nlohmann::json;

struct FloatKey {
    const char* key;
    float Corruption::*field;
    float lo, hi, softLo, softHi;
};

constexpr std::array<FloatKey, 31> kFloats{{
    {"amount", &Corruption::amount, 0.0f, 1.0f, 0.0f, 1.0f},
    {"radius", &Corruption::radius, 1.0f, 1.0e5f, 10.0f, 2000.0f},
    {"inner", &Corruption::inner, 0.0f, 0.99f, 0.0f, 0.9f},
    {"fieldNear", &Corruption::fieldNear, 0.0f, 1.0e5f, 0.0f, 1000.0f},
    {"fieldFar", &Corruption::fieldFar, 0.0f, 1.0e5f, 0.0f, 5000.0f},
    {"open", &Corruption::open, 0.0f, 1.0f, 0.0f, 1.0f},
    {"openNear", &Corruption::openNear, 0.0f, 1.0e5f, 0.0f, 500.0f},
    {"openFar", &Corruption::openFar, 0.0f, 1.0e5f, 0.0f, 2000.0f},
    {"width", &Corruption::width, 0.1f, 1000.0f, 1.0f, 100.0f},
    {"height", &Corruption::height, 0.1f, 1000.0f, 1.0f, 100.0f},
    {"depth", &Corruption::depth, 0.0f, 1000.0f, 0.0f, 100.0f},
    {"seam", &Corruption::seam, 0.01f, 0.99f, 0.05f, 0.9f},
    {"block", &Corruption::block, 0.25f, 200.0f, 1.0f, 40.0f},
    {"share", &Corruption::share, 0.0f, 1.0f, 0.0f, 1.0f},
    {"slip", &Corruption::slip, 0.0f, 200.0f, 0.0f, 20.0f},
    {"rate", &Corruption::rate, 0.0f, 30.0f, 0.0f, 4.0f},
    {"strip", &Corruption::strip, 0.25f, 200.0f, 1.0f, 30.0f},
    {"stripSlip", &Corruption::stripSlip, 0.0f, 200.0f, 0.0f, 20.0f},
    {"slide", &Corruption::slide, 0.0f, 100.0f, 0.0f, 4.0f},
    {"pull", &Corruption::pull, 0.0f, 1000.0f, 0.0f, 60.0f},
    {"lift", &Corruption::lift, 0.0f, 200.0f, 0.0f, 20.0f},
    {"fold", &Corruption::fold, 0.0f, 200.0f, 0.0f, 10.0f},
    {"lag", &Corruption::lag, 0.0f, 1.0f, 0.0f, 1.0f},
    {"seed", &Corruption::seed, 0.0f, 1.0e6f, 0.0f, 100.0f},
    {"glyph", &Corruption::glyph, 0.0f, 1000.0f, 0.0f, 20.0f},
    {"glyphSize", &Corruption::glyphSize, 0.05f, 100.0f, 0.25f, 8.0f},
    {"glow", &Corruption::glow, 0.0f, 1000.0f, 0.0f, 40.0f},
    {"echoOffset", &Corruption::echoOffset, 0.0f, 2.0f, 0.0f, 1.0f},
    {"light", &Corruption::light, 0.0f, 1.0e8f, 0.0f, 100000.0f},
    {"floor", &Corruption::floor, -1.0e9f, 1.0e9f, -500.0f, 2000.0f},
    {"front", &Corruption::front, 0.5f, 1000.0f, 1.0f, 60.0f},
}};
constexpr std::size_t kFloatCount = kFloats.size();

bool readVec3(const json& j, glm::vec3& out) {
    if (!j.is_array() || j.size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        const json& e = j[static_cast<std::size_t>(i)];
        if (!e.is_number() || !std::isfinite(e.get<double>())) return false;
        out[i] = e.get<float>();
    }
    return true;
}

float smooth(float a, float b, float x) {
    if (a == b) return x < a ? 0.0f : 1.0f;
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---- the CPU twin of shaders/corruption.wgsl --------------------------------------------------------------------

float crHash(std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3) {
    std::uint32_t v[4] = {a0 * 1664525u + 1013904223u, a1 * 1664525u + 1013904223u, a2 * 1664525u + 1013904223u,
                          a3 * 1664525u + 1013904223u};
    v[0] += v[1] * v[3];
    v[1] += v[2] * v[0];
    v[2] += v[0] * v[1];
    v[3] += v[1] * v[2];
    for (auto& x : v) x ^= x >> 16u;
    v[0] += v[1] * v[3];
    v[1] += v[2] * v[0];
    v[2] += v[0] * v[1];
    v[3] += v[1] * v[2];
    return static_cast<float>(v[0] >> 8u) / 16777216.0f;
}

std::uint32_t bits(std::int32_t i) { return static_cast<std::uint32_t>(i); }

} // namespace

bool corruptionTargets(const Corruption& c, const std::string& name) {
    for (const std::string& t : c.targets) {
        if (!t.empty() && name.compare(0, t.size(), t) == 0) return true;
    }
    return false;
}

float corruptionFieldScale(const Corruption& c, float cameraDistance) {
    // squared: the field gathers slowly from far away and closes in fast near the wall
    const float s = smooth(c.fieldFar, c.fieldNear, cameraDistance);
    return std::clamp(c.amount, 0.0f, 1.0f) * s * s;
}

float corruptionOpening(const Corruption& c, float cameraDistance) {
    const float approach = smooth(c.openFar, c.openNear, cameraDistance);
    return std::clamp(c.open + approach, 0.0f, 1.0f);
}

std::array<glm::vec4, kCorruptionVec4s> packCorruption(const Corruption& c, bool active) {
    std::array<glm::vec4, kCorruptionVec4s> p{};
    if (!active) return p;
    glm::vec3 axis = glm::length(c.axis) > 1e-6f ? glm::normalize(c.axis) : glm::vec3(0.0f, 0.0f, -1.0f);
    p[0] = glm::vec4(c.focus, 1.0f);
    p[1] = glm::vec4(c.liveAmount, c.radius, c.inner, c.prevTime);
    p[2] = glm::vec4(axis, c.liveOpen);
    p[3] = glm::vec4(c.width, c.height, c.depth, c.seam);
    p[4] = glm::vec4(c.block, c.share, c.slip, c.rate);
    p[5] = glm::vec4(c.strip, c.stripSlip, c.slide, c.pull);
    p[6] = glm::vec4(c.lift, c.fold, c.lag, std::floor(c.seed));
    p[7] = glm::vec4(c.glyph, c.glyphSize, c.glow, c.echoOffset);
    p[8] = glm::vec4(c.glowColor, static_cast<float>(std::clamp(c.echoes, 0, kMaxCorruptionEchoes)));
    p[9] = glm::vec4(std::max(c.floor, -1.0e6f), c.front, 0.0f, 0.0f); // f32 keeps floor + 2 distinct
    return p;
}

CorruptedPoint corruptPoint(const std::array<glm::vec4, kCorruptionVec4s>& f, glm::vec3 p0, float t, int echo) {
    CorruptedPoint out{p0, 0.0f, echo == 0};
    if (!(f[0].w > 0.5f)) return out;
    const auto field = [&](glm::vec3 p) {
        const float r = f[1].y;
        return f[1].x * (1.0f - smooth(r * f[1].z, r, glm::distance(p, glm::vec3(f[0]))));
    };
    const glm::vec3 ax = glm::normalize(glm::vec3(f[2]));
    glm::vec3 h = glm::cross(ax, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(h, h) < 1e-6f) h = glm::vec3(1.0f, 0.0f, 0.0f);
    h = glm::normalize(h);
    const glm::vec3 vv = glm::cross(h, ax);
    const std::uint32_t seed = bits(static_cast<std::int32_t>(f[6].w));
    glm::vec3 off(0.0f);
    float keepOfPull = 1.0f;
    const float B = std::max(f[4].x, 0.25f);
    const glm::ivec3 cell(static_cast<int>(std::floor(p0.x / B)), static_cast<int>(std::floor(p0.y / B)),
                          static_cast<int>(std::floor(p0.z / B)));
    const glm::vec3 cc = (glm::vec3(cell) + 0.5f) * B;
    const float fc = field(cc);
    if (fc > 0.0f) {
        const float ph = t * f[4].w + crHash(bits(cell.x), bits(cell.y), bits(cell.z), seed + 1u);
        const float ep = std::floor(ph);
        const float fr = ph - ep;
        const std::uint32_t epu = bits(static_cast<std::int32_t>(ep));
        const std::uint32_t k0 = bits(cell.x), k1 = bits(cell.y), k2 = bits(cell.z) ^ (epu * 747796405u);
        if (crHash(k0, k1, k2, seed + 2u) < fc * f[4].y) {
            const float env = smooth(0.0f, 0.25f, fr) * (1.0f - smooth(0.6f, 1.0f, fr));
            const float r1 = crHash(k0, k1, k2, seed + 3u), r2 = crHash(k0, k1, k2, seed + 4u),
                        r3 = crHash(k0, k1, k2, seed + 5u), r4 = crHash(k0, k1, k2, seed + 6u);
            const glm::vec3 dir =
                glm::normalize(h * (2.0f * r1 - 1.0f) + vv * (r2 - 0.5f) * 0.8f + glm::vec3(1e-4f, 0.0f, 0.0f));
            glm::vec3 slip = (dir * f[4].z - ax * f[6].x * r3) * fc * env;
            if (echo > 0) {
                const float k = static_cast<float>(echo) / (f[8].w + 1.0f);
                slip *= 1.0f - k * f[7].w;
                out.keep = env > 0.02f;
            }
            off += slip;
            out.uvShift = (2.0f * r4 - 1.0f) * f[5].z * fc * env;
        }
        if (crHash(bits(cell.x), bits(cell.y), bits(cell.z), seed + 7u) < fc * 0.3f) keepOfPull = 1.0f - f[6].z;
    }
    const float fp = field(p0);
    if (fp > 0.0f) {
        const int s = static_cast<int>(std::floor(glm::dot(p0, vv) / std::max(f[5].x, 0.25f)));
        const float hs = crHash(bits(s), 0u, seed, 9u);
        off += h * f[5].y * fp * (2.0f * hs - 1.0f) * (0.6f + 0.4f * std::sin(t * 0.5f + hs * 6.2831853f)) * keepOfPull;
        const glm::vec3 toF = glm::vec3(f[0]) - p0;
        const float dl = glm::length(toF);
        const glm::vec3 q = p0 - glm::vec3(f[0]);
        const float eFull = std::sqrt(std::pow(glm::dot(q, h) / f[3].x, 2.0f) + std::pow(glm::dot(q, vv) / f[3].y, 2.0f));
        const float ring = smooth(1.4f, 2.2f, eFull) * (1.0f - smooth(4.0f, 9.0f, eFull));
        const glm::vec3 tang = toF - ax * glm::dot(toF, ax);
        const float pull = std::min(f[5].w * fp * fp * ring, 0.3f * dl);
        off += tang / std::max(glm::length(tang), 1e-3f) * pull;
        off += ax * f[6].y * fp * std::sin(dl * 0.15f - t * 0.7f);
    }
    if (echo > 0) {
        const glm::vec3 q = p0 - glm::vec3(f[0]);
        const float eFull = std::sqrt(std::pow(glm::dot(q, h) / f[3].x, 2.0f) + std::pow(glm::dot(q, vv) / f[3].y, 2.0f));
        out.keep = out.keep && eFull > 1.6f;
    } else if (f[2].w > 0.0f) {
        const glm::vec3 q = p0 - glm::vec3(f[0]);
        const float u = glm::dot(q, h), v = glm::dot(q, vv), w = glm::dot(q, ax);
        const float seam = std::max(f[3].w, 1e-3f);
        const float seamT = smooth(0.0f, seam, f[2].w), openT = smooth(seam, 1.0f, f[2].w);
        const float a = f[3].x * (0.08f + (1.0f - 0.08f) * seamT);
        const float hh = 0.35f + (f[3].y - 0.35f) * openT;
        const int cx = static_cast<int>(std::floor(u / 2.5f)), cy = static_cast<int>(std::floor(v / 2.5f));
        const float ang = std::atan2(v / std::max(hh, 1e-3f), u / std::max(a, 1e-3f));
        const float wander = 0.10f * std::sin(ang * 3.0f + 1.3f) + 0.06f * std::sin(ang * 7.0f + 4.1f) +
                             0.04f * std::sin(ang * 13.0f + 2.2f);
        const float rag = 1.0f + wander + 0.07f * (crHash(bits(cx), bits(cy), seed, 11u) - 0.5f);
        const float e = std::sqrt((u / a) * (u / a) + (v / hh) * (v / hh)) * rag;
        const float front = std::max(f[9].y, 0.5f);
        const float wd = w >= 0.0f ? 1.0f - smooth(10.0f, 30.0f, w) : 1.0f - smooth(front / 3.0f, front, -w); // crReach
        if (wd > 0.0f && openT > 0.0f) {
            const float ee = std::max(e, 1e-3f);
            if (e < 1.0f) {
                const float band = std::clamp((1.0f - e) / 0.25f, 0.0f, 1.0f);
                const float k = 1.0f / ee - 1.0f;
                off += ((h * u + vv * v) * k + ax * f[3].z * band) * openT * wd;
            } else if (e < 1.9f) {
                const int cx2 = static_cast<int>(std::floor(u / 6.0f)), cy2 = static_cast<int>(std::floor(v / 2.5f));
                const glm::vec2 ccu((static_cast<float>(cx2) + 0.5f) * 6.0f, (static_cast<float>(cy2) + 0.5f) * 2.5f);
                const float ce = glm::length(glm::vec2(ccu.x / std::max(f[3].x, 1e-3f), ccu.y / std::max(f[3].y, 1e-3f)));
                const float lip = std::clamp(1.0f - (ce - 1.0f) / 0.9f, 0.0f, 1.0f) * openT * wd;
                const float h1 = crHash(bits(cx2), bits(cy2), seed, 41u), h2 = crHash(bits(cx2), bits(cy2), seed, 42u),
                            h3 = crHash(bits(cx2), bits(cy2), seed, 43u);
                const glm::vec3 outward = glm::normalize(h * ccu.x + vv * ccu.y + glm::vec3(1e-4f, 0.0f, 0.0f));
                off += (ax * (h1 * 2.0f - 0.6f) * 4.0f + outward * h2 * 3.0f + vv * (h3 - 0.5f) * 1.5f) * lip;
            }
        }
    }
    // a river bed stays under its water: nothing at rest below the floor moves
    const float keep = smooth(f[9].x, f[9].x + 2.0f, p0.y);
    out.position = p0 + off * keep;
    out.uvShift *= keep;
    return out;
}

Result<Corruption> corruptionFromJson(const json& j) {
    if (!j.is_object()) return fail("'corruption' must be an object");
    Corruption c;
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& key = it.key();
        if (key == "targets") {
            if (!it->is_array()) return fail("corruption 'targets' must be an array of entity-name prefixes");
            for (const json& t : *it) {
                if (!t.is_string()) return fail("corruption 'targets' must be an array of strings");
                c.targets.push_back(t.get<std::string>());
            }
            continue;
        }
        if (key == "focus" || key == "axis" || key == "glowColor") {
            glm::vec3 v;
            if (!readVec3(*it, v)) return fail("corruption '{}' must be three finite numbers", key);
            if (key == "focus") c.focus = v;
            else if (key == "axis") c.axis = v;
            else c.glowColor = v;
            continue;
        }
        if (key == "echoes") {
            if (!it->is_number_integer() || it->get<int>() < 0 || it->get<int>() > kMaxCorruptionEchoes) {
                return fail("corruption 'echoes' must be an integer in [0, {}]", kMaxCorruptionEchoes);
            }
            c.echoes = it->get<int>();
            continue;
        }
        bool known = false;
        for (std::size_t k = 0; k < kFloatCount; ++k) {
            const FloatKey& f = kFloats[k];
            if (key != f.key) continue;
            known = true;
            if (!it->is_number() || !std::isfinite(it->get<double>())) return fail("corruption '{}' must be a finite number", key);
            const float v = it->get<float>();
            if (v < f.lo || v > f.hi) return fail("corruption '{}' must be in [{}, {}]", key, f.lo, f.hi);
            c.*(f.field) = v;
        }
        if (!known) return fail("corruption has no key '{}'", key);
    }
    if (glm::length(c.axis) < 1e-6f) return fail("corruption 'axis' must not be zero");
    return c;
}

json corruptionToJson(const Corruption& c) {
    json j = json::object();
    j["targets"] = c.targets;
    j["focus"] = json::array({c.focus.x, c.focus.y, c.focus.z});
    j["axis"] = json::array({c.axis.x, c.axis.y, c.axis.z});
    j["glowColor"] = json::array({c.glowColor.x, c.glowColor.y, c.glowColor.z});
    j["echoes"] = c.echoes;
    for (std::size_t k = 0; k < kFloatCount; ++k) j[kFloats[k].key] = c.*(kFloats[k].field);
    return j;
}

CorruptionParameters registerCorruptionParameters(params::ParameterSet& params, const Corruption& rest,
                                                  const std::string& prefix) {
    CorruptionParameters out;
    for (std::size_t k = 0; k < kFloatCount; ++k) {
        const FloatKey& f = kFloats[k];
        params::ParamDesc<float> d;
        d.path = prefix + f.key;
        d.label = f.key;
        d.group = "Corruption";
        d.defaultValue = rest.*(f.field);
        d.hardMin = f.lo;
        d.hardMax = f.hi;
        d.softMin = f.softLo;
        d.softMax = f.softHi;
        out.all.push_back(&params.add(std::move(d)));
    }
    return out;
}

void applyCorruptionParameters(const CorruptionParameters& p, const Corruption& rest, Corruption& live) {
    live = rest;
    for (std::size_t k = 0; k < kFloatCount && k < p.all.size(); ++k) {
        if (const auto* f = dynamic_cast<const params::Parameter<float>*>(p.all[k])) live.*(kFloats[k].field) = f->value();
    }
}

} // namespace avgen::scene
