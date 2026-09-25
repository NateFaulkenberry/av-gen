#pragma once

// What every SHELL type (world/effects/shell_frame.hpp) does the same way: whether an instance is
// on, where it stands, and the transform its record carries. Header-only; each type's file includes
// it (Plasma, Energy Shield and Force Field now; phase 2's Beam, Halo, Bubble, Portal and Tear).

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/effects/kinds/stored_rows.hpp"
#include "world/effects/shell_frame.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace avgen::world::kinds {

struct ShellLive {
    double start = 0.0;   // the instant the activation (plus delay) opened
    float envelope = 0.0f;
};

// Is this instance on, and how strongly: its activation window and its timing envelope. The ONE
// gate a type's `records` hook and its producer both go through, so the conformance probe asks the
// question the builder answers. Entity and World owners only.
[[nodiscard]] inline bool shellLive(const EffectInstance& e, const EffectContext& ctx, ShellLive& out) {
    if (!e.enabled) {
        return false;
    }
    const bool world = e.owner.kind == EffectTarget::World;
    if (!world && (e.owner.kind != EffectTarget::Entity || e.owner.name.empty())) {
        return false;
    }
    const std::string_view owner = world ? std::string_view() : std::string_view(e.owner.name);
    const auto window = resolveActivationWindow(e.activation, e.timing, ctx, world, owner, owner);
    if (!window) {
        return false;
    }
    const double local = ctx.seconds - window->start - e.timing.delay;
    const float envelope = timingEnvelope(e.timing, local, window->end - window->start - e.timing.delay);
    if (!(envelope > 1e-4f)) {
        return false;
    }
    out.start = window->start + e.timing.delay;
    out.envelope = envelope;
    return true;
}

// Why a shell that is not live says nothing: the trigger's own sentence when it is waiting for one.
inline EffectStatus shellDormant(const EffectInstance& e, const EffectContext& ctx, std::string& reason) {
    if (const char* why = effectTriggerDormancy(e, ctx)) {
        reason = why;
    }
    return EffectStatus::Dormant;
}

// Where an owner's shell is anchored. World: `offset` is the world position. Entity: the centre of
// the owner's drawn bounds plus `offset`, with its bounds' half-extents (zero when it has none).
struct ShellAnchor {
    glm::vec3 centre{0.0f};
    glm::vec3 halfExtents{0.0f};
    float baseY = 0.0f; // the bottom of the owner's bounds (World: the anchor's own height)
    bool entity = false;
};
[[nodiscard]] inline bool shellAnchor(const EffectInstance& e, const EffectContext& ctx, glm::vec3 offset,
                                      ShellAnchor& out) {
    out = ShellAnchor{};
    if (e.owner.kind == EffectTarget::World) {
        out.centre = offset;
        out.baseY = offset.y;
        return finite3(offset);
    }
    NodeView view;
    if (ctx.scene == nullptr || !ctx.scene->nodeView(e.owner.name, view)) {
        return false;
    }
    out.entity = true;
    if (view.hasBounds) {
        out.centre = 0.5f * (view.boundsMin + view.boundsMax) + offset;
        out.halfExtents = 0.5f * (view.boundsMax - view.boundsMin);
        out.baseY = view.boundsMin.y + offset.y;
    } else {
        out.centre = glm::vec3(view.world[3]) + offset;
        out.baseY = out.centre.y;
    }
    return finite3(out.centre);
}

// Mesh space -> world: translate, turn about +Y by `yawRadians`, scale each axis. The columns stay
// orthogonal, which is what shell.wgsl's projection inverse needs.
[[nodiscard]] inline glm::mat4 shellModel(glm::vec3 centre, glm::vec3 halfExtents, float yawRadians = 0.0f) {
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(c * halfExtents.x, 0.0f, -s * halfExtents.x, 0.0f);
    m[1] = glm::vec4(0.0f, halfExtents.y, 0.0f, 0.0f);
    m[2] = glm::vec4(s * halfExtents.z, 0.0f, c * halfExtents.z, 0.0f);
    m[3] = glm::vec4(centre, 1.0f);
    return m;
}

} // namespace avgen::world::kinds
