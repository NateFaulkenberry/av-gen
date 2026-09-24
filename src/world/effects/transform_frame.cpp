#include "world/effects/transform_frame.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::world {

// Declared here, defined in each producer's own file (the registry's own rule for `builtinSchemas`).
bool orbitTransform(const EffectInstance&, const EffectContext&, double, TransformContribution&);
bool spiralTransform(const EffectInstance&, const EffectContext&, double, TransformContribution&);
bool floatTransform(const EffectInstance&, const EffectContext&, double, TransformContribution&);
bool shakeTransform(const EffectInstance&, const EffectContext&, double, TransformContribution&);
bool bounceTransform(const EffectInstance&, const EffectContext&, double, TransformContribution&);

namespace {

constexpr TransformProducer kProducers[] = {
    {EffectKind::Orbit, &orbitTransform},   {EffectKind::Spiral, &spiralTransform},
    {EffectKind::Float, &floatTransform},   {EffectKind::Shake, &shakeTransform},
    {EffectKind::Bounce, &bounceTransform},
};

constexpr const char* kBudgetFull =
    "Transform owner budget (64) full this frame; lower-priority motion effects are dropped first.";

bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(glm::quat q) { return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z); }

// The contribution at `envelope`: translation and scale toward identity, rotation slerped toward it.
TransformContribution faded(const TransformContribution& c, float envelope) {
    if (envelope >= 1.0f) {
        return c;
    }
    TransformContribution out = c;
    out.translation = c.translation * envelope;
    out.scale = glm::mix(glm::vec3(1.0f), c.scale, envelope);
    out.rotation = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), c.rotation, envelope);
    return out;
}

std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

} // namespace

std::span<const TransformProducer> transformProducers() { return kProducers; }

const TransformProducer* transformProducer(EffectKind kind) {
    for (const TransformProducer& p : kProducers) {
        if (p.kind == kind) {
            return &p;
        }
    }
    return nullptr;
}

const TransformOffset* TransformFrame::find(std::string_view node) const {
    for (std::size_t i = 0; i < count; ++i) {
        if (offsets[i].node == node) {
            return &offsets[i];
        }
    }
    return nullptr;
}

TransformGate transformGate(const EffectInstance& e, const EffectContext& ctx) {
    TransformGate gate;
    if (!e.enabled) {
        return gate;
    }
    // An entity-owned instance fires for its OWN subject under `HeroFocus` (Ground Pulse's pattern).
    const bool world = e.owner.kind == EffectTarget::World;
    const auto window = resolveActivationWindow(e.activation, e.timing, ctx.seconds, ctx.shots, world,
                                                world ? std::string_view{} : std::string_view(e.owner.name));
    if (!window) {
        return gate;
    }
    const double local = ctx.seconds - window->start - e.timing.delay;
    if (local < 0.0) {
        return gate;
    }
    const double windowLength = window->end - window->start - e.timing.delay;
    // `fmod` rather than a counter, so a scrub lands on the same pass and the same age.
    const double pass = e.timing.repeatSeconds > 0.0 ? std::fmod(local, e.timing.repeatSeconds) : local;
    const double passLength =
        e.timing.repeatSeconds > 0.0 ? std::min(e.timing.repeatSeconds, windowLength) : windowLength;
    const float env = timingEnvelope(e.timing, pass, passLength);
    if (env > 1e-4f) {
        gate.envelope = std::min(env, 1.0f);
        gate.age = pass;
    }
    return gate;
}

void composeTransformOffset(TransformOffset& into, const TransformContribution& c) {
    into.translation += c.translation;
    // Li = T(p) R S T(-p) as a TRS: position p - R (S p).
    const glm::vec3 liPos = c.pivot - c.rotation * (c.scale * c.pivot);
    // into.local * Li. `into.local` is outermost (earlier in the stack). Exact TRS when the outer
    // scale is uniform; otherwise the rotation is applied to the inner scale's axes, which is the
    // composition the flatten uses for its own parent chains.
    into.localPosition = into.localPosition + into.localRotation * (into.localScale * liPos);
    into.localRotation = glm::normalize(into.localRotation * c.rotation);
    into.localScale = into.localScale * c.scale;
}

