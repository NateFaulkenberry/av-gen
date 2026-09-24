#include "world/effects/entity_fx.hpp"

#include "world/effects/effect_registry.hpp"

#include "core/noise.hpp"

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
    // Wave 2's exclusive sub-blocks, by the instance holding each (kNone = free).
    std::uint32_t clipHolder = kNone;
    std::uint32_t inflateHolder = kNone;
    std::uint32_t travelHolder = kNone;
    std::uint32_t smearHolder = kNone;
    std::uint32_t bioHolder = kNone;
    std::uint32_t veinsHolder = kNone;
    std::uint32_t hueHolder = kNone;
    std::uint32_t rimLightHolder = kNone;
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

// How many records a contribution takes: its own, and an extension when it has pattern sub-blocks.
bool hasExt(const EntityLaneContribution& c) { return (entityFxSurfaceFlags(c) & kFxExt) != 0; }
std::uint32_t recordsFor(const EntityLaneContribution& c) { return hasExt(c) ? 2u : 1u; }

std::uint32_t addRecord(EntityFxFrame& out, Scratch& s, const EntityLaneContribution& c) {
    const auto index = static_cast<std::uint32_t>(out.records.size());
    out.records.push_back(packEntityFx(c, index));
    s.recordContribution.push_back(c);
    if (hasExt(c)) {
        // The extension sits at index + 1 and is never pointed at by itself; its contribution slot
        // is neutral so a combined record can never be folded from it.
        out.records.push_back(packEntityFxExt(c));
        s.recordContribution.push_back(EntityLaneContribution{});
    }
    return index;
}

