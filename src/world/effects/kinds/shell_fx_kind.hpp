#pragma once

// What SHELL's phase-2 types (Charge-Up, Light Beam, Halo, Bubble, Portal, Reality Tear) do the same
// way, beside `shell_kind.hpp`, which phase 1 wrote for Entity and World owners:
//   - the live gate for every owner they take, the Light owner included (a Light Beam from a spot, a
//     Halo at a lamp), with the window's start and end, which the open/close and charge phases read;
//   - a frame for an oriented shell: an owner's anchor plus a heading from yaw and tilt rows;
//   - the edge test a burst uses: "did this instant fall inside this frame's interval" (TRIGGER's own
//     rule), so a scrub never fires a backlog;
//   - folding a secondary builder's lost part (DF's or EMIT's budget, whose reason is already in the
//     slot) into the instance's status.
// Header-only; each type's file includes it.

#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/effects/kinds/shell_kind.hpp"
#include "world/effects/kinds/stored_rows.hpp"
#include "world/effects/shell_frame.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace avgen::world::kinds {

struct FxLive {
    double start = 0.0;   // the instant the activation (plus delay) opened
    double end = std::numeric_limits<double>::infinity(); // when it closes (infinite: open-ended)
    double local = 0.0;   // seconds since `start`
    float envelope = 0.0f; // the timing envelope (delay, fades, lifetime)
};

// Is this instance's activation window open, and where in it are we. `needEnvelope` false lets a type
// that runs its own phases past the fade (a Charge-Up's release) see the window whatever the envelope.
[[nodiscard]] inline bool fxLive(const EffectInstance& e, const EffectContext& ctx, FxLive& out,
                                 bool needEnvelope = true) {
    if (!e.enabled) {
        return false;
    }
    const bool world = e.owner.kind == EffectTarget::World;
    const bool entity = e.owner.kind == EffectTarget::Entity;
    const bool light = e.owner.kind == EffectTarget::Light;
    if (!(world || entity || light) || (!world && e.owner.name.empty())) {
        return false;
    }
    // An entity is the subject a HeroFocus activation fires for and the node a Proximity trigger
    // measures from; a light is neither, so it follows the cut as the World does.
    const std::string_view owner = entity ? std::string_view(e.owner.name) : std::string_view();
    const auto window = resolveActivationWindow(e.activation, e.timing, ctx, !entity, owner, owner);
    if (!window) {
        return false;
    }
    out.start = window->start + e.timing.delay;
    out.end = window->end;
    out.local = ctx.seconds - out.start;
    if (out.local < 0.0) {
        return false;
    }
    out.envelope = timingEnvelope(e.timing, out.local, window->end - window->start - e.timing.delay);
    return !needEnvelope || out.envelope > 1e-4f;
}

// Where an owner stands. World: `offset` is the position. Entity: the centre of its drawn bounds plus
// `offset` (half-extents, bottom and top). Light: the light's position plus `offset`, and `light` set.
struct FxAnchor {
    glm::vec3 centre{0.0f};
    glm::vec3 halfExtents{0.0f};
    float baseY = 0.0f;
    float topY = 0.0f;
    glm::vec3 forward{0.0f, 0.0f, -1.0f}; // Entity: the node's -Z; Light: its direction
    LightView light;
    bool entity = false;
    bool isLight = false;
};

[[nodiscard]] inline bool fxAnchor(const EffectInstance& e, const EffectContext& ctx, glm::vec3 offset, FxAnchor& out) {
    out = FxAnchor{};
    if (e.owner.kind == EffectTarget::World) {
        out.centre = offset;
        out.baseY = out.topY = offset.y;
        return finite3(offset);
    }
    if (ctx.scene == nullptr) {
        return false;
    }
    if (e.owner.kind == EffectTarget::Light) {
        if (!ctx.scene->lightView(e.owner.name, out.light)) {
            return false;
        }
        out.isLight = true;
        out.centre = out.light.position + offset;
        out.baseY = out.topY = out.centre.y;
        out.forward = out.light.direction;
        return finite3(out.centre);
    }
    NodeView view;
    if (!ctx.scene->nodeView(e.owner.name, view)) {
        return false;
    }
    out.entity = true;
    if (view.hasBounds) {
        out.centre = 0.5f * (view.boundsMin + view.boundsMax) + offset;
        out.halfExtents = 0.5f * (view.boundsMax - view.boundsMin);
        out.baseY = view.boundsMin.y + offset.y;
        out.topY = view.boundsMax.y + offset.y;
    } else {
        out.centre = glm::vec3(view.world[3]) + offset;
        out.baseY = out.topY = out.centre.y;
    }
    const glm::vec3 f = -glm::vec3(view.world[2]);
    if (glm::length(f) > 1e-6f) {
        out.forward = glm::normalize(f);
    }
    return finite3(out.centre);
}