std::size_t transformRecords(const EffectInstance& instance, const EffectContext& ctx) {
    const TransformProducer* producer = transformProducer(instance.kind);
    if (producer == nullptr || instance.owner.kind != EffectTarget::Entity) {
        return 0;
    }
    const TransformGate gate = transformGate(instance, ctx);
    if (gate.envelope <= 0.0f) {
        return 0;
    }
    TransformContribution c;
    return producer->produce(instance, ctx, gate.age, c) ? 1u : 0u;
}

void buildTransformFrame(std::span<const EffectInstance> effects, const EffectContext& ctx,
                         TransformFrame& out, std::span<const std::uint32_t> order,
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
        const TransformProducer* producer = transformProducer(e.kind);
        if (producer == nullptr) {
            continue; // not an XFORM type: another builder owns its status
        }
        EffectStatus said = e.enabled ? EffectStatus::Dormant : EffectStatus::Disabled;
        const TransformGate gate =
            e.owner.kind == EffectTarget::Entity && !e.owner.name.empty() ? transformGate(e, ctx) : TransformGate{};
        TransformContribution c;
        if (gate.envelope > 0.0f && producer->produce(e, ctx, gate.age, c) && finite(c.translation) &&
            finite(c.rotation) && finite(c.scale) && finite(c.pivot)) {
            TransformOffset* slot = nullptr;
            for (std::size_t i = 0; i < out.count; ++i) {
                if (out.offsets[i].node == e.owner.name) {
                    slot = &out.offsets[i];
                    break;
                }
            }
            if (slot == nullptr && out.count < out.offsets.size()) {
                slot = &out.offsets[out.count++];
                slot->node.assign(e.owner.name); // keeps its capacity: no allocation once warm
                slot->translation = glm::vec3(0.0f);
                slot->localPosition = glm::vec3(0.0f);
                slot->localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                slot->localScale = glm::vec3(1.0f);
            }
            if (slot != nullptr) {
                composeTransformOffset(*slot, faded(c, gate.envelope));
                said = EffectStatus::Drawn;
            } else {
                ++out.dropped;
                said = EffectStatus::Dropped;
                if (at < reasons.size()) {
                    reasons[at].assign(kBudgetFull);
                }
            }
        }
        if (at < status.size()) {
            status[at] = said;
        }
    }
}

float transformSeed(std::string_view id, float authoredSeed) {
    std::uint32_t h = 2166136261u;
    for (const char ch : id) {
        h ^= static_cast<std::uint8_t>(ch);
        h *= 16777619u;
    }
    h = hash32(h ^ static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(authoredSeed * 997.0f))));
    return static_cast<float>(h & 0xffffffu) / 16777216.0f;
}

float motionNoise(double x, std::uint32_t channel, float seed) {
    // Offset per channel and seed, in lattice units, so the axes are unrelated curves. The offset is
    // kept small enough that `x` stays precise on a long timeline once the caller wraps it.
    const double shifted = x + static_cast<double>(seed) * 1013.0 + static_cast<double>(channel) * 57.31;
    const double cell = std::floor(shifted);
    const auto f = static_cast<float>(shifted - cell);
    const auto i = static_cast<std::int64_t>(cell);
    const auto gradient = [&](std::int64_t k) {
        const std::uint32_t h = hash32(static_cast<std::uint32_t>(k) * 0x9E3779B1u ^ hash32(channel + 0x51ED27u));
        return static_cast<float>(h & 0xffffu) / 32767.5f - 1.0f; // [-1, 1]
    };
    const float g0 = gradient(i);
    const float g1 = gradient(i + 1);
    const float v0 = g0 * f;
    const float v1 = g1 * (f - 1.0f);
    const float u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    // A 1-D gradient noise peaks at |0.5|; doubled to use [-1, 1].
    return std::clamp(2.0f * (v0 + (v1 - v0) * u), -1.0f, 1.0f);
}

} // namespace avgen::world
