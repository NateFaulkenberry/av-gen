#pragma once

// What the four BOLT types (Lightning, Arc, Electric Field, Discharge) share beyond the generator in
// world/effects/bolt_path: where an endpoint is, the Source/Target storage the Effects panel draws
// pickers for, the per-instance seed, and how a RIBBON result becomes a status. Header-only and
// file-local in use, like stored_rows.hpp.
//
// **Endpoints.** A bolt's ends are `EffectEndpoint`s -- a world point, a node, a hero, the camera, or
// the instance's own owner -- held in the instance's `wave.source` / `wave.target` (the endpoint
// storage every instance carries; the surface waves were its first users) and written as the
// `source` / `target` blocks of the type's JSON. An endpoint that resolves to a BODY (a node or hero
// with drawn bounds, or the owner) is not its centre: the bolt meets the body where the line to the
// other end leaves its bounds, so an arc touches a saucer's hull instead of vanishing into it.
//
// **A lost part says so.** Two builders run before RIBBON and may refuse part of a bolt type: the FXL
// builder (an Electric Field's crackle, when its owner's vein block is taken) and LIGHTMOD's pool (a
// flash, when the 16 lights are spoken for). Either writes its reason into the instance's slot, which
// the engine clears at the top of every frame -- so a producer that finds its reason already written
// has lost a part this frame and reports `Partial`, keeping that reason.

#include "world/effects/bolt_path.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/wave_effect.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace avgen::world::bolt_rows {

using E = EffectInstance;

// A resolved endpoint: a point, and the body it belongs to when it has one.
struct Resolved {
    glm::vec3 point{0.0f};
    bool body = false;
    glm::vec3 lo{0.0f};
    glm::vec3 hi{0.0f};
};

inline bool nodeBody(const EffectContext& ctx, std::string_view name, Resolved& out) {
    if (ctx.scene == nullptr) {
        return false;
    }
    NodeView view;
    if (ctx.scene->nodeView(name, view)) {
        if (view.hasBounds) {
            out.point = 0.5f * (view.boundsMin + view.boundsMax);
            out.body = true;
            out.lo = view.boundsMin;
            out.hi = view.boundsMax;
        } else {
            out.point = glm::vec3(view.world[3]);
        }
        return true;
    }
    return ctx.scene->nodePosition(name, out.point);
}

// Where `ep` is this frame. An endpoint naming something the scene does not have falls back to its
// authored position (a scene swap must not throw a bolt to the origin); an Owner endpoint on a World
// owner IS its authored position. `groundOffset` drops the result, as it does for a wave.
inline bool resolveEndpoint(const E& e, const EffectEndpoint& ep, const EffectContext& ctx, Resolved& out) {
    out = Resolved{};
    out.point = ep.position;
    switch (ep.kind) {
    case SourceKind::World: break;
    case SourceKind::Camera: out.point = ctx.cameraPosition; break;
    case SourceKind::Node:
        if (!nodeBody(ctx, ep.name, out)) {
            out = Resolved{};
            out.point = ep.position;
        }
        break;
    case SourceKind::Hero:
    case SourceKind::FocusHero: {
        // A focus hero is the subject of the shot the cut holds now, when there is one.
        std::string_view name = ep.name;
        if (ep.kind == SourceKind::FocusHero) {
            name = {};
            for (const ShotSpan& s : ctx.shots) {
                if (ctx.seconds >= s.start && ctx.seconds < s.end && s.spotlight) {
                    name = s.subject;
                    break;
                }
            }
            if (name.empty()) {
                return false;
            }
        }
        if (!nodeBody(ctx, name, out)) {
            out = Resolved{};
            out.point = ep.position;
            for (const HeroPoint& h : ctx.heroes) {
                if (h.name == name) {
                    out.point = h.position;
                    break;
                }
            }
        }
        break;
    }
    case SourceKind::Owner:
        if (e.owner.kind == EffectTarget::Entity) {
            if (!nodeBody(ctx, e.owner.name, out)) {
                return false; // the owner is not in the scene this frame (the engine says Orphaned)
            }
        } else if (e.owner.kind == EffectTarget::Camera) {
            out.point = ctx.cameraPosition;
        }
        break;
    }
    out.point.y -= ep.groundOffset;
    out.lo.y -= ep.groundOffset;
    out.hi.y -= ep.groundOffset;
    return std::isfinite(out.point.x) && std::isfinite(out.point.y) && std::isfinite(out.point.z);
}

// Where the line from a body's centre towards `toward` leaves its bounds, a little inside them so
// the bolt visibly touches the surface rather than stopping short of it.
inline glm::vec3 bodyExit(const Resolved& r, const glm::vec3& toward) {
    if (!r.body) {
        return r.point;
    }
    const glm::vec3 d = toward - r.point;
    const glm::vec3 half = 0.5f * (r.hi - r.lo);
    float t = 1.0f;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) > 1e-6f) {
            t = std::min(t, half[i] / std::abs(d[i]));
        }
    }
    return r.point + d * (t * 0.9f);
}

// ---- the endpoint schema hooks (the panel's Source / Target pickers, and the file) ------------------

