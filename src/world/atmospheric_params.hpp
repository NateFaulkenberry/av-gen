#pragma once

// Atmospheric effect parameters (ADR-230, §8 of the brief): every meaningful number on a comet or an
// aurora, exposed through the parameter system so it can be automated, keyed, preset, cued and
// modulated by exactly the routes everything else in the engine uses.
//
// **There is no audio hook in `world::atmospherics`, and there is not going to be one.** This is
// `world/effect_params.hpp`'s rule, inherited deliberately: an aurora that answers the bass is a
// `audio.bass -> atmos/<name>/curtainHeight` route, because that is how a hero reacts, how a light
// rig reacts and how a post chain reacts. The one exception is the aurora's *spectrum vector* --
// sixteen numbers that no scalar route can carry -- and it is documented in `atmospherics.hpp`
// rather than smuggled in here.
//
// The path shape is `atmos/<effect name>/<property>`. A separate group from `worldfx` rather than a
// child of it, because the two families are registered by different code with different lifetimes
// and a shared prefix would need the two registrars to agree about names they cannot see.
// `AtmosphericEffect::validate` refuses a '/' in a name, which is what stops an effect inventing a
// group nobody can find.
//
// **The registrar is table-driven**, which is the one place this file departs from
// `effect_params.cpp`. That file names 33 parameters three times over -- once to register, once to
// copy finals in, once to copy bases out -- and that is a readable amount. A comet and an aurora
// have about ninety between them, and ninety names written three times is 270 lines in which a
// single typo is a parameter that registers and then silently never applies. Here each parameter is
// one row with an accessor pair, and register/apply/capture are three loops over the rows, so a row
// that exists is a parameter that works in all three directions or in none.

#include "params/modulation.hpp"
#include "params/parameter.hpp"
#include "world/atmospherics.hpp"

#include <span>
#include <string>
#include <vector>

namespace avgen::params {
class ParameterSet;
}

namespace avgen::world {

// `atmos/<name>/`.
[[nodiscard]] std::string atmosphericParameterPrefix(std::string_view effectName);

// The parameters of one effect, as pointers cached at registration.
//
// Parallel to the tables in the .cpp rather than named members: the whole point of the table is
// that nothing names a parameter twice. Null before registration and after `unregister`, which is
// what `applyAtmosphericParameters` tests before it touches anything.
struct AtmosphericParams {
    std::string name;
    AtmosphereKind kind = AtmosphereKind::Comet;
    params::Parameter<bool>* enabled = nullptr;
    // ADR-500: one entry per row of `effectSchema(kind)->fields`, then one per row of
    // `sharedEffectFields()`, in that order. A single vector rather than three because the schema is
    // a single ordered list -- three would reintroduce the positional bookkeeping that made
    // `copyParameters` count floats, colours and bools separately and get it right by inspection.
    std::vector<params::IParameter*> values;
};

struct AtmosphericParameters {
    std::vector<AtmosphericParams> effects;
    // Every path the registrar wrote, so an unregister is exact rather than a suffix table that can
    // forget one and leak it into the next scene (the `entity::EntityWorld` pattern ADR-207 cites).
    std::vector<std::string> registered;
    [[nodiscard]] const AtmosphericParams* find(std::string_view name) const;
};

[[nodiscard]] AtmosphericParameters registerAtmosphericParameters(params::ParameterSet& params,
                                                                  std::span<const AtmosphericEffect> effects);

// Removes every path the registrar wrote. Call `Timeline::unbind()` first: a track aimed at a
// departing path holds a pointer to it.
void unregisterAtmosphericParameters(params::ParameterSet& params, AtmosphericParameters& registered);

// This frame's **finals** -- base, plus automation, plus modulation -- onto the live set the
// renderer reads.
void applyAtmosphericParameters(const AtmosphericParameters& registered,
                                std::vector<AtmosphericEffect>& live);

// The **bases** -- what somebody actually authored -- onto the set a save writes. The finals carry
// this frame's beat on them, and writing those back would bake the music into the file.
void captureAtmosphericParameters(const AtmosphericParameters& registered,
                                  std::vector<AtmosphericEffect>& authored);

// ADR-500: every one of the functions below is now one loop over the kind's schema. The five
// hand-written per-kind lists ADR-392 counted -- `toJson`, `fromJson`, `sanitise`, the style presets
// and this file's own `defaultAtmosphericRoutes` -- are gone; each effect declares its own in
// `world_effects/effects/<name>_effect.cpp`.

// The default modulation routes for an effect of this kind, as data.
//
// Returned rather than installed, in the `scene::tree_audio.cpp` style, so a caller can inspect them
// in a test with no engine and so the UI can offer them without owning the depths. Depths follow the
// house discipline: `op: add` so silence leaves the authored pose alone, and event routes shaped
// with an attack and a decay rather than used as a gate.
[[nodiscard]] std::vector<params::ModRoute> defaultAtmosphericRoutes(std::string_view effectName,
                                                                     AtmosphereKind kind);

} // namespace avgen::world