// Wave 2's exclusive sub-blocks: moves each one `c` carries into `into`. Returns false, naming the
// conflicting holder in `conflict`, when `c` needs a block `into` already holds -- the clip being the
// exception, whose thresholds take the max when the two clips are the same kind.
struct SubBlockConflict {
    const char* block = nullptr;
    std::uint32_t holder = kNone;
};
bool claimSubBlocks(Group& g, const EntityLaneContribution& c, std::uint32_t at, SubBlockConflict& conflict) {
    const auto busy = [&](bool wants, std::uint32_t holder, const char* name) {
        if (wants && holder != kNone && conflict.block == nullptr) {
            conflict.block = name;
            conflict.holder = holder;
        }
    };
    if (c.hasClip && g.clipHolder != kNone && g.c.clipMode == c.clipMode) {
        // Same kind of clip: the max threshold wins (§7). Not a conflict.
    } else {
        busy(c.hasClip, g.clipHolder, "clip");
    }
    busy(c.hasInflate, g.inflateHolder, "inflate");
    busy(c.hasTravel, g.travelHolder, "travelling bulge");
    busy(c.hasSmear, g.smearHolder, "smear");
    busy(c.hasBio, g.bioHolder, "bioluminescent pattern");
    busy(c.hasVeins, g.veinsHolder, "vein network");
    busy(c.hasHue, g.hueHolder, "hue cycle");
    busy(c.hasRimLight, g.rimLightHolder, "rim light");
    if (conflict.block != nullptr) {
        return false;
    }
    if (c.hasClip) {
        if (g.clipHolder == kNone) {
            g.clipHolder = at;
            g.c.hasClip = true;
            g.c.clipMode = c.clipMode;
            g.c.clipHidden = c.clipHidden;
            g.c.clipEdgeWidth = c.clipEdgeWidth;
            g.c.clipNoiseScale = c.clipNoiseScale;
            g.c.clipEdge = c.clipEdge;
            g.c.clipBreakup = c.clipBreakup;
        } else {
            g.c.clipHidden = std::max(g.c.clipHidden, c.clipHidden);
            g.c.clipEdge += c.clipEdge; // edges sum, like any added emission
        }
    }
    if (c.hasInflate) {
        g.inflateHolder = at;
        g.c.hasInflate = true;
        g.c.inflateAmp = c.inflateAmp;
        g.c.inflateRate = c.inflateRate;
        g.c.inflateAsym = c.inflateAsym;
        g.c.inflatePhase = c.inflatePhase;
        g.c.regionCentre = c.regionCentre;
        g.c.regionWidth = c.regionWidth;
    }
    if (c.hasTravel) {
        g.travelHolder = at;
        g.c.hasTravel = true;
        g.c.travelAmp = c.travelAmp;
        g.c.travelWidth = c.travelWidth;
        g.c.travelSpeed = c.travelSpeed;
        g.c.travelInterval = c.travelInterval;
        g.c.travelDirection = c.travelDirection;
    }
    if (c.hasSmear) {
        g.smearHolder = at;
        g.c.hasSmear = true;
        g.c.smear = c.smear;
        g.c.smearSharpness = c.smearSharpness;
    }
    if (c.hasBio) {
        g.bioHolder = at;
        g.c.hasBio = true;
        g.c.bioPattern = c.bioPattern;
        g.c.bioScale = c.bioScale;
        g.c.bioCoverage = c.bioCoverage;
        g.c.bioVariation = c.bioVariation;
        g.c.bioColor = c.bioColor;
        g.c.bioBreatheRate = c.bioBreatheRate;
        g.c.bioBreatheDepth = c.bioBreatheDepth;
        g.c.bioWaveSpeed = c.bioWaveSpeed;
        g.c.bioWaveInterval = c.bioWaveInterval;
        g.c.bioWaveGain = c.bioWaveGain;
    }
    if (c.hasVeins) {
        g.veinsHolder = at;
        g.c.hasVeins = true;
        g.c.veinsScale = c.veinsScale;
        g.c.veinsWidth = c.veinsWidth;
        g.c.veinsNoise = c.veinsNoise;
        g.c.veinsCoordinate = c.veinsCoordinate;
        g.c.veinsNear = c.veinsNear;
        g.c.veinsFar = c.veinsFar;
        g.c.veinsPulseSpeed = c.veinsPulseSpeed;
        g.c.veinsPulseWidth = c.veinsPulseWidth;
        g.c.veinsPulseInterval = c.veinsPulseInterval;
        g.c.veinsBaseline = c.veinsBaseline;
    }
    if (c.hasHue) {
        g.hueHolder = at;
        g.c.hasHue = true;
        g.c.hueSpeed = c.hueSpeed;
        g.c.hueRange = c.hueRange;
        g.c.hueFrequency = c.hueFrequency;
        g.c.hueChannel = c.hueChannel;
        g.c.hueAxis = c.hueAxis;
        g.c.huePhase = c.huePhase;
    }
    if (c.hasRimLight) {
        g.rimLightHolder = at;
        g.c.hasRimLight = true;
        g.c.rimLight = c.rimLight;
        g.c.rimLightPower = c.rimLightPower;
        g.c.rimLightDir = c.rimLightDir;
        g.c.rimLightThreshold = c.rimLightThreshold;
        g.c.rimLightSoftness = c.rimLightSoftness;
    }
    return true;
}

