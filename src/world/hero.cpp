#include "world/hero.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

using nlohmann::json;

namespace avgen::world {
namespace {

struct BehaviourName {
    HeroBehaviour value;
    std::string_view name;
};

constexpr BehaviourName kBehaviours[] = {
    {HeroBehaviour::EmissionPulse, "emissionPulse"},
    {HeroBehaviour::ColorShift, "colorShift"},
    {HeroBehaviour::Hover, "hover"},
    {HeroBehaviour::ScalePulse, "scalePulse"},
    {HeroBehaviour::Rotation, "rotation"},
    {HeroBehaviour::LightBurst, "lightBurst"},
    {HeroBehaviour::ParticleEmission, "particleEmission"},
    {HeroBehaviour::Reveal, "reveal"},
};

// The signal paths the modulation system already publishes. A reaction naming anything else is a
// reaction that silently never fires, which is indistinguishable from a hero that does not react --
// so it is rejected at validation rather than discovered by staring at a scene.
constexpr std::string_view kKnownSources[] = {
    "audio.rms",      "audio.bass",         "audio.mid",     "audio.treble",
    "audio.spectralCentroid", "audio.spectralFlux", "audio.onset",   "audio.tempo",
    "beat.phase",     "beat.pulse",         "beat.bar",      "beat.phrase",
    "beat.section",
};

bool knownSource(std::string_view s) {
    return std::find(std::begin(kKnownSources), std::end(kKnownSources), s) != std::end(kKnownSources);
}

HeroReactionProfile makeProfile(std::string name, std::vector<HeroReaction> reactions) {
    HeroReactionProfile p;
    p.name = std::move(name);
    p.reactions = std::move(reactions);
    return p;
}
} // namespace

const char* heroBehaviourName(HeroBehaviour b) {
    for (const auto& entry : kBehaviours) {
        if (entry.value == b) {
            return entry.name.data();
        }
    }
    return "emissionPulse";
}

Result<void> HeroReaction::validate() const {
    if (!knownSource(source)) {
        return fail("hero reaction: '{}' is not a signal the modulator publishes, so this reaction "
                    "would never fire", source);
    }
    if (!std::isfinite(amount)) {
        return fail("hero reaction from '{}': amount must be finite", source);
    }
    if (attackMs < 0.0f || decayMs < 0.0f) {
        return fail("hero reaction from '{}': attack and decay are durations", source);
    }
    return {};
}

Result<void> HeroReactionProfile::validate() const {
    if (name.empty()) {
        return fail("a hero reaction profile needs a name");
    }
    if (reactions.size() > 4) {
        // Not a technical limit. A hero doing five things at once is a hero doing none of them
        // legibly, and the spec's own guidance is that not every hero should do everything.
        return fail("hero reaction profile '{}': {} reactions is more than reads as one behaviour",
                    name, reactions.size());
    }
    for (const auto& r : reactions) {
        if (auto ok = r.validate(); !ok) {
            return fail("hero reaction profile '{}': {}", name, ok.error().message);
        }
    }
    return {};
}

Result<void> HeroPoint::validate() const {
    if (name.empty()) {
        return fail("a hero needs a name");
    }
    if (!(scale > 0.0f) || !std::isfinite(scale)) {
        return fail("hero '{}': scale must be positive", name);
    }
    if (importance < 0.0f || importance > 1.0f) {
        return fail("hero '{}': importance is 0..1", name);
    }
    if (focalWeight < 0.0f || focalWeight > 1.0f) {
        return fail("hero '{}': focalWeight is 0..1", name);
    }
    if (!(preferredCameraDistance > 0.0f)) {
        return fail("hero '{}': a camera has to stand somewhere", name);
    }
    if (activationRadius < preferredCameraDistance) {
        // A hero that activates closer than the camera is meant to stand never activates on the
        // shot that was designed for it.
        return fail("hero '{}': activationRadius ({:.1f} m) is inside preferredCameraDistance "
                    "({:.1f} m), so it would never be active while being looked at",
                    name, activationRadius, preferredCameraDistance);
    }
    if (!reactionProfile.empty() && findHeroReactionProfile(reactionProfile) == nullptr) {
        return fail("hero '{}': unknown reaction profile '{}'", name, reactionProfile);
    }
    return {};
}

json HeroPoint::toJson() const {
    json j;
    j["name"] = name;
    if (!assetId.empty()) {
        j["asset"] = assetId;
    }
    j["position"] = {position.x, position.y, position.z};
    j["yaw"] = yaw;
    j["scale"] = scale;
    j["radius"] = radius;
    j["height"] = height;
    j["importance"] = importance;
    j["focalWeight"] = focalWeight;
    j["preferredCameraDistance"] = preferredCameraDistance;
    j["preferredCameraElevation"] = preferredCameraElevationDegrees;
    j["activationRadius"] = activationRadius;
    j["colorAccent"] = {colorAccent.r, colorAccent.g, colorAccent.b};
    if (!reactionProfile.empty()) {
        j["reactionProfile"] = reactionProfile;
    }
    return j;
}

Result<HeroPoint> HeroPoint::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("a hero must be a JSON object");
    }
    HeroPoint h;
    if (!j.contains("name") || !j.at("name").is_string()) {
        return fail("a hero needs a string 'name'");
    }
    h.name = j.at("name").get<std::string>();
    const auto str = [&](const char* key, std::string& out) {
        if (j.contains(key) && j.at(key).is_string()) {
            out = j.at(key).get<std::string>();
        }
    };
    const auto num = [&](const char* key, float& out) {
        if (j.contains(key) && j.at(key).is_number()) {
            out = j.at(key).get<float>();
        }
    };
    const auto vec = [&](const char* key, glm::vec3& out) -> Result<void> {
        if (!j.contains(key)) {
            return {};
        }
        if (!j.at(key).is_array() || j.at(key).size() != 3) {
            return fail("hero '{}': '{}' must be three numbers", h.name, key);
        }
        out = glm::vec3(j.at(key)[0].get<float>(), j.at(key)[1].get<float>(), j.at(key)[2].get<float>());
        return {};
    };
    str("asset", h.assetId);
    if (j.contains("assembly")) {
        // Read and dropped rather than rejected, so a scene written before ADR-107 still opens. The
        // hero is now the object of its own name; an assembly that named a group of nodes named
        // nothing the rest of the engine could act on.
        log::warn("hero '{}': 'assembly' is no longer used -- a hero is the object it is named "
                  "after; this one now stands on whatever node is called '{}'",
                  h.name, h.name);
    }
    str("reactionProfile", h.reactionProfile);
    if (auto ok = vec("position", h.position); !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = vec("colorAccent", h.colorAccent); !ok) {
        return std::unexpected(ok.error());
    }
    num("yaw", h.yaw);
    num("scale", h.scale);
    num("radius", h.radius);
    num("height", h.height);
    num("importance", h.importance);
    num("focalWeight", h.focalWeight);
    num("preferredCameraDistance", h.preferredCameraDistance);
    num("preferredCameraElevation", h.preferredCameraElevationDegrees);
    num("activationRadius", h.activationRadius);
    if (auto ok = h.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return h;
}

