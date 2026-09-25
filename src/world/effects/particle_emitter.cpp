#include "world/effects/particle_emitter.hpp"

#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/effect_trigger.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <string_view>

namespace avgen::world {
namespace {

constexpr std::string_view kPrefix = "fx:";

bool isEmitter(const EffectInstance& e) {
    const EffectSchema* schema = effectSchema(e.kind);
    return schema != nullptr && schema->resolve.bucket == EffectBucket::Emitter;
}

// The id a system's name carries, or empty when it is not an effect-owned system.
std::string_view idOf(const scene::ParticleSystem& s) {
    const std::string_view name(s.name);
    return name.starts_with(kPrefix) ? name.substr(kPrefix.size()) : std::string_view();
}

// Activation and timing, exactly as every other type reads them: the window on the transport
// clock, then the delay/fade/lifetime envelope. An entity-owned emitter with `heroFocus` fires for
// its own owner, as a Ground Pulse does.
float envelopeOf(const EffectInstance& e, const EffectContext& ctx) {
    const bool followsFocus = e.owner.kind != EffectTarget::Entity;
    const auto window = resolveActivationWindow(
        e.activation, e.timing, ctx, followsFocus, e.owner.name,
        e.owner.kind == EffectTarget::Entity ? std::string_view(e.owner.name) : std::string_view());
    if (!window) {
        return 0.0f;
    }
    const double local = ctx.seconds - window->start - e.timing.delay;
    const double length = window->end - window->start - e.timing.delay;
    return timingEnvelope(e.timing, local, length);
}

} // namespace

std::string particleSystemName(std::string_view effectId) {
    std::string name(kPrefix);
    name.append(effectId);
    return name;
}

void buildParticleFrame(std::span<const EffectInstance> effects, const EffectContext& ctx,
                        std::vector<scene::ParticleSystem>& particles, std::span<const std::uint32_t> order,
                        std::span<EffectStatus> status, std::span<std::string> reasons) {
    // Systems whose instance is gone (removed, or no longer an emitter) leave. Done first, so the
    // survivors keep their relative order and the renderer's per-index pools stay attached.
    std::erase_if(particles, [&](const scene::ParticleSystem& s) {
        const std::string_view id = idOf(s);
        if (id.empty()) {
            return false; // the composition's own systems are never touched
        }
        const std::size_t at = findEffect(effects, id);
        return at == effects.size() || !isEmitter(effects[at]);
    });

    std::size_t written = 0;
    const std::size_t n = order.empty() ? effects.size() : order.size();
    for (std::size_t walk = 0; walk < n; ++walk) {
        const std::size_t at = order.empty() ? walk : order[walk];
        if (at >= effects.size() || !isEmitter(effects[at])) {
            continue;
        }
        const EffectInstance& e = effects[at];
        EffectStatus said = e.enabled ? EffectStatus::Dormant : EffectStatus::Disabled;
        std::string* why = at < reasons.size() ? &reasons[at] : nullptr;

        // Find this instance's system, or make it once.
        auto it = std::find_if(particles.begin(), particles.end(),
                               [&](const scene::ParticleSystem& s) { return idOf(s) == e.id; });

        // The owner's drawn view. An entity that cannot be seen this frame (not flattened, or not in
        // the scene -- the evaluator reports the latter as Orphaned) stops emitting.
        NodeView view;
        const bool entityOwned = e.owner.kind == EffectTarget::Entity;
        const bool placed = !entityOwned || (ctx.scene != nullptr && ctx.scene->nodeView(e.owner.name, view));

        const bool overBudget = written >= kMaxEffectParticleSystems;
        if (overBudget) {
            said = e.enabled ? EffectStatus::Dropped : EffectStatus::Disabled;
            if (e.enabled && why != nullptr) {
                *why = fmt::format("The effect particle-system budget ({}) is full; emitters above this "
                                   "one in the stack took every system.",
                                   kMaxEffectParticleSystems);
            }
            if (it != particles.end()) {
                it->enabled = false;
            }
        } else {
            if (it == particles.end()) {
                particles.emplace_back();
                it = std::prev(particles.end());
                it->name = particleSystemName(e.id);
            }
            const float envelope = e.enabled && placed ? envelopeOf(e, ctx) : 0.0f;
            describeParticleSystem(e, envelope, *it);
            // Wave 2 (TRIGGER): a burst on the frame a trigger lands. The edge is the frame's own
            // interval (`TriggerClock::edgeStart`), so a scrub never fires a backlog of them.
            if (e.enabled && placed && effectTriggerEdge(e, ctx)) {
                it->burst += triggerBurstOf(e);
            }
            if (entityOwned) {
                // The camera-carried looks are weather over the World; on an entity they are a local
                // cloud around it, so the box does not also follow the camera.
                it->volumeFollow = glm::vec3(0.0f);
                if (placed) {
                    // Ride the owner: born in a region around the centre of what is drawn, pulled
                    // towards it, and launched along the owner's own axes (a tilted craft sheds its
                    // embers tilted).
                    const glm::vec3 centre = view.hasBounds ? (view.boundsMin + view.boundsMax) * 0.5f
                                                            : glm::vec3(view.world[3]);
                    it->position = centre;
                    it->attractorPosition = centre;
                    const glm::vec3 d = glm::mat3(view.world) * it->direction;
                    if (glm::length(d) > 1e-5f) {
                        it->direction = glm::normalize(d);
                    }
                } else {
                    it->spawnRate = 0.0f;
                    it->burst = 0.0f;
                }
            }
            ++written;
            if (e.enabled && placed && envelope > 0.0f) {
                said = EffectStatus::Drawn;
            } else if (e.enabled && placed && why != nullptr) {
                if (const char* waiting = effectTriggerDormancy(e, ctx)) {
                    why->assign(waiting);
                }
            }
        }
        if (at < status.size()) {
            status[at] = said;
        }
    }
}

} // namespace avgen::world