// Copies every Wave 2 sub-block `from` holds that `into` does not (a combined record for a draw two
// owners cover: the first owner keeps what it holds, as the band does).
void inheritSubBlocks(EntityLaneContribution& into, const EntityLaneContribution& from) {
    const EntityLaneContribution keep = into;
    const auto take = [](bool mine) { return !mine; };
    if (take(keep.hasClip) && from.hasClip) {
        into.hasClip = true;
        into.clipMode = from.clipMode;
        into.clipHidden = from.clipHidden;
        into.clipThreshold = from.clipThreshold;
        into.clipEdgeWidth = from.clipEdgeWidth;
        into.clipNoiseScale = from.clipNoiseScale;
        into.clipEdge = from.clipEdge;
        into.clipBreakup = from.clipBreakup;
    }
    if (take(keep.hasInflate) && from.hasInflate) {
        into.hasInflate = true;
        into.inflateAmp = from.inflateAmp;
        into.inflateRate = from.inflateRate;
        into.inflateAsym = from.inflateAsym;
        into.inflatePhase = from.inflatePhase;
        into.regionCentre = from.regionCentre;
        into.regionWidth = from.regionWidth;
    }
    if (take(keep.hasTravel) && from.hasTravel) {
        into.hasTravel = true;
        into.travelAmp = from.travelAmp;
        into.travelWidth = from.travelWidth;
        into.travelSpeed = from.travelSpeed;
        into.travelInterval = from.travelInterval;
        into.travelDirection = from.travelDirection;
    }
    if (take(keep.hasSmear) && from.hasSmear) {
        into.hasSmear = true;
        into.smear = from.smear;
        into.smearSharpness = from.smearSharpness;
    }
    if (take(keep.hasBio) && from.hasBio) {
        into.hasBio = true;
        into.bioPattern = from.bioPattern;
        into.bioScale = from.bioScale;
        into.bioCoverage = from.bioCoverage;
        into.bioVariation = from.bioVariation;
        into.bioColor = from.bioColor;
        into.bioBreatheRate = from.bioBreatheRate;
        into.bioBreatheDepth = from.bioBreatheDepth;
        into.bioWaveSpeed = from.bioWaveSpeed;
        into.bioWaveInterval = from.bioWaveInterval;
        into.bioWaveGain = from.bioWaveGain;
    }
    if (take(keep.hasVeins) && from.hasVeins) {
        into.hasVeins = true;
        into.veinsScale = from.veinsScale;
        into.veinsWidth = from.veinsWidth;
        into.veinsNoise = from.veinsNoise;
        into.veinsCoordinate = from.veinsCoordinate;
        into.veinsNear = from.veinsNear;
        into.veinsFar = from.veinsFar;
        into.veinsPulseSpeed = from.veinsPulseSpeed;
        into.veinsPulseWidth = from.veinsPulseWidth;
        into.veinsPulseInterval = from.veinsPulseInterval;
        into.veinsBaseline = from.veinsBaseline;
    }
    if (take(keep.hasHue) && from.hasHue) {
        into.hasHue = true;
        into.hueSpeed = from.hueSpeed;
        into.hueRange = from.hueRange;
        into.hueFrequency = from.hueFrequency;
        into.hueChannel = from.hueChannel;
        into.hueAxis = from.hueAxis;
        into.huePhase = from.huePhase;
    }
    if (take(keep.hasRimLight) && from.hasRimLight) {
        into.hasRimLight = true;
        into.rimLight = from.rimLight;
        into.rimLightPower = from.rimLightPower;
        into.rimLightDir = from.rimLightDir;
        into.rimLightThreshold = from.rimLightThreshold;
        into.rimLightSoftness = from.rimLightSoftness;
    }
    if (!into.hasFrame && from.hasFrame) {
        into.hasFrame = true;
        into.frame[0] = from.frame[0];
        into.frame[1] = from.frame[1];
        into.frame[2] = from.frame[2];
        into.shape = from.shape;
        into.radialInv = from.radialInv;
    }
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
    // Wave 2: every amount towards zero -- nothing clipped, nothing moved, nothing lit.
    c.clipHidden *= k;
    c.clipEdge *= k;
    c.inflateAmp *= k;
    c.travelAmp *= k;
    c.smear *= k;
    c.bioColor *= k;
    c.veinsNear *= k;
    c.veinsFar *= k;
    c.hueRange *= k;
    c.rimLight *= k;
}

std::uint32_t entityFxSurfaceFlags(const EntityLaneContribution& c) {
    std::uint32_t flags = 0;
    if (c.hasClip && c.clipMode != EntityFxClipMode::None && c.clipHidden > 0.0f) {
        flags |= kFxClip;
    }
    if (c.hasInflate && c.inflateAmp != 0.0f) {
        flags |= kFxInflate;
    }
    if (c.hasTravel && c.travelAmp != 0.0f) {
        flags |= kFxTravel;
    }
    if (c.hasSmear && glm::dot(c.smear, c.smear) > 1e-10f) {
        flags |= kFxSmear;
    }
    if (c.hasBio && luminance(c.bioColor) > 0.0f) {
        flags |= kFxBio;
    }
    if (c.hasVeins && luminance(c.veinsNear) + luminance(c.veinsFar) > 0.0f) {
        flags |= kFxVeins;
    }
    if (c.hasHue && c.hueRange > 0.0f) {
        flags |= kFxHue;
    }
    if (c.hasRimLight && luminance(c.rimLight) > 0.0f) {
        flags |= kFxRimLight;
    }
    if ((flags & (kFxBio | kFxVeins | kFxHue | kFxRimLight)) != 0) {
        flags |= kFxExt;
    }
    return flags;
}