HeroBehaviourTarget heroBehaviourTarget(HeroBehaviour behaviour) {
    switch (behaviour) {
    case HeroBehaviour::EmissionPulse:
        return {"emissiveBoost", -1, true, ""};
    case HeroBehaviour::Hover:
        return {"position", 1, true, ""};   // Y only: a hover is vertical
    case HeroBehaviour::ScalePulse:
        return {"scale", -1, true, ""};
    case HeroBehaviour::Rotation:
        return {"rotation", 1, true, ""};   // yaw
    case HeroBehaviour::ColorShift:
        return {"", -1, false, "a material program with a hue input"};
    case HeroBehaviour::LightBurst:
        return {"", -1, false, "a practical light of its own"};
    case HeroBehaviour::ParticleEmission:
        return {"", -1, false, "a particle system"};
    case HeroBehaviour::Reveal:
        return {"", -1, false, "a one-shot trigger rather than a continuous signal"};
    }
    return {"", -1, false, "an unknown behaviour"};
}

float heroClearanceRadius(const HeroPoint& hero) {
    // Proportional to the hero's own footprint, not to how far the camera stands off. Tying it to
    // camera distance seems reasonable and is not: stand-off is about three times a hero's height,
    // so a tall hero cleared a radius comparable to its own stand-off, and five heroes in a
    // four-hundred-metre world emptied most of it. The render showed a bare hillside with five
    // objects on it and no vegetation at all.
    //
    // What this is for is much smaller: enough ground around a hero that it is not standing in a
    // thicket when the camera arrives.
    const float base = std::max(hero.radius, hero.height * 0.25f);
    return std::max(base * (1.2f + 1.6f * std::clamp(hero.focalWeight, 0.0f, 1.0f)), 2.0f);
}

const std::vector<HeroReactionProfile>& heroReactionProfiles() {
    static const std::vector<HeroReactionProfile> profiles = {
        // Almost static. Its runes light on the beat and the whole thing comes up on a build; it
        // does not hover, rotate or pulse, and that stillness is what makes it read as a monument.
        makeProfile("monument",
                    {HeroReaction{HeroBehaviour::EmissionPulse, "beat.pulse", 0.6f, 60.0f, 420.0f},
                     HeroReaction{HeroBehaviour::LightBurst, "audio.rms", 1.4f, 900.0f, 2400.0f}}),
        // Alive. Slow opening driven by low-frequency energy, an inner glow on the beat. No
        // rotation: a thing that grew there does not spin.
        makeProfile("organism",
                    {HeroReaction{HeroBehaviour::ScalePulse, "audio.bass", 0.08f, 1400.0f, 3200.0f},
                     HeroReaction{HeroBehaviour::EmissionPulse, "beat.pulse", 0.9f, 90.0f, 700.0f},
                     HeroReaction{HeroBehaviour::ColorShift, "audio.spectralCentroid", 0.15f, 1800.0f,
                                  3600.0f}}),
        // Machinery. It hovers on the bass and its lights run on the beat clock; it does not
        // breathe or change colour, because it is not alive.
        makeProfile("craft",
                    {HeroReaction{HeroBehaviour::Hover, "audio.bass", 0.9f, 700.0f, 1800.0f},
                     HeroReaction{HeroBehaviour::EmissionPulse, "beat.pulse", 1.2f, 40.0f, 260.0f},
                     HeroReaction{HeroBehaviour::Rotation, "beat.phrase", 0.25f, 2000.0f, 4000.0f}}),
        // Deliberately almost nothing. Most heroes in a world should be on this one: the ones that
        // react are worth noticing only because the others do not.
        makeProfile("still", {HeroReaction{HeroBehaviour::EmissionPulse, "audio.rms", 0.12f, 1600.0f,
                                           4000.0f}}),
    };
    return profiles;
}

const HeroReactionProfile* findHeroReactionProfile(std::string_view name) {
    for (const auto& p : heroReactionProfiles()) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace avgen::world
