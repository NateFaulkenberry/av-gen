#include "world/effects/entity_fx.hpp"

#include "world/effects/effect_registry.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace avgen::world {

namespace {

constexpr float kTau = 6.28318530718f;
constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

float luminance(const glm::vec3& c) { return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)); }

// Formats a reason into a stack buffer and assigns it, so a reason repeated every frame reuses the
// string's storage instead of allocating (the engine counts allocations per frame).
template <typename... Args>
void sayReason(std::span<std::string> reasons, std::size_t at, fmt::format_string<Args...> format,
               Args&&... args) {
    if (at >= reasons.size()) {
        return;
    }
    std::array<char, 256> buffer{};
    const auto result = fmt::format_to_n(buffer.data(), buffer.size(), format, std::forward<Args>(args)...);
    reasons[at].assign(buffer.data(), std::min<std::size_t>(result.size, buffer.size()));
}

enum class Liveness : std::uint8_t { Disabled, Dormant, EmptyOwner, Live };

// Everything about one instance that does not depend on its neighbours: is it on, is its window
// open, what does its owner look like, what does it add. Shared by the builder and by
// `entityLaneRecords` (the conformance probe's hook), so the two cannot disagree about whether an
// instance resolves.
Liveness liveContribution(const EffectInstance& e, const EffectContext& ctx, NodeView& view,
                          EntityLaneContribution& c) {
    if (!e.enabled) {
        return Liveness::Disabled;
    }
    const EffectSchema* schema = effectSchema(e.kind);
    if (schema == nullptr || schema->resolve.bucket != EffectBucket::EntityLanes ||
        schema->resolve.lanes == nullptr || e.owner.kind != EffectTarget::Entity || ctx.scene == nullptr) {
        return Liveness::Dormant;
    }
    // A `HeroFocus` lane effect fires while the cut holds ITS OWNER -- the same rule an entity-owned
    // Ground Pulse follows -- never "whoever is in focus": a glow on the saucer does not light up
    // because the camera found a mushroom.
    const auto window =
        resolveActivationWindow(e.activation, e.timing, ctx.seconds, ctx.shots, false, e.owner.name);
    if (!window) {
        return Liveness::Dormant;
    }
    const double local = ctx.seconds - window->start - e.timing.delay;
    const double windowLength = window->end - window->start - e.timing.delay;
    const float envelope = timingEnvelope(e.timing, local, windowLength);
    if (!(envelope > 1e-4f)) {
        return Liveness::Dormant;
    }
    if (!ctx.scene->nodeView(e.owner.name, view)) {
        // No such node, or nothing flattened yet. The engine reports the first as Orphaned.
        return Liveness::Dormant;
    }
    // Neither scene entities nor procedural objects: nothing is drawn for a lane to change. (A
    // procedural node -- Glowmere's `visitor` saucer -- has procedurals and no entities.)
    if (view.entityCount == 0 && view.proceduralCount == 0) {
        return Liveness::EmptyOwner;
    }
    c = EntityLaneContribution{};
    if (!schema->resolve.lanes(e, ctx, view, local, c)) {
        return Liveness::Dormant;
    }
    applyEntityEnvelope(c, envelope);
    return Liveness::Live;
}

// Per-owner state while the frame is folded.
struct Group {
    std::string_view owner;
    NodeView view;
    EntityLaneContribution c;
    std::uint32_t bandHolder = kNone; // the instance holding the exclusive band sub-block
    std::uint32_t record = 0;
};

struct Pending {
    std::uint32_t at = 0;       // index into the effect list
    std::uint32_t walk = 0;     // position in the evaluation order
    std::uint32_t group = 0;
    EntityLaneContribution c;
};

// Reused per call on the evaluating thread: the builder allocates only while a scene grows past
// what it has seen, never on a steady frame.
struct Scratch {
    std::vector<Pending> pending;
    std::vector<Group> groups;
    std::vector<EntityLaneContribution> recordContribution;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> combos;
    std::vector<EffectLightRequest> requests;
};
Scratch& scratch() {
    thread_local Scratch s;
    return s;
}

std::uint32_t addRecord(EntityFxFrame& out, Scratch& s, const EntityLaneContribution& c) {
    const auto index = static_cast<std::uint32_t>(out.records.size());
    out.records.push_back(packEntityFx(c, index));
    s.recordContribution.push_back(c);
    return index;
}

} // namespace