float entityFxClipThreshold(EntityFxClipMode mode, float hidden, float edgeWidth, float breakup) {
    // The range the keep-value k spans for this mode (see `fxClipKeep` in pbr_shade.wgsl): the noise
    // (or noise swept by a height) lies in [0, 1]; a height or radial front with noise on it
    // overshoots each end by a quarter of the breakup.
    float lo = 0.0f;
    float hi = 1.0f;
    if (mode == EntityFxClipMode::Height || mode == EntityFxClipMode::Radial) {
        const float b = 0.25f * std::clamp(breakup, 0.0f, 1.0f);
        lo = -b - 0.02f; // and the bounds are conservative, so h can sit a hair past 0 or 1
        hi = 1.0f + b + 0.02f;
    }
    const float w = std::max(edgeWidth, 0.0f);
    // hidden 0: every k is at least an edge width above it -- nothing discarded, no edge lit.
    // hidden 1: above every k -- all of it discarded.
    return lo - w + std::clamp(hidden, 0.0f, 1.0f) * (hi - lo + w + 1e-3f);
}

void setEntityFxFrame(EntityLaneContribution& c, const NodeView& view) {
    const glm::mat4 inv = glm::inverse(view.world);
    glm::vec3 lo(-1.0f);
    glm::vec3 hi(1.0f);
    if (view.hasBounds) {
        lo = glm::vec3(std::numeric_limits<float>::max());
        hi = glm::vec3(-std::numeric_limits<float>::max());
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner((i & 1) != 0 ? view.boundsMax.x : view.boundsMin.x,
                                   (i & 2) != 0 ? view.boundsMax.y : view.boundsMin.y,
                                   (i & 4) != 0 ? view.boundsMax.z : view.boundsMin.z);
            const glm::vec3 local = glm::vec3(inv * glm::vec4(corner, 1.0f));
            lo = glm::min(lo, local);
            hi = glm::max(hi, local);
        }
    }
    const glm::vec3 centre = (lo + hi) * 0.5f;
    const float r = std::max(glm::length(hi - lo) * 0.5f, 1e-4f);
    for (int i = 0; i < 3; ++i) {
        c.frame[i] = glm::vec4(inv[0][i], inv[1][i], inv[2][i], inv[3][i] - centre[i]) / r;
    }
    const glm::vec3 origin = -centre / r;
    c.shape = glm::vec4(origin, r / std::max(hi.y - lo.y, 1e-4f));
    float farthest = 1e-4f;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner(((i & 1) != 0 ? hi.x : lo.x), ((i & 2) != 0 ? hi.y : lo.y),
                               ((i & 4) != 0 ? hi.z : lo.z));
        farthest = std::max(farthest, glm::length((corner - centre) / r - origin));
    }
    c.radialInv = 1.0f / farthest;
    c.hasFrame = true;
}

glm::vec3 entityFxOwnerSpace(const EntityLaneContribution& c, const glm::vec3& world) {
    const glm::vec4 p(world, 1.0f);
    return {glm::dot(c.frame[0], p), glm::dot(c.frame[1], p), glm::dot(c.frame[2], p)};
}

