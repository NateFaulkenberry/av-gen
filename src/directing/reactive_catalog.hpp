#pragma once

// ADR-925: the reactive catalogue -- every target in the scene an audio-reactivity planner may move,
// generated from the engine's own data (ADR-754), with what moving it does.
//
// The Director's capability registry (ADR-754) says what characters, cameras, events and effects can
// do. It said nothing about what can respond to the music, so no director could plan reactivity
// (the director audit, recommendation 2), and the first pass of Glowmere Valley 3 configured 24
// route-target pairs of which one reached the pixels. This is the missing card: for each target,
//
//   * **its neutral value** -- where it rests: its base, the look the scene was authored with;
//   * **a safe range** -- how far a route should move it before the look breaks (a hue past +0.08 of
//     a turn clips the valley's teal; a mushroom layer's glow past +-30% stops reading as the same
//     light), measured values from the GV3 revision's reports where they exist;
//   * **what kind of change it makes** -- luminance, hue, motion or density;
//   * **the level it acts at** -- micro (a small, local thing), meso (a hero, a cluster, a wave), macro
//     (the world's light, air and colour), the owner's brief section 5;
//   * **who owns it** -- the hero, scatter layer, particle system or effect that answers, so a plan
//     can give each its own musical layer and a validator can tell when one is over-saturated.
//
// **Nothing here is a list of the scene's objects.** Which targets exist is read from the registered
// parameter set, the composition (its nodes, heroes, scatter layers, fields, lights) and the effect
// list; whether each can reach the picture is asked of the liveness registry (ADR-902), and a dead
// target is not listed -- it is `excluded`, with the rule's reason, so the catalogue can never offer
// what the validator would refuse. What the engine does not carry is the *vocabulary*: that an
// `emissionGain` is a glow and a `windSpeed` is motion. That is one small table of path families
// (reactive_catalog.cpp), tested against every family it names.

#include "directing/plan_route.hpp"
#include "params/liveness.hpp"
#include "params/modulation.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::params {
class ParameterSet;
}
namespace avgen::scene {
class Composition;
}
namespace avgen::world {
struct EffectInstance;
}