float pulseWave(FxWaveform w, float x) {
    x = x - std::floor(x);
    switch (w) {
    case FxWaveform::Sine: return 0.5f - 0.5f * std::cos(kTau * x);
    case FxWaveform::Triangle: return 1.0f - std::abs(2.0f * x - 1.0f);
    case FxWaveform::Square: {
        // Soft-edged, on for the middle half of the cycle: a hard edge strobes at the frame rate
        // rather than at the pulse's, and in a band it is a visible stair across the surface.
        const auto smooth = [](float a, float b, float v) {
            const float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        return smooth(0.22f, 0.28f, x) * (1.0f - smooth(0.72f, 0.78f, x));
    }
    case FxWaveform::Saw: {
        // A ramp that falls away softly at the end of the cycle rather than in one sample.
        const float t = std::clamp((x - 0.94f) / 0.06f, 0.0f, 1.0f);
        return x * (1.0f - t * t * (3.0f - 2.0f * t));
    }
    case FxWaveform::Heartbeat: {
        // Lub-dub: a strong beat and a weaker one a quarter-cycle behind it, then rest.
        const float a = (x - 0.15f) / 0.045f;
        const float b = (x - 0.38f) / 0.06f;
        return std::min(1.0f, std::exp(-a * a) + 0.6f * std::exp(-b * b));
    }
    }
    return 0.0f;
}

void foldEntityLanes(EntityLaneContribution& into, const EntityLaneContribution& c) {
    into.gain *= c.gain;             // gains multiply
    into.ownTint *= c.ownTint;       // tints on the owner's own light multiply
    into.add += c.add;               // added emission sums
    // Rims sum; the exponent is the strength-weighted mean of the two, so a strong wide rim and a
    // faint tight one read as mostly wide rather than as whichever was listed last.
    const float wa = luminance(into.rim);
    const float wb = luminance(c.rim);
    if (wa + wb > 1e-8f) {
        into.rimPower = (wa * into.rimPower + wb * c.rimPower) / (wa + wb);
    } else {
        into.rimPower = std::max(into.rimPower, c.rimPower);
    }
    into.rim += c.rim;
    into.bloomShare = std::max(into.bloomShare, c.bloomShare); // the maximum wins
}

void applyEntityEnvelope(EntityLaneContribution& c, float envelope) {
    const float k = std::clamp(envelope, 0.0f, 1.0f);
    c.gain = 1.0f + (c.gain - 1.0f) * k;
    c.ownTint = glm::vec3(1.0f) + (c.ownTint - glm::vec3(1.0f)) * k;
    c.add *= k;
    c.rim *= k;
    c.bloomShare *= k;
    c.band.depth *= k;
    c.spillIntensity *= k;
}

EntityFxRecord packEntityFx(const EntityLaneContribution& c, std::uint32_t index) {
    EntityFxRecord r{};
    for (glm::vec4& lane : r.lanes) {
        lane = glm::vec4(0.0f);
    }
    std::uint32_t flags = kFxOn;
    if (luminance(c.add) > 0.0f || luminance(c.rim) > 0.0f) {
        flags |= kFxRecord;
    }
    if (c.hasBand && c.band.depth > 0.0f) {
        flags |= kFxBand;
    }
    const float share = std::clamp(c.bloomShare, 0.0f, 1.0f);
    if (share > 0.0f) {
        flags |= kFxBloomShare;
    }
    r.lanes[kFxLaneA] = glm::vec4(std::max(c.gain, 0.0f), share, static_cast<float>(flags), static_cast<float>(index));
    r.lanes[kFxLaneB] = glm::vec4(glm::max(c.ownTint, glm::vec3(0.0f)), 0.0f);
    r.lanes[kFxLaneAdd] = glm::vec4(glm::max(c.add, glm::vec3(0.0f)), 0.0f);
    r.lanes[kFxLaneRim] = glm::vec4(glm::max(c.rim, glm::vec3(0.0f)), std::max(c.rimPower, 0.05f));
    if ((flags & kFxBand) != 0) {
        const float extent = std::max(c.band.u1 - c.band.u0, 1e-4f);
        r.lanes[kFxLaneBandAxis] = glm::vec4(c.band.axis / extent, -c.band.u0 / extent);
        r.lanes[kFxLaneBand] = glm::vec4(c.band.centre, std::max(c.band.halfWidth, 1e-3f),
                                         static_cast<float>(c.band.waveform), std::clamp(c.band.depth, 0.0f, 1.0f));
    }
    return r;
}

void EntityFxFrame::clear() {
    records.clear();
    entityRecord.clear();
    proceduralRecord.clear();
    lights.count = 0;
    lights.dropped = 0;
    dropped = 0;
}

std::size_t entityLaneRecords(const EffectInstance& e, const EffectContext& ctx) {
    NodeView view;
    EntityLaneContribution c;
    return liveContribution(e, ctx, view, c) == Liveness::Live ? 1u : 0u;
}

void buildEntityFxFrame(std::span<const EffectInstance> effects, const EffectContext& ctx, EntityFxFrame& out,
                        std::span<const std::uint32_t> order, std::span<EffectStatus> status,
                        std::span<std::string> reasons) {
    out.clear();
    Scratch& s = scratch();
    s.pending.clear();
    s.groups.clear();
    s.recordContribution.clear();
    s.combos.clear();
    s.requests.clear();

    const auto setStatus = [&](std::size_t at, EffectStatus st) {
        if (at < status.size()) {
            status[at] = st;
        }
    };

    // ---- 1. every EntityLanes instance, in evaluation order: live or not, and what it adds ----
    const std::size_t n = order.empty() ? effects.size() : order.size();
    for (std::size_t walk = 0; walk < n; ++walk) {
        const std::size_t at = order.empty() ? walk : order[walk];
        if (at >= effects.size()) {
            continue;
        }
        const EffectInstance& e = effects[at];
        const EffectSchema* schema = effectSchema(e.kind);
        if (schema == nullptr || schema->resolve.bucket != EffectBucket::EntityLanes) {
            continue;
        }
        NodeView view;
        EntityLaneContribution c;
        switch (liveContribution(e, ctx, view, c)) {
        case Liveness::Disabled: setStatus(at, EffectStatus::Disabled); continue;
        case Liveness::Dormant: setStatus(at, EffectStatus::Dormant); continue;
        case Liveness::EmptyOwner:
            setStatus(at, EffectStatus::Dropped);
            ++out.dropped;
            sayReason(reasons, at, "'{}' draws no meshes, so there is no surface for this effect to change.",
                      e.owner.name);
            continue;
        case Liveness::Live: break;
        }

        // ---- 2. fold into the owner's group (§7) ----
        std::uint32_t g = 0;
        while (g < s.groups.size() && s.groups[g].owner != e.owner.name) {
            ++g;
        }
        if (g == s.groups.size()) {
            Group group;
            group.owner = e.owner.name;
            group.view = view;
            s.groups.push_back(group);
        }
        Group& group = s.groups[g];
        if (c.hasBand) {
            if (group.bandHolder != kNone) {
                // The band is an exclusive sub-block: there is one axis, one centre, one width per
                // owner. Refused by name rather than blended into a band nobody authored.
                setStatus(at, EffectStatus::Dropped);
                ++out.dropped;
                sayReason(reasons, at,
                          "'{}' already has a travelling band from '{}'; an owner carries one band. "
                          "Use the whole-object mode, or put the band on one effect.",
                          e.owner.name, effects[group.bandHolder].name);
                continue;
            }
            group.bandHolder = static_cast<std::uint32_t>(at);
            group.c.hasBand = true;
            group.c.band = c.band;
        }
        foldEntityLanes(group.c, c);
        Pending p;
        p.at = static_cast<std::uint32_t>(at);
        p.walk = static_cast<std::uint32_t>(walk);
        p.group = g;
        p.c = c;
        s.pending.push_back(p);
        setStatus(at, EffectStatus::Drawn);
    }
    if (s.groups.empty()) {
        return; // no records: the renderer writes zero lanes, and the frame is the frame without FXL
    }

    // ---- 3. one record per owner, and each entity in its drawn range pointed at it ----
    // Record 0: all zero (flags 0 -- "no effect"), never referenced. Not a packed neutral
    // contribution, whose flags would say "on".
    EntityFxRecord zero;
    for (glm::vec4& lane : zero.lanes) {
        lane = glm::vec4(0.0f);
    }
    out.records.push_back(zero);
    s.recordContribution.push_back(EntityLaneContribution{});
    std::uint32_t highestEntity = 0;
    std::uint32_t highestProcedural = 0;
    for (const Group& group : s.groups) {
        highestEntity = std::max(highestEntity, group.view.firstEntity + group.view.entityCount);
        highestProcedural = std::max(highestProcedural, group.view.firstProcedural + group.view.proceduralCount);
    }
    out.entityRecord.assign(highestEntity, 0u);
    out.proceduralRecord.assign(highestProcedural, 0u);
    // Points one draw (an entity or a procedural object) at group `g`'s record. Two owners covering
    // one draw (a node nested in another that also carries lanes) give it both, folded by the same
    // rules, through one combined record per distinct pair; the band stays with whichever owner
    // claimed it first. Entities and procedurals are two address spaces for the SAME records.
    const auto point = [&](std::uint32_t& slot, std::uint32_t g) {
        const Group& group = s.groups[g];
        const std::uint32_t existing = slot;
        if (existing == 0) {
            slot = group.record;
            return;
        }
        const std::uint64_t key = (static_cast<std::uint64_t>(existing) << 32) | g;
        std::uint32_t combined = 0;
        for (const auto& [k, r] : s.combos) {
            if (k == key) {
                combined = r;
                break;
            }
        }
        if (combined == 0 && out.records.size() < kMaxEntityFxRecords) {
            EntityLaneContribution both = s.recordContribution[existing];
            foldEntityLanes(both, group.c);
            if (!both.hasBand && group.c.hasBand) {
                both.hasBand = true;
                both.band = group.c.band;
            }
            combined = addRecord(out, s, both);
            s.combos.emplace_back(key, combined);
        }
        if (combined != 0) {
            slot = combined;
        }
    };
    for (std::uint32_t g = 0; g < s.groups.size(); ++g) {
        if (out.records.size() >= kMaxEntityFxRecords) {
            s.groups[g].record = 0;
            continue;
        }
        s.groups[g].record = addRecord(out, s, s.groups[g].c);
        const NodeView& v = s.groups[g].view;
        for (std::uint32_t e = v.firstEntity; e < v.firstEntity + v.entityCount; ++e) {
            point(out.entityRecord[e], g);
        }
        for (std::uint32_t p = v.firstProcedural; p < v.firstProcedural + v.proceduralCount; ++p) {
            point(out.proceduralRecord[p], g);
        }
    }

    // ---- 4. the budget's losers, and the spill lights (LIGHTMOD) ----
    for (const Pending& p : s.pending) {
        const Group& group = s.groups[p.group];
        const EffectInstance& e = effects[p.at];
        if (group.record == 0) {
            setStatus(p.at, EffectStatus::Dropped);
            ++out.dropped;
            sayReason(reasons, p.at, "The entity lane budget ({} owners, {} MiB) is full.", kMaxEntityFxRecords - 1,
                      kEntityFxBudgetMiB);
            continue;
        }
        if (!p.c.spill || !(p.c.spillIntensity > 0.0f)) {
            continue;
        }
        const NodeView& v = group.view;
        const glm::vec3 centre = v.hasBounds ? (v.boundsMin + v.boundsMax) * 0.5f : glm::vec3(v.world[3]);
        const float radius =
            v.hasBounds ? std::max(glm::length(v.boundsMax - v.boundsMin) * 0.5f, 0.25f) : 1.0f;
        EffectLightRequest request;
        request.instance = p.at;
        const EffectSchema* schema = effectSchema(e.kind);
        request.priority = schema != nullptr ? schema->priority : 0;
        request.order = p.walk;
        request.light.position = centre;
        // The owner's FOLDED gain: a Pulse on the same owner pulses the light it throws too.
        request.light.intensity = p.c.spillIntensity * std::max(group.c.gain, 0.0f);
        request.light.color = p.c.spillColor;
        request.light.range = std::max(p.c.spillRange, 0.1f) * radius;
        request.light.volumetric = std::clamp(p.c.spillFog, 0.0f, 1.0f);
        s.requests.push_back(request);
    }
    resolveEffectLights(s.requests, ctx.cameraPosition, out.lights, status, reasons);
}

} // namespace avgen::world