inline EffectEndpoint getSource(const E& e) { return e.wave.source; }
inline void setSource(E& e, const EffectEndpoint& p) { e.wave.source = p; }
inline bool hasTarget(const E& e) { return e.wave.hasTarget; }
inline EffectEndpoint getTarget(const E& e) { return e.wave.target; }
inline void setTarget(E& e, bool has, const EffectEndpoint& p) {
    e.wave.hasTarget = has;
    e.wave.target = p;
}

inline void writeEndpoints(const E& e, nlohmann::json& block) {
    block["source"] = waveEndpointToJson(e.wave.source);
    if (e.wave.hasTarget) {
        block["target"] = waveEndpointToJson(e.wave.target);
    }
}

inline Result<void> readEndpoints(E& e, const nlohmann::json& block) {
    if (block.contains("source")) {
        auto src = waveEndpointFromJson(block.at("source"));
        if (!src) {
            return fail("source: {}", src.error().message);
        }
        e.wave.source = *src;
    }
    e.wave.hasTarget = false;
    if (block.contains("target")) {
        auto dst = waveEndpointFromJson(block.at("target"));
        if (!dst) {
            return fail("target: {}", dst.error().message);
        }
        e.wave.target = *dst;
        e.wave.hasTarget = true;
    }
    return {};
}

inline Result<void> validateEndpoints(const E& e) {
    if (auto ok = e.wave.source.validate(); !ok) {
        return ok;
    }
    if (e.wave.hasTarget) {
        return e.wave.target.validate();
    }
    return {};
}

// ---- seeds, status ---------------------------------------------------------------------------------

// The instance's seed: its `seed` row mixed with its id, so two instances with the same row still
// strike differently, and an instance keeps its bolts through a save and a reload.
inline std::uint64_t instanceSeed(const E& e, float row) {
    std::uint64_t h = 1469598103934665603ull;
    for (const char c : e.id) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    const auto r = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(std::max(row, 0.0f))));
    return h ^ (r * 0x9E3779B97F4A7C15ull);
}

// The window and envelope every bolt type is gated by (activation + timing), the one question the
// records hook and the producer both ask.
inline bool windowEnvelope(const E& e, const EffectContext& ctx, float& envelope) {
    const bool entity = e.owner.kind == EffectTarget::Entity;
    const auto window = resolveActivationWindow(e.activation, e.timing, ctx, false,
                                                entity ? std::string_view(e.owner.name) : std::string_view(),
                                                entity ? std::string_view(e.owner.name) : std::string_view());
    if (!window) {
        return false;
    }
    const double local = ctx.seconds - window->start - e.timing.delay;
    envelope = timingEnvelope(e.timing, local, window->end - window->start - e.timing.delay);
    return envelope > 1e-4f;
}

// The status a producer returns, given what it drew: `drawn` parts written in full, `reduced` parts
// written at lower detail, `refused` parts not written for want of room.
inline EffectStatus ribbonStatus(std::size_t drawn, std::size_t reduced, std::size_t refused, std::string& reason) {
    // An earlier builder's refusal (FXL, LIGHTMOD) this frame -- see the header.
    const bool earlierLoss = !reason.empty();
    if (drawn + reduced == 0) {
        if (refused > 0) {
            reason = "The ribbon budget (65536 vertices, 256 strips) was full: not drawn.";
            return EffectStatus::Dropped;
        }
        return EffectStatus::Dormant;
    }
    if (refused > 0) {
        reason = "The ribbon budget (65536 vertices, 256 strips) was full: some bolts were not drawn.";
        return EffectStatus::Partial;
    }
    if (reduced > 0) {
        reason = "The ribbon budget (65536 vertices, 256 strips) was full: drawn at reduced detail.";
        return EffectStatus::Partial;
    }
    return earlierLoss ? EffectStatus::Partial : EffectStatus::Drawn;
}

// Writes one placed bolt, lowering its depth until it fits. Counts it into drawn / reduced / refused.
inline void drawBolt(RibbonSink& sink, BoltParams params, std::uint64_t seed, std::uint32_t index,
                     const BoltPlacement& placement, const BoltLook& look, std::vector<RibbonPoint>& scratch,
                     std::size_t& drawn, std::size_t& reduced, std::size_t& refused) {
    const int asked = params.depth;
    for (;;) {
        const BoltPath& path = boltCache().get(params, seed, index);
        switch (appendBoltStrips(sink, path, placement, look, scratch)) {
        case BoltFit::Written: (params.depth == asked ? drawn : reduced) += 1; return;
        case BoltFit::CoreOnly: ++reduced; return;
        case BoltFit::Nothing: return;
        case BoltFit::NoRoom: break;
        }
        if (params.depth <= 2) {
            ++refused;
            return;
        }
        --params.depth;
    }
}

// The camera distance a bolt's level of detail is judged at: to the nearest point of its chord.
inline float chordDistance(const glm::vec3& a, const glm::vec3& b, const glm::vec3& eye) {
    const glm::vec3 ab = b - a;
    const float t = std::clamp(glm::dot(eye - a, ab) / std::max(glm::dot(ab, ab), 1e-9f), 0.0f, 1.0f);
    return glm::length(eye - (a + ab * t));
}

} // namespace avgen::world::bolt_rows