glm::vec3 worleyF1F2(const glm::vec3& p, std::uint32_t seed) {
    const glm::vec3 c(std::floor(p.x), std::floor(p.y), std::floor(p.z));
    const glm::vec3 f = p - c;
    float best = 8.0f;
    float second = 8.0f;
    float nearestHash = 0.0f;
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                const auto ix = static_cast<std::int32_t>(c.x) + x;
                const auto iy = static_cast<std::int32_t>(c.y) + y;
                const auto iz = static_cast<std::int32_t>(c.z) + z;
                const glm::vec3 jitter(noise::hash01(ix, iy, iz, seed), noise::hash01(ix, iy, iz, seed + 1u),
                                       noise::hash01(ix, iy, iz, seed + 2u));
                const glm::vec3 d = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) +
                                    jitter - f;
                const float dd = glm::dot(d, d);
                if (dd < best) {
                    second = best;
                    best = dd;
                    nearestHash = noise::hash01(ix, iy, iz, seed + 3u);
                } else if (dd < second) {
                    second = dd;
                }
            }
        }
    }
    return {std::sqrt(best), std::sqrt(second), nearestHash};
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
    const std::uint32_t surface = entityFxSurfaceFlags(c);
    flags |= surface;
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
    // ---- Wave 2 ----
    if (surface != 0 && c.hasFrame) {
        r.lanes[kFxLaneFrame0] = c.frame[0];
        r.lanes[kFxLaneFrame1] = c.frame[1];
        r.lanes[kFxLaneFrame2] = c.frame[2];
        r.lanes[kFxLaneShape] = c.shape;
        r.lanes[kFxLaneTravel].w = c.radialInv;
    }
    if ((surface & kFxClip) != 0) {
        const float edgeWidth = std::max(c.clipEdgeWidth, 1e-3f);
        r.lanes[kFxLaneClip] = glm::vec4(static_cast<float>(c.clipMode),
                                         entityFxClipThreshold(c.clipMode, c.clipHidden, edgeWidth, c.clipBreakup),
                                         edgeWidth, std::max(c.clipNoiseScale, 0.01f));
        r.lanes[kFxLaneClipEdge] = glm::vec4(glm::max(c.clipEdge, glm::vec3(0.0f)), c.clipBreakup);
    }
    if ((surface & kFxInflate) != 0) {
        r.lanes[kFxLaneInflate] =
            glm::vec4(c.inflateAmp, std::max(c.inflateRate, 0.0f), std::clamp(c.inflateAsym, -0.9f, 0.9f), c.inflatePhase);
        r.lanes[kFxLaneRegion].x = c.regionCentre;
        r.lanes[kFxLaneRegion].y = std::max(c.regionWidth, 0.0f);
    }
    if ((surface & kFxTravel) != 0) {
        r.lanes[kFxLaneRegion].z = c.travelAmp;
        r.lanes[kFxLaneRegion].w = std::max(c.travelWidth, 1e-3f);
        r.lanes[kFxLaneTravel].x = c.travelSpeed;
        r.lanes[kFxLaneTravel].y = std::max(c.travelInterval, 0.0f);
        r.lanes[kFxLaneTravel].z = c.travelDirection < 0.0f ? -1.0f : 1.0f;
    }
    if ((surface & kFxSmear) != 0) {
        r.lanes[kFxLaneSmear] = glm::vec4(c.smear, std::max(c.smearSharpness, 0.05f));
    }
    return r;
}

