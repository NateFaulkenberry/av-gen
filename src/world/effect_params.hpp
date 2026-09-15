#pragma once

// World effect parameters (ADR-207, §16 of the brief): every meaningful number on a world effect,
// exposed through the parameter system so it can be automated, keyed, preset, cued and modulated by
// exactly the routes everything else in the engine uses.
//
// **There is no audio hook in `world::effects`, and there is not going to be one.** A pulse that
// answers the beat is a `beat.pulse -> worldfx/<name>/intensity` route, because that is how a hero
// reacts (ADR-072), how a light rig reacts and how a post chain reacts. A second reaction system
// beside the modulator would be a second thing to debug when a scene does not move.
//
// The path shape is `worldfx/<effect name>/<property>`, so "worldfx" is the UI group and the
// effect's own name addresses it -- the same shape as `nodes/<name>/position` and for the same
// reason. `WorldEffect::validate` refuses a '/' in a name, which is what stops an effect inventing
// a group nobody can find.
//
// Registration is dynamic: the number of effects is whatever the scene declared. The registrar keeps
// its own list of every path it wrote (the `entity::EntityWorld` pattern) rather than a hard-coded
// suffix table, because an unregister that forgets a suffix leaks a parameter into the next scene.

#include "params/parameter.hpp"
#include "world/effects.hpp"

#include <span>
#include <string>
#include <vector>

namespace avgen::params {
class ParameterSet;
}

namespace avgen::world {

// The parameters of one effect. Null before registration and after `unregister`, which is what
// `applyWorldEffectParameters` tests before it touches anything.
struct WorldEffectParams {
    std::string name;
    params::Parameter<bool>* enabled = nullptr;

    params::Parameter<glm::vec3>* color = nullptr;
    params::Parameter<float>* intensity = nullptr;
    params::Parameter<glm::vec3>* edgeColor = nullptr;
    params::Parameter<float>* edgeIntensity = nullptr;
    params::Parameter<float>* width = nullptr;

    params::Parameter<bool>* rainbow = nullptr;
    params::Parameter<float>* rainbowSpeed = nullptr;
    params::Parameter<float>* rainbowScale = nullptr;
    params::Parameter<float>* rainbowSaturation = nullptr;
    params::Parameter<float>* rainbowBrightness = nullptr;

    params::Parameter<bool>* sparkle = nullptr;
    params::Parameter<float>* sparkleDensity = nullptr;
    params::Parameter<float>* sparkleSize = nullptr;
    params::Parameter<float>* sparkleIntensity = nullptr;
    params::Parameter<float>* sparkleSpeed = nullptr;

    params::Parameter<float>* speed = nullptr;
    params::Parameter<float>* range = nullptr;
    params::Parameter<float>* frontWidth = nullptr;
    params::Parameter<float>* trailLength = nullptr;
    params::Parameter<float>* falloff = nullptr;
    params::Parameter<float>* startOffset = nullptr;
    params::Parameter<float>* verticalExtent = nullptr;
    params::Parameter<float>* ringCount = nullptr;
    params::Parameter<float>* beamRadius = nullptr;

    params::Parameter<float>* responseGround = nullptr;
    params::Parameter<float>* responseFoliage = nullptr;
    params::Parameter<float>* responseSurface = nullptr;
    params::Parameter<float>* responseEmissive = nullptr;

    params::Parameter<float>* delay = nullptr;
    params::Parameter<float>* lifetime = nullptr;
    params::Parameter<float>* fadeIn = nullptr;
    params::Parameter<float>* fadeOut = nullptr;
    params::Parameter<float>* repeat = nullptr;
};

struct WorldEffectParameters {
    std::vector<WorldEffectParams> effects;
    // Every path this registrar wrote, so unregistering is exact rather than a suffix table that can
    // fall behind the struct above.
    std::vector<std::string> registered;

    [[nodiscard]] bool empty() const { return effects.empty(); }
    // The parameter block for an effect by name, or null. Linear: there are single digits of these.
    [[nodiscard]] const WorldEffectParams* find(std::string_view name) const;
};

// The parameter path prefix for an effect, including the trailing '/'. One function so the panel,
// the registrar and any test agree on what a path looks like.
[[nodiscard]] std::string worldEffectParameterPrefix(std::string_view effectName);

// Declares `worldfx/<name>/...` for every effect in `effects`, taking each parameter's default from
// the authored value. Idempotent in the way `ParameterSet::add` is: re-registering the same paths
// returns the existing parameters and leaves their values alone.
[[nodiscard]] WorldEffectParameters registerWorldEffectParameters(params::ParameterSet& params,
                                                                  std::span<const WorldEffect> effects);

// Removes every path this registrar wrote and clears it. Call `Timeline::unbind()` first: a track
// aimed at a departing path holds a pointer to it.
void unregisterWorldEffectParameters(params::ParameterSet& params, WorldEffectParameters& registered);

// Copies this frame's **final** (post-modulation) values onto `live`. Matching is by name, so an
// effect the registrar never saw is left exactly as authored rather than silently zeroed.
void applyWorldEffectParameters(const WorldEffectParameters& registered, std::span<WorldEffect> live);

// The mirror: copies each parameter's **base** value onto `authored`. What a save and a structural
// edit want, and the reason it is a separate call rather than the same one -- the finals carry this
// frame's beat on them, and writing those back would bake the music into the file.
void captureWorldEffectParameters(const WorldEffectParameters& registered, std::span<WorldEffect> authored);

} // namespace avgen::world
