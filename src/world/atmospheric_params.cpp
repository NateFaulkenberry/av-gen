#include "world/atmospheric_params.hpp"

#include "params/parameter_set.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <algorithm>
#include <vector>

namespace avgen::world {
namespace {

// ADR-500. This file used to hold the tables: four `constexpr FloatField[]`s, three `switch`es to
// pick one, `sanitise` as a list of fifteen clamps, and `defaultAtmosphericRoutes` as a `switch`
// with an arm per kind. All four are gone. Each effect declares its rows, its clamps and its routes
// in its own file, and what is left here is the three loops -- register, apply, capture -- that
// ADR-387 correctly identified as the part of the design that was already right.
//
// `copyParameters` still serves apply and capture from one walk, so the two cannot drift. That was
// the load-bearing property before and it is unchanged.

// Desc builders, the same shape `effect_params.cpp` uses.
params::ParamDesc<float> f(std::string path, float def, float lo, float hi, float slo, float shi) {
    params::ParamDesc<float> d;
    d.path = std::move(path);
    d.defaultValue = std::clamp(def, lo, hi);
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = slo;
    d.softMax = shi;
    return d;
}

// ADR-566: a Choice row registers as an INT.
//
// It is carried as a float index everywhere else, and for a while it registered as a float too --
// which the conformance round trip caught within one suite. `distinctValue` sets a parameter to a
// fraction of its range, the serialiser writes the NAME of the index that fraction rounds to, and
// what comes back is an integer that does not equal 1.55. **The value did not survive the file,
// and it was right to say so**: a control whose parameter can hold 1.55 has states the file cannot
// represent, and a route or a keyframe could put it in one.
//
// `params::Components<int>::set` rounds on the way in, so the whole chain -- slider, route,
// project parameter, save -- is integral and the representable states are exactly the choices.
params::ParamDesc<int> i(std::string path, int def, int lo, int hi) {
    params::ParamDesc<int> d;
    d.path = std::move(path);
    d.defaultValue = std::clamp(def, lo, hi);
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = lo;
    d.softMax = hi;
    return d;
}

params::ParamDesc<bool> b(std::string path, bool def) {
    params::ParamDesc<bool> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = false;
    d.hardMax = true;
    return d;
}

params::ParamDesc<glm::vec3> col(std::string path, glm::vec3 def) {
    params::ParamDesc<glm::vec3> d;
    d.path = std::move(path);
    d.defaultValue = def;
    // HDR headroom on the hard range, the picker's range on the soft one: a comet's core colour is
    // a radiance and a route may legitimately push it past white.
    d.hardMin = glm::vec3(0.0f);
    d.hardMax = glm::vec3(8.0f);
    d.softMin = glm::vec3(0.0f);
    d.softMax = glm::vec3(1.0f);
    d.isColor = true;
    return d;
}

} // namespace

std::string atmosphericParameterPrefix(std::string_view effectName) {
    std::string prefix = "atmos/";
    prefix.append(effectName);
    prefix.push_back('/');
    return prefix;
}

const AtmosphericParams* AtmosphericParameters::find(std::string_view name) const {
    for (const AtmosphericParams& p : effects) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

AtmosphericParameters registerAtmosphericParameters(params::ParameterSet& params,
                                                    std::span<const AtmosphericEffect> effects) {
    AtmosphericParameters out;
    out.effects.reserve(effects.size());
    for (const AtmosphericEffect& e : effects) {
        const EffectSchema* schema = effectSchema(e.kind);
        if (schema == nullptr) {
            // A kind with no schema. It registers nothing, so nothing about it is automatable --
            // and `checkRegistry` has already named it in the CPU suite, which is where the news
            // belongs rather than in a frame nobody is watching.
            continue;
        }
        const std::string base = atmosphericParameterPrefix(e.name);
        const auto path = [&](const char* leaf) {
            std::string full = base + leaf;
            out.registered.push_back(full); // exact unregister, not a suffix table
            return full;
        };

        AtmosphericParams p;
        p.name = e.name;
        p.kind = e.kind;
        p.enabled = &params.add(b(path("enabled"), e.enabled));

        // Only the rows for the kind this effect actually is, then the shared ones. An effect keeps
        // the settings of the kind it is not -- that is why every payload exists -- but registering
        // them all would put ninety parameters in the table for every one an artist can reach, and
        // most of them would do nothing, which is worse than their being absent.
        p.values.reserve(schema->fields.size() + sharedEffectFields().size());
        const auto addRow = [&](const EffectField& field) {
            switch (field.type) {
            case FieldType::Float:
                p.values.push_back(&params.add(f(path(field.leaf), fieldFloat(field, *schema, e),
                                                 field.hardMin, field.hardMax, field.softMin,
                                                 field.softMax)));
                break;
            case FieldType::Color:
                p.values.push_back(&params.add(col(path(field.leaf), fieldColor(field, *schema, e))));
                break;
            // ADR-566: a Choice is a float index in the parameter table. That is what lets a
            // project parameter (ADR-264) and a modulation route reach it at all -- both speak
            // floats -- and the hard range the factory set is 0..count-1, so neither can select a
            // primitive that does not exist.
            case FieldType::Bool:
                p.values.push_back(&params.add(b(path(field.leaf), fieldBool(field, *schema, e))));
                break;
            case FieldType::Choice:
                p.values.push_back(&params.add(
                    i(path(field.leaf), static_cast<int>(fieldFloat(field, *schema, e) + 0.5f), 0,
                      std::max(field.choiceCount - 1, 0))));
                break;
            }
        };
        for (const EffectField& field : schema->fields) {
            addRow(field);
        }
        for (const EffectField& field : sharedEffectFields()) {
            addRow(field);
        }
        out.effects.push_back(std::move(p));
    }
    return out;
}

void unregisterAtmosphericParameters(params::ParameterSet& params, AtmosphericParameters& registered) {
    for (const std::string& path : registered.registered) {
        params.remove(path);
    }
    registered.registered.clear();
    registered.effects.clear();
}

namespace {

// One walk serving both directions, so the two cannot drift. `fromBase` picks what a save wants
// (what somebody authored) over what the renderer wants (this frame's modulated finals).
void copyParameters(const AtmosphericParameters& registered, std::vector<AtmosphericEffect>& effects,
                    bool fromBase) {
    const auto value = [fromBase](const params::IParameter* p, std::size_t component) {
        return fromBase ? p->baseComponent(component) : p->finalComponent(component);
    };
    for (AtmosphericEffect& e : effects) {
        const AtmosphericParams* p = registered.find(e.name);
        // Effects the registrar never saw are skipped rather than zeroed: a scene that added an
        // effect this frame has one the parameter table does not know about yet, and stamping
        // defaults onto it would erase what the file said.
        if (p == nullptr || p->kind != e.kind) {
            continue;
        }
        const EffectSchema* schema = effectSchema(e.kind);
        if (schema == nullptr) {
            continue;
        }
        if (p->enabled != nullptr) {
            e.enabled = value(p->enabled, 0) >= 0.5f;
        }
        std::size_t i = 0;
        const auto copyRow = [&](const EffectField& field) {
            if (i >= p->values.size() || p->values[i] == nullptr) {
                ++i;
                return;
            }
            const params::IParameter* q = p->values[i];
            switch (field.type) {
            case FieldType::Float: setFieldFloat(field, *schema, e, value(q, 0)); break;
            case FieldType::Color:
                setFieldColor(field, *schema, e, glm::vec3(value(q, 0), value(q, 1), value(q, 2)));
                break;
            case FieldType::Bool: setFieldBool(field, *schema, e, value(q, 0) >= 0.5f); break;
            case FieldType::Choice: setFieldFloat(field, *schema, e, value(q, 0)); break;
            }
            ++i;
        };
        for (const EffectField& field : schema->fields) {
            copyRow(field);
        }
        for (const EffectField& field : sharedEffectFields()) {
            copyRow(field);
        }
        if (!fromBase) {
            // Belt and braces before the values reach the resolver: a route can drive a final
            // anywhere inside the *hard* range, and a few of these are divisors. The hard ranges
            // already exclude zero where it matters, so this is the second line rather than the
            // first -- and it is now each row's own floor rather than a list beside them.
            sanitiseEffect(e);
        }
    }
}

} // namespace

void applyAtmosphericParameters(const AtmosphericParameters& registered, std::vector<AtmosphericEffect>& live) {
    copyParameters(registered, live, false);
}

void captureAtmosphericParameters(const AtmosphericParameters& registered,
                                  std::vector<AtmosphericEffect>& authored) {
    copyParameters(registered, authored, true);
}

std::vector<params::ModRoute> defaultAtmosphericRoutes(std::string_view effectName, AtmosphereKind kind) {
    // ADR-392's defect was that this function was `if aurora else comet`, so a vortex fell into the
    // comet arm and was handed three routes aimed at paths it does not register. Nothing failed: a
    // route naming an unregistered path binds to nothing, warns once at load, and is thereafter
    // indistinguishable from an effect nobody automated.
    //
    // ADR-500 removes the shape rather than fixing the arms. The routes are declared as leaves
    // beside the rows they aim at, in the kind's own file, so a target outside the effect's own
    // prefix is now unspellable -- the leaf is appended to the prefix here and nowhere else -- and
    // a target the kind does not declare is named by `checkRegistry` in the CPU suite.
    std::vector<params::ModRoute> routes;
    const EffectSchema* schema = effectSchema(kind);
    if (schema == nullptr) {
        return routes;
    }
    const std::string base = atmosphericParameterPrefix(effectName);
    routes.reserve(schema->routes.size());
    for (const EffectRoute& r : schema->routes) {
        params::ModRoute route;
        route.source = r.source;
        route.target = base + r.leaf;
        route.amount = r.amount;
        // `Add` so silence leaves the authored pose exactly as it was written. A `Multiply` route
        // would make an unplayed project look wrong, which is the discipline scene/tree_audio.hpp
        // states and the reason every depth is small next to the value it moves.
        route.op = params::ModOp::Add;
        route.chain.attackMs = r.attackMs;
        route.chain.decayMs = r.decayMs;
        routes.push_back(std::move(route));
    }
    return routes;
}

} // namespace avgen::world