EntityFxRecord packEntityFxExt(const EntityLaneContribution& c) {
    EntityFxRecord r{};
    for (glm::vec4& lane : r.lanes) {
        lane = glm::vec4(0.0f);
    }
    const std::uint32_t surface = entityFxSurfaceFlags(c);
    if ((surface & kFxBio) != 0) {
        r.lanes[kFxExtBio0] = glm::vec4(static_cast<float>(c.bioPattern), std::max(c.bioScale, 0.01f),
                                        std::clamp(c.bioCoverage, 0.0f, 1.0f), std::max(c.bioVariation, 0.0f));
        r.lanes[kFxExtBio1] = glm::vec4(glm::max(c.bioColor, glm::vec3(0.0f)), std::max(c.bioBreatheRate, 0.0f));
        r.lanes[kFxExtBio2] = glm::vec4(std::clamp(c.bioBreatheDepth, 0.0f, 1.0f), c.bioWaveSpeed,
                                        std::max(c.bioWaveInterval, 0.1f), std::max(c.bioWaveGain, 0.0f));
    }
    if ((surface & kFxVeins) != 0) {
        r.lanes[kFxExtVeins0] = glm::vec4(std::max(c.veinsScale, 0.01f), std::max(c.veinsWidth, 1e-3f),
                                          std::max(c.veinsNoise, 0.0f), c.veinsCoordinate > 0.5f ? 1.0f : 0.0f);
        r.lanes[kFxExtVeins1] = glm::vec4(glm::max(c.veinsNear, glm::vec3(0.0f)), c.veinsPulseSpeed);
        r.lanes[kFxExtVeins2] = glm::vec4(glm::max(c.veinsFar, glm::vec3(0.0f)), std::max(c.veinsPulseWidth, 1e-3f));
        r.lanes[kFxExtVeins3] = glm::vec4(std::max(c.veinsPulseInterval, 0.0f), std::clamp(c.veinsBaseline, 0.0f, 1.0f),
                                          0.0f, 0.0f);
    }
    if ((surface & kFxHue) != 0) {
        r.lanes[kFxExtHue0] = glm::vec4(c.hueSpeed, c.hueRange, c.hueFrequency, std::clamp(c.hueChannel, 0.0f, 2.0f));
        const float len = glm::length(c.hueAxis);
        r.lanes[kFxExtHue1] =
            glm::vec4(len > 1e-6f ? c.hueAxis / len : glm::vec3(0.0f, 1.0f, 0.0f), c.huePhase);
    }
    if ((surface & kFxRimLight) != 0) {
        r.lanes[kFxExtRim0] = glm::vec4(glm::max(c.rimLight, glm::vec3(0.0f)), std::max(c.rimLightPower, 0.05f));
        const float len = glm::length(c.rimLightDir);
        r.lanes[kFxExtRim1] = glm::vec4(len > 1e-6f ? c.rimLightDir / len : glm::vec3(0.0f, 0.0f, 1.0f),
                                        c.rimLightThreshold);
        r.lanes[kFxExtRim2] = glm::vec4(std::max(c.rimLightSoftness, 1e-3f), 0.0f, 0.0f, 0.0f);
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
        }
        // Wave 2's exclusive sub-blocks (clip, each displacement mode, each pattern, hue, rim light).
        // Checked before anything is claimed, so a refused instance leaves its owner as it was.
        SubBlockConflict conflict;
        if (!claimSubBlocks(group, c, static_cast<std::uint32_t>(at), conflict)) {
            setStatus(at, EffectStatus::Dropped);
            ++out.dropped;
            sayReason(reasons, at,
                      "'{}' already has a {} from '{}'; an owner carries one. Remove one of them, or "
                      "put this effect on another entity.",
                      e.owner.name, conflict.block, effects[conflict.holder].name);
            continue;
        }
        if (c.hasBand) {
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
    // The owner frame, for every owner whose folded state has a clip, a displacement or a pattern:
    // from its drawn view, so it rides the owner exactly as the draw does this frame.
    for (Group& group : s.groups) {
        if (group.c.needsFrame()) {
            setEntityFxFrame(group.c, group.view);
        }
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
        if (combined == 0) {
            EntityLaneContribution both = s.recordContribution[existing];
            foldEntityLanes(both, group.c);
            if (!both.hasBand && group.c.hasBand) {
                both.hasBand = true;
                both.band = group.c.band;
            }
            inheritSubBlocks(both, group.c);
            if (out.records.size() + recordsFor(both) > kMaxEntityFxRecords) {
                return;
            }
            combined = addRecord(out, s, both);
            s.combos.emplace_back(key, combined);
        }
        if (combined != 0) {
            slot = combined;
        }
    };
    for (std::uint32_t g = 0; g < s.groups.size(); ++g) {
        if (out.records.size() + recordsFor(s.groups[g].c) > kMaxEntityFxRecords) {
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