// The unit direction a yaw about +Y and a tilt away from `rest` give. `rest` is one of the world's
// principal directions (-Y for a beam pointing down, +Z for an upright opening's normal); the tilt
// leans it towards the horizontal heading the yaw names.
[[nodiscard]] inline glm::vec3 fxHeading(float yawDegrees, float tiltDegrees, bool fromDown) {
    const float yaw = glm::radians(yawDegrees);
    const float tilt = glm::radians(tiltDegrees);
    const glm::vec3 across(std::sin(yaw), 0.0f, std::cos(yaw));
    if (fromDown) {
        return glm::normalize(glm::vec3(0.0f, -std::cos(tilt), 0.0f) + across * std::sin(tilt));
    }
    return glm::normalize(across * std::cos(tilt) + glm::vec3(0.0f, std::sin(tilt), 0.0f));
}

// Mesh space -> world with the three given axis columns (they must be mutually orthogonal).
[[nodiscard]] inline glm::mat4 fxModel(glm::vec3 centre, glm::vec3 x, glm::vec3 y, glm::vec3 z) {
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0.0f);
    m[1] = glm::vec4(y, 0.0f);
    m[2] = glm::vec4(z, 0.0f);
    m[3] = glm::vec4(centre, 1.0f);
    return m;
}

// An orthonormal pair across `n` (unit), the first as horizontal as it can be: a portal's right and up.
inline void fxPlaneAxes(glm::vec3 n, glm::vec3& right, glm::vec3& up) {
    glm::vec3 r = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), n);
    if (glm::length(r) < 1e-4f) {
        r = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), n);
    }
    right = glm::normalize(r);
    up = glm::normalize(glm::cross(n, right));
}

// Did the instant `t0` fall inside this frame's interval (edgeStart, seconds]? TRIGGER's edge rule:
// false after a jump (the interval is empty), so a seek fires no backlog of bursts.
[[nodiscard]] inline bool fxEdge(double t0, const EffectContext& ctx) {
    return ctx.triggers != nullptr && ctx.triggers->edgeStart() < t0 && t0 <= ctx.seconds;
}

// A deterministic uniform in [0, 1) from an integer pair (lowbias32), for per-event and per-step
// choices that must replay: a tear's shape per opening, a flicker per step.
[[nodiscard]] inline float fxHash(std::uint32_t a, std::uint32_t b) {
    std::uint32_t x = a * 0x9e3779b9U ^ (b + 0x7f4a7c15U);
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x) * (1.0f / 4294967296.0f);
}

// An instant to the millisecond, as an integer key: the same event gives the same key in play and
// after a seek however the double was produced.
[[nodiscard]] inline std::uint32_t fxMillis(double t) {
    return static_cast<std::uint32_t>(std::llround(std::max(t, 0.0) * 1000.0) & 0xffffffffLL);
}

// A secondary builder that ran before SHELL (DF's proxies, EMIT's system) left a reason when part of
// this instance did not fit; a drawn shell is then Partial with that reason, kept as it was.
[[nodiscard]] inline EffectStatus fxDrawn(bool partLost) {
    return partLost ? EffectStatus::Partial : EffectStatus::Drawn;
}

// A DF Warp proxy with every lane zeroed but the shape and field: the caller fills the rest.
[[nodiscard]] inline DistortionProxy fxWarpProxy(DistortionShape shape) {
    DistortionProxy p;
    p.axis0.w = static_cast<float>(shape);
    p.axis1.w = static_cast<float>(DistortionField::Warp);
    return p;
}

} // namespace avgen::world::kinds
