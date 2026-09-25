#include "world/effects/star_field.hpp"

#include "world/effects/effect_registry.hpp"
#include "world/effects/transform_frame.hpp" // transformGate: the one lifecycle gate every builder shares

#include <algorithm>
#include <cmath>

namespace avgen::world {
namespace {

bool isStars(const EffectInstance& e) {
    const EffectSchema* schema = effectSchema(e.kind);
    return schema != nullptr && schema->resolve.bucket == EffectBucket::Starfield;
}

float value(const EffectInstance& e, const char* leaf, float fallback) {
    std::string key = "stars/";
    key += leaf;
    return e.values.getFloat(key, fallback);
}

// The instance's field at full strength, clamped to what the shader can draw sensibly.
StarField resolve(const EffectInstance& e) {
    const StarField d;
    StarField f;
    f.on = true;
    f.density = std::clamp(value(e, "density", d.density), 0.0f, 0.2f);
    f.brightness = std::max(value(e, "brightness", d.brightness), 0.0f);
    f.magnitudeSlope = std::clamp(value(e, "magnitudeSlope", d.magnitudeSlope), 1.0f, 12.0f);
    f.colorSpread = std::clamp(value(e, "colorSpread", d.colorSpread), 0.0f, 1.0f);
    f.twinkle = std::clamp(value(e, "twinkle", d.twinkle), 0.0f, 1.0f);
    f.twinkleRate = std::clamp(value(e, "twinkleRate", d.twinkleRate), 0.0f, 40.0f);
    f.horizonFade = std::clamp(value(e, "horizonFade", d.horizonFade), 0.01f, 1.0f);
    f.band = std::max(value(e, "band", d.band), 0.0f);
    f.bandTilt = value(e, "bandTilt", d.bandTilt);
    f.daylight = std::clamp(value(e, "daylight", d.daylight), 0.0f, 1.0f);
    return f;
}

} // namespace

std::size_t starFieldRecords(const EffectInstance& instance, const EffectContext& ctx) {
    if (!isStars(instance) || instance.owner.kind != EffectTarget::World) {
        return 0;
    }
    return transformGate(instance, ctx).envelope > 0.0f && resolve(instance).brightness > 0.0f ? 1u : 0u;
}

void buildStarField(std::span<const EffectInstance> effects, const EffectContext& ctx, StarField& out,
                    std::span<const std::uint32_t> order, std::span<EffectStatus> status,
                    std::span<std::string> reasons) {
    out = StarField{};
    const EffectInstance* drawn = nullptr;
    const std::size_t n = order.empty() ? effects.size() : order.size();
    for (std::size_t walk = 0; walk < n; ++walk) {
        const std::size_t at = order.empty() ? walk : order[walk];
        if (at >= effects.size() || !isStars(effects[at])) {
            continue;
        }
        const EffectInstance& e = effects[at];
        EffectStatus said = e.enabled ? EffectStatus::Dormant : EffectStatus::Disabled;
        const float envelope =
            e.owner.kind == EffectTarget::World ? transformGate(e, ctx).envelope : 0.0f;
        if (envelope > 0.0f) {
            if (drawn == nullptr) {
                out = resolve(e);
                out.envelope = envelope;
                out.seconds = static_cast<float>(std::fmod(std::max(ctx.seconds, 0.0), kTwinklePeriod));
                drawn = &e;
                said = EffectStatus::Drawn;
            } else {
                said = EffectStatus::Dropped;
                if (at < reasons.size()) {
                    reasons[at] = "The sky has one star field, and '" + drawn->name +
                                  "' is drawing it. Disable one, or merge their settings.";
                }
            }
        }
        if (at < status.size()) {
            status[at] = said;
        }
    }
}

} // namespace avgen::world
