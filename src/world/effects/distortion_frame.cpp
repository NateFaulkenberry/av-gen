#include "world/effects/distortion_frame.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::world {

// Declared here, defined in the producer's own file: nothing else calls them, so a header of their
// own would be one more file to keep in step (the registry's own rule for `builtinSchemas`).
std::size_t spaceWarpProxies(const EffectInstance& instance, const EffectContext& ctx, float envelope,
                             std::span<DistortionProxy> out);

namespace {

// The largest `maxProxies` any producer declares; the scratch the records hook resolves into.
constexpr std::size_t kMaxProxiesPerInstance = 8;

constexpr DistortionProducer kProducers[] = {
    {EffectKind::SpaceWarp, &spaceWarpProxies, 1},
};

static_assert([] {
    for (const DistortionProducer& p : kProducers) {
        if (p.maxProxies == 0 || p.maxProxies > kMaxProxiesPerInstance) {
            return false;
        }
    }
    return true;
}(), "a producer's maxProxies must fit the records scratch");

// The sentence the panel prints under a Dropped badge. A literal, so rewriting it every frame an
// instance stays over budget allocates nothing once the reason string has capacity.
constexpr const char* kBudgetFull =
    "Distortion proxy budget (64) full this frame; lower-priority distortions are dropped first.";
constexpr const char* kBudgetPartial =
    "Distortion proxy budget (64) filled part-way through this effect's proxies.";

} // namespace

std::span<const DistortionProducer> distortionProducers() { return kProducers; }

const DistortionProducer* distortionProducer(EffectKind kind) {
    for (const DistortionProducer& p : kProducers) {
        if (p.kind == kind) {
            return &p;
        }
    }
    return nullptr;
}

float distortionEnvelope(const EffectInstance& e, const EffectContext& ctx) {
    if (!e.enabled) {
        return 0.0f;
    }
    // An entity-owned warp fires for its OWN subject under `HeroFocus` (the per-hero pattern Ground
    // Pulse established); a World-owned one follows whatever the cut is holding.
    const bool world = e.owner.kind == EffectTarget::World;
    const auto window = resolveActivationWindow(e.activation, e.timing, ctx.seconds, ctx.shots, world,
                                                world ? std::string_view{} : std::string_view(e.owner.name));
    if (!window) {
        return 0.0f;
    }
    const double local = ctx.seconds - window->start - e.timing.delay;
    if (local < 0.0) {
        return 0.0f;
    }
    const double windowLength = window->end - window->start - e.timing.delay;
    // `repeatSeconds` restarts the envelope, which is what a beat-synced warp flash is; `fmod`
    // rather than a counter so a scrub lands the same phase.
    const double pass = e.timing.repeatSeconds > 0.0 ? std::fmod(local, e.timing.repeatSeconds) : local;
    const double passLength =
        e.timing.repeatSeconds > 0.0 ? std::min(e.timing.repeatSeconds, windowLength) : windowLength;
    const float env = timingEnvelope(e.timing, pass, passLength);
    return env > 1e-4f ? env : 0.0f;
}

std::size_t distortionRecords(const EffectInstance& instance, const EffectContext& ctx) {
    const DistortionProducer* producer = distortionProducer(instance.kind);
    if (producer == nullptr || producer->produce == nullptr) {
        return 0;
    }
    const float envelope = distortionEnvelope(instance, ctx);
    if (envelope <= 0.0f) {
        return 0;
    }
    std::array<DistortionProxy, kMaxProxiesPerInstance> scratch{};
    return producer->produce(instance, ctx, envelope, std::span(scratch).first(producer->maxProxies));
}

void buildDistortionFrame(std::span<const EffectInstance> effects, const EffectContext& ctx,
                          DistortionFrame& out, std::span<const std::uint32_t> order,
                          std::span<EffectStatus> status, std::span<std::string> reasons) {
    out.count = 0;
    out.dropped = 0;
    const std::size_t n = order.empty() ? effects.size() : order.size();
    for (std::size_t walk = 0; walk < n; ++walk) {
        const std::size_t at = order.empty() ? walk : order[walk];
        if (at >= effects.size()) {
            continue;
        }
        const EffectInstance& e = effects[at];
        const DistortionProducer* producer = distortionProducer(e.kind);
        if (producer == nullptr || producer->produce == nullptr) {
            continue; // not a DF type: another builder owns its status
        }
        EffectStatus said = e.enabled ? EffectStatus::Dormant : EffectStatus::Disabled;
        const float envelope = distortionEnvelope(e, ctx);
        if (envelope > 0.0f) {
            // Resolve into scratch first, so an instance that does not fit is counted as what it
            // would have drawn rather than as nothing -- the difference between Dropped and Dormant.
            std::array<DistortionProxy, kMaxProxiesPerInstance> scratch{};
            const std::size_t made =
                producer->produce(e, ctx, envelope, std::span(scratch).first(producer->maxProxies));
            if (made > 0) {
                const std::size_t room = kMaxDistortionProxies - out.count;
                const std::size_t taken = std::min(made, room);
                for (std::size_t i = 0; i < taken; ++i) {
                    out.proxies[out.count++] = scratch[i];
                }
                out.dropped += static_cast<std::uint32_t>(made - taken);
                if (taken == made) {
                    said = EffectStatus::Drawn;
                } else {
                    said = taken == 0 ? EffectStatus::Dropped : EffectStatus::Partial;
                    if (at < reasons.size()) {
                        reasons[at].assign(taken == 0 ? kBudgetFull : kBudgetPartial);
                    }
                }
            }
        }
        if (at < status.size()) {
            status[at] = said;
        }
    }
    // Unused slots are zeroed so the block the renderer uploads is a pure function of the live
    // proxies -- a stale record past `count` is never read, but a byte-compare of two frames should
    // not depend on what an earlier frame left there.
    for (std::size_t i = out.count; i < kMaxDistortionProxies; ++i) {
        out.proxies[i] = DistortionProxy{};
    }
}

void distortionBasis(glm::vec3 forward, glm::vec3& a0, glm::vec3& a1, glm::vec3& a2) {
    const float len = glm::length(forward);
    a0 = len > 1e-6f ? forward / len : glm::vec3(1.0f, 0.0f, 0.0f);
    // The helper axis is the world axis LEAST aligned with forward, chosen by a fixed rule, so the
    // basis is a continuous function of forward except where two components tie -- and at a tie
    // the stretch is along a0 while a1/a2 carry equal lengths, so the ellipsoid is unchanged.
    const glm::vec3 ab = glm::abs(a0);
    const glm::vec3 helper = (ab.x <= ab.y && ab.x <= ab.z)   ? glm::vec3(1.0f, 0.0f, 0.0f)
                             : (ab.y <= ab.z)                   ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                                : glm::vec3(0.0f, 0.0f, 1.0f);
    a1 = glm::normalize(glm::cross(a0, helper));
    a2 = glm::cross(a0, a1);
}

} // namespace avgen::world