namespace avgen::directing {

// What a viewer sees change when the target moves.
enum class ReactiveKind : std::uint8_t { Luminance, Hue, Motion, Density };
[[nodiscard]] const char* reactiveKindName(ReactiveKind kind);

// The kind of thing a target is. The names are the catalogue's words for a plan's `group`.
enum class ReactiveGroup : std::uint8_t {
    HeroEmission,     // nodes/<hero part>/emissiveBoost: a hero's own glow, after its program (ADR-903)
    NodeEmission,     // nodes/<n>/emissiveBoost on an emitting node that is no hero's
    MaterialEmission, // material/<p>/emissionIntensity and a layer's: every surface on the program together
    ScatterGlow,      // nodes/<terrain>/scatter/<layer>/emissionGain (ADR-905)
    ScatterHue,       // .../hueOffset: a layer's displayed hue, in turns
    ScatterWave,      // .../emissiveFieldAmount on a layer that names a field (ADR-905/906)
    NodeWave,         // procedural/<n>/emissiveFieldAmount on a node that names a field
    Particles,        // particles/<n>/emissive (glow) and spawnRate (density)
    Effect,           // fx/<id>/intensity|glow|gain of an effect that can fire
    Light,            // lightrig/<rig>/<light>/intensity; the rig's key and ambient
    EcologyLight,     // scene/ecologyLight: the light the glowing plants and fungi cast (ADR-905)
    Atmosphere,       // scene/volumeDensity, volumeScattering: the air
    Wind,             // scene/windSpeed, scene/wind/gustAmount, scene/wind/turbulence
    Water,            // nodes/<terrain>/water/glow|sparkle|ripple|swell|foam
    WaterTears,       // nodes/<terrain>/water/tears|tearShear|tearCoverage (the water stream's)
};
[[nodiscard]] const char* reactiveGroupName(ReactiveGroup group);
[[nodiscard]] std::optional<ReactiveGroup> reactiveGroupFromName(std::string_view name);
// Every group, in declaration order.
[[nodiscard]] std::span<const ReactiveGroup> allReactiveGroups();

struct ReactiveTarget {
    std::string path;
    int component = -1;
    ReactiveGroup group = ReactiveGroup::NodeEmission;
    ReactiveKind kind = ReactiveKind::Luminance;
    ReactiveLevel level = ReactiveLevel::Meso;
    std::string owner;      // a node, "<terrain>/<layer>", a particle node, an effect id, or "world"
    std::string hero;       // the hero point it belongs to, or ""
    std::string label;      // what a viewer sees: "fungi glow", "light cast by glowing plants and fungi"
    float base = 0.0f;      // the parameter's base now
    float neutral = 0.0f;   // where it rests: its base (the authored look), or 0 for an offset
    float safeMin = 0.0f;   // the range a route should keep it inside, absolute
    float safeMax = 0.0f;
    params::ModOp op = params::ModOp::Multiply; // how a route should move it: about its base, or by an offset
    std::optional<glm::vec3> position;          // where it is, when it has one place
    float size = 0.0f;                          // metres: a layer's instance height, a hero's radius
    // How much its owner emits at rest, for ranking what reads: a scatter layer's authored intensity
    // times its colour's brightest channel (its program's intensity when it authors none), a node's
    // brightest surface. And whether that light is a material program's (ADR-179), which is where a
    // glowing part's light comes from; a plain emissive part beside it is a fainter accent.
    float emission = 0.0f;
    bool programLit = false;
    bool global = false;                        // moves the whole frame's look (a key light): not a plan's to pulse
    // Its owner is a character (a node an entity drives), something a staging scenario moves, or under
    // one: what it does is the simulation's or the scenario's (a scenario sets an abductee's glow).
    bool scripted = false;
    std::vector<std::string> sharedBy;          // a material's: every node and layer drawn with the program
    std::string field;                          // a wave's: the field its owner names
    bool fieldTriggered = false;                // ...whose clock starts at a musical event (ADR-906)
    bool keyed = false;                         // the author keys it (a timeline track)
    std::vector<std::string> drivenBy;          // sources of the authored routes already on it
    std::vector<params::liveness::Finding> hazards; // liveness hazards that did not exclude it
};

// A hero point and what answers for it: its emitting parts, the particle systems under them, the
// practical lights beside it -- found from the scene (parents, and nodes standing within its reach),
// never from a naming convention. A character (a node an entity drives) or anything a staging
// scenario moves is no hero's part: where it stands, and what its glow does, belong to its
// simulation or its scenario, and a route would fight them.
struct ReactiveHero {
    std::string name;
    glm::vec3 position{0.0f};
    float radius = 1.0f;
    float importance = 0.5f;
    std::vector<std::string> members; // node names
};

struct ReactiveCatalog {
    std::vector<ReactiveTarget> targets;
    std::vector<ReactiveHero> heroes;
    // Parameters of a reactive family that are not offered, each "path: why" -- dead by a liveness
    // rule, a phase rate, a base of zero that a multiply cannot move.
    std::vector<std::string> excluded;

    [[nodiscard]] const ReactiveTarget* find(std::string_view path) const;
    [[nodiscard]] std::vector<const ReactiveTarget*> inGroup(ReactiveGroup group) const;
    [[nodiscard]] const ReactiveHero* hero(std::string_view name) const;
    [[nodiscard]] bool empty() const { return targets.empty(); }
    [[nodiscard]] nlohmann::json toJson() const;
};

// What the catalogue is generated from. Pointers are borrowed for the call; null = none.
struct ReactiveInputs {
    const params::ParameterSet* params = nullptr;
    const params::liveness::Facts* liveness = nullptr; // the scene's facts (scene::SceneLivenessFacts)
    const scene::Composition* composition = nullptr;
    std::span<const world::EffectInstance> effects;
    std::span<const params::ModRoute> routes;          // the project's authored routes
    std::span<const std::string> keyedTargets;         // parameters the author keys on the timeline
};

[[nodiscard]] ReactiveCatalog buildReactiveCatalog(const ReactiveInputs& in);

} // namespace avgen::directing
