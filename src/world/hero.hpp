#pragma once

// Heroes (ADR-072): the things in a world worth travelling towards.
//
// A composed world without them is a texture. It can be dense, well lit, correctly hierarchical and
// still have nothing in it that rewards being approached, because every instance of a scatter layer
// is interchangeable with every other instance of that layer -- that is what a scatter layer *is*.
// A hero is the opposite: one object, in one place, that the composition is arranged around.
//
// Two decisions here are load-bearing and both come out of the Glowmere audit.
//
// **A hero is an authored assembly, not a generated shape.** Glowmere's elder is three procedural
// nodes -- a squashed sphere with two displacement deformers, a five-point curved tube, and
// thirty-eight radial filaments -- plus a practical light underneath it. Its silhouette works
// because those three parts have wildly different scales and because the filaments move while the
// cap does not. No parameterisation of "a mushroom" would produce it, and a generator general
// enough to try would produce a worse elder and be used by nothing else. So a hero *names* an
// assembly and places it; what the assembly contains is somebody's design.
//
// **Placement is compositional, not random.** A HeroPoint carries what a camera director needs to
// discover it -- how important it is, how far away it wants to be seen from, at what angle, and how
// close you must get before it matters. Scattering heroes uniformly would make them ordinary, which
// defeats the entire point of having them.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace avgen::world {

// How a hero answers the music. Deliberately a small vocabulary of *authored* behaviours rather
// than a general modulation graph: the point is that not every hero does everything, and a system
// that made it easy to make everything react to everything would be used that way.
enum class HeroBehaviour : std::uint8_t {
    EmissionPulse,   // brightness moves with the signal
    ColorShift,      // hue drifts
    Hover,           // rises and falls
    ScalePulse,
    Rotation,        // continuous, rate scaled by the signal
    LightBurst,      // its practical light surges
    ParticleEmission,
    Reveal,          // one-shot: it becomes visible, or opens
};
[[nodiscard]] const char* heroBehaviourName(HeroBehaviour b);

// One authored reaction. `source` is a signal path the modulation system already publishes --
// "audio.bass", "beat.pulse", "musical.drop" -- because a hero reacting to music must go through
// the same routes as everything else that reacts to music. A second reaction system beside the
// modulator would be a second thing to debug when a scene does not move.
struct HeroReaction {
    HeroBehaviour behaviour = HeroBehaviour::EmissionPulse;
    std::string source = "audio.bass";
    float amount = 0.2f;       // in the target parameter's own units
    float attackMs = 400.0f;
    float decayMs = 1200.0f;
    [[nodiscard]] Result<void> validate() const;
};

// A named set of them. Kept as a profile rather than inlined per hero so that "the UFO behaves like
// a UFO" is a thing that can be said once and reused, and so an artist editing how UFOs behave does
// not have to find every UFO.
struct HeroReactionProfile {
    std::string name;
    std::vector<HeroReaction> reactions;
    [[nodiscard]] Result<void> validate() const;
};

// A hero, placed.
struct HeroPoint {
    std::string name;
    // The library asset the world composer placed here, when one did. Empty for a hero declared in
    // the editor or by hand: what stands there is the scene's own node of the same name.
    //
    // A hero is **one object**. It used to be able to name an "assembly" instead -- a group of nodes
    // sharing a prefix -- which meant a hero that no row in the editor was, that no reaction could
    // reach (reactions are wired to `nodes/<hero name>/...`), and that nothing but a naming
    // convention tied to anything. See ADR-107.
    std::string assetId;

    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float scale = 1.0f;
    // The hero's own size in metres: half its horizontal extent, and its height. Carried rather
    // than derived, because `scale` means nothing without the asset's bounds and the two things
    // that need this most -- how much space to clear around it, and how far a camera should stand
    // off -- must not have to hold the asset library to find out how big something is.
    float radius = 1.0f;
    float height = 1.0f;

    // What a camera director sorts by. This is the single most useful number here: it is how the
    // director decides what the shot is about, and heroes that all claim 1.0 are heroes among which
    // nothing can be chosen.
    float importance = 0.5f;
    // How strongly the composition should clear space around it and point at it. Separate from
    // importance because a thing can be the subject of a shot without dominating the world, and a
    // thing can dominate the world while the camera is looking elsewhere.
    float focalWeight = 0.5f;

    // Metres. What the director should use as a stand-off, and where the eye should be relative to
    // the horizon. A forty-metre tree and a two-metre artefact want completely different framing
    // and neither of them wants "the default".
    float preferredCameraDistance = 30.0f;
    float preferredCameraElevationDegrees = 8.0f;
    // How close the camera must be before this hero is worth activating at all. Used to keep
    // distant heroes cheap and to stop everything reacting to the music at once.
    float activationRadius = 120.0f;

    // The colour this hero owns. Usually the profile's reserved accent, which is what makes it
    // findable from anywhere in frame; a world with several heroes may vary it deliberately.
    glm::vec3 colorAccent{1.0f, 0.47f, 0.15f};
    std::string reactionProfile;   // names a HeroReactionProfile; empty means it does not react

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<HeroPoint> fromJson(const nlohmann::json& j);
};

// Where a behaviour lands on a placed node: a parameter path suffix under "nodes/<hero>/" and a
// vector component (-1 for every component).
//
// Only the behaviours with somewhere real to go are wired. A colour shift, a light burst, particle
// emission and a one-shot reveal each need machinery a placed glTF node does not have -- a material
// program with a hue input, a practical light of its own, a particle system, a trigger rather than
// a continuous signal. Routing them at whatever happens to be nearby would produce heroes that
// appear to react and are in fact doing something else, which is worse than a hero that visibly
// does nothing. `missing` says what each would need.
struct HeroBehaviourTarget {
    const char* suffix = "";
    int component = -1;
    bool supported = false;
    const char* missing = "";
};
[[nodiscard]] HeroBehaviourTarget heroBehaviourTarget(HeroBehaviour behaviour);

// How much a hero wants to outrank the population around it, given the world's focal strength.
// Exposed because it is the one judgement in hero placement worth testing directly.
[[nodiscard]] float heroClearanceRadius(const HeroPoint& hero);

// The built-in reaction profiles. Authored, not generated: each says what one *kind* of thing does,
// and deliberately leaves most behaviours out. A hero that pulses, shifts colour, hovers, rotates,
// bursts, emits and reveals all at once is a hero doing none of them legibly.
[[nodiscard]] const std::vector<HeroReactionProfile>& heroReactionProfiles();
[[nodiscard]] const HeroReactionProfile* findHeroReactionProfile(std::string_view name);

} // namespace avgen::world
