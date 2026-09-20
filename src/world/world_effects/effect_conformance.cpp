#include "world/world_effects/effect_conformance.hpp"

#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "world/atmospheric_params.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace avgen::world::conformance {
namespace {

constexpr std::string_view kProbeName = "conformance probe";

void report(Report& out, AtmosphereKind kind, std::string_view rule, std::string detail) {
    out.findings.push_back(Finding{atmosphereKindName(kind), std::string(rule), std::move(detail)});
}

// A value inside the parameter's range that is not its default, varied by component index so two
// fields of one effect do not get the same number -- a round trip that swapped two fields would
// otherwise pass. Prefers the soft range, because that is what an artist can reach; falls back to
// the hard range for a parameter whose soft range has collapsed.
float distinctValue(const params::IParameter& p, std::size_t component, std::size_t salt) {
    const float lo = p.hardMin(component);
    const float hi = p.hardMax(component);
    const float def = p.defaultComponent(component);
    if (p.kind() == params::ParamKind::Bool) {
        return def >= 0.5f ? 0.0f : 1.0f;
    }
    float slo = p.softMin(component);
    float shi = p.softMax(component);
    if (!(shi > slo)) {
        slo = lo;
        shi = hi;
    }
    if (!(shi > slo)) {
        return def; // a pinned parameter; nothing to vary, and nothing to get wrong
    }
    // Seven fractions, so consecutive fields differ and the cycle does not align with any
    // plausible field ordering.
    static constexpr float kFractions[] = {0.31f, 0.67f, 0.44f, 0.82f, 0.23f, 0.58f, 0.71f};
    for (std::size_t attempt = 0; attempt < std::size(kFractions); ++attempt) {
        const float t = kFractions[(salt + attempt) % std::size(kFractions)];
        const float v = std::clamp(slo + t * (shi - slo), lo, hi);
        if (std::abs(v - def) > 1e-4f * std::max(1.0f, std::abs(def))) {
            return v;
        }
    }
    return def;
}

} // namespace

std::string Report::summary() const {
    std::string out;
    for (const Finding& f : findings) {
        out += f.subject;
        out += " [";
        out += f.rule;
        out += "] ";
        out += f.detail;
        out += '\n';
    }
    return out;
}

AtmosphericEffect probeEffect(AtmosphereKind kind, std::string name) {
    switch (kind) {
    case AtmosphereKind::Comet: return bioluminescentComet(std::move(name));
    case AtmosphereKind::Aurora: return glowmereAurora(std::move(name));
    case AtmosphereKind::Vortex: return cosmicVortex(std::move(name));
    }
    return bioluminescentComet(std::move(name));
}

std::vector<std::string> registeredPaths(const AtmosphericEffect& effect) {
    params::ParameterSet params;
    const AtmosphericEffect one = effect;
    AtmosphericParameters registered = registerAtmosphericParameters(params, std::span(&one, 1));
    // `registered.registered` is the registrar's own list of every path it wrote -- the same list
    // `unregisterAtmosphericParameters` uses -- so this is what the engine believes it created,
    // not what a table says it should have.
    return registered.registered;
}

std::vector<std::string> registeredLeaves(const AtmosphericEffect& effect) {
    const std::string prefix = atmosphericParameterPrefix(effect.name);
    std::vector<std::string> leaves;
    for (const std::string& path : registeredPaths(effect)) {
        leaves.push_back(path.starts_with(prefix) ? path.substr(prefix.size()) : path);
    }
    return leaves;
}

Report checkLeavesExist(AtmosphereKind kind, std::span<const std::string_view> leaves, std::string_view rule) {
    Report out;
    const AtmosphericEffect probe = probeEffect(kind, std::string(kProbeName));
    params::ParameterSet params;
    registerAtmosphericParameters(params, std::span(&probe, 1));
    const std::string prefix = atmosphericParameterPrefix(probe.name);
    for (const std::string_view leaf : leaves) {
        const std::string path = prefix + std::string(leaf);
        const params::IParameter* p = params.find(path);
        if (p == nullptr) {
            report(out, kind, rule, "no parameter at '" + path + "'");
        } else if (!p->flags().exposed) {
            report(out, kind, rule, "'" + path + "' is not exposed, so the panel draws nothing");
        }
    }
    return out;
}

Report checkAtmospheric(AtmosphereKind kind) {
    Report out;
    const AtmosphericEffect probe = probeEffect(kind, std::string(kProbeName));

    // The probe has to be of the kind asked for, or every check below is about something else.
    if (probe.kind != kind) {
        report(out, kind, "probe-kind",
               std::string("probeEffect returned a ") + atmosphereKindName(probe.kind));
        return out;
    }

    // 5. The name survives the file format. A kind whose name does not parse back is a scene that
    //    silently loads as a comet.
    {
        const auto back = atmosphereKindFromName(atmosphereKindName(kind));
        if (!back.has_value() || *back != kind) {
            report(out, kind, "kind-name",
                   std::string("atmosphereKindFromName(\"") + atmosphereKindName(kind) + "\") does not "
                   "return this kind -- atmosphereKindFromName is an if-chain, not a switch");
        }
    }

    params::ParameterSet params;
    std::vector<AtmosphericEffect> effects{probe};
    AtmosphericParameters registered = registerAtmosphericParameters(params, effects);
    const std::string prefix = atmosphericParameterPrefix(probe.name);

    if (registered.registered.empty()) {
        report(out, kind, "registration", "this kind registers no parameters at all");
        return out;
    }

    // 3. Ranges. A default outside the hard range is a value the parameter cannot hold; a soft
    //    range outside the hard one is a slider that clamps silently part-way along.
    for (const std::string& path : registered.registered) {
        const params::IParameter* p = params.find(path);
        if (p == nullptr) {
            report(out, kind, "registration",
                   "'" + path + "' is in the registrar's list but not in the set");
            continue;
        }
        for (std::size_t c = 0; c < p->componentCount(); ++c) {
            const float lo = p->hardMin(c);
            const float hi = p->hardMax(c);
            if (!(hi >= lo)) {
                report(out, kind, "range", "'" + path + "' has hardMax below hardMin");
                continue;
            }
            const float def = p->defaultComponent(c);
            if (def < lo || def > hi) {
                report(out, kind, "range", "'" + path + "' default is outside its hard range");
            }
            if (p->softMin(c) < lo || p->softMax(c) > hi) {
                report(out, kind, "range", "'" + path + "' soft range escapes its hard range");
            }
        }
    }

    // 1. Default routes. This is the check that ADR-387's "a parameter path is three things at
    //    once" makes necessary: a route target is a string, a wrong one binds to nothing, logs one
    //    warning at load and is thereafter indistinguishable from an effect nobody automated.
    {
        const std::vector<params::ModRoute> routes = defaultAtmosphericRoutes(probe.name, kind);
        if (routes.empty()) {
            report(out, kind, "default-routes",
                   "this kind has no default audio routes, so a newly added one is silent");
        }
        for (const params::ModRoute& r : routes) {
            const params::IParameter* p = params.find(r.target);
            if (p == nullptr) {
                report(out, kind, "default-routes",
                       "route '" + r.source + "' -> '" + r.target + "' names a path this kind does "
                       "not register");
            } else if (!p->flags().modulatable) {
                report(out, kind, "default-routes", "'" + r.target + "' is not modulatable");
            }
            if (!r.target.starts_with(prefix)) {
                report(out, kind, "default-routes",
                       "route target '" + r.target + "' is outside this effect's own prefix");
            }
        }
    }

    // 2. ADR-350's round trip, driven through the registration table. Set every registered
    //    component to a distinct non-default value, capture it onto the authored effect (which is
    //    what a save does), serialise, load, serialise again, and require the two documents to
    //    agree. This is the one check that holds the hand-written `toJson`/`fromJson` against the
    //    table, and it does so without asserting the two key sets are equal -- ADR-388 is right
    //    that `speed`/`speedScale` are two namespaces rather than two spellings, and an equality
    //    assertion would fail on working code.
    {
        std::size_t salt = 0;
        for (const std::string& path : registered.registered) {
            params::IParameter* p = params.find(path);
            if (p == nullptr) {
                continue;
            }
            for (std::size_t c = 0; c < p->componentCount(); ++c) {
                p->setBaseComponent(c, distinctValue(*p, c, salt++));
            }
        }
        captureAtmosphericParameters(registered, effects);

        const nlohmann::json first = effects.front().toJson();
        const auto reloaded = AtmosphericEffect::fromJson(first);
        if (!reloaded) {
            report(out, kind, "round-trip",
                   "a fully-populated effect of this kind does not load back: " +
                       reloaded.error().message);
        } else if (reloaded->kind != kind) {
            report(out, kind, "round-trip", "the kind itself did not survive the file");
        } else {
            // The comparison that matters is NOT `toJson(fromJson(toJson(e))) == toJson(e)`. That
            // identity holds even when a field is missing from *both* directions: the key is absent
            // from the first document, `fromJson` leaves the struct's default in place, and the
            // second document is absent it too. Both documents agree, and the field is gone.
            //
            // So the round trip is read back through the registrar instead. Registration takes each
            // parameter's default from the effect's own field, so re-registering the reloaded
            // effect and comparing every default against the value that was set before the save
            // asks the only question worth asking: did this number survive? A leaf the serialiser
            // never knew about comes back as the factory's value and is named here.
            params::ParameterSet after;
            const AtmosphericEffect loaded = *reloaded;
            AtmosphericParameters registeredAfter = registerAtmosphericParameters(after, std::span(&loaded, 1));
            std::string lost;
            std::size_t lostCount = 0;
            for (const std::string& path : registered.registered) {
                const params::IParameter* before = params.find(path);
                const params::IParameter* now = after.find(path);
                if (before == nullptr) {
                    continue;
                }
                if (now == nullptr) {
                    report(out, kind, "round-trip",
                           "'" + path + "' is not registered after the file round trip");
                    continue;
                }
                for (std::size_t c = 0; c < before->componentCount(); ++c) {
                    const float want = before->baseComponent(c);
                    const float got = now->defaultComponent(c);
                    if (std::abs(want - got) > 1e-4f * std::max(1.0f, std::abs(want))) {
                        ++lostCount;
                        if (lostCount <= 12) { // ADR-362: name a handful, do not print a document
                            if (!lost.empty()) {
                                lost += ", ";
                            }
                            lost += path;
                        }
                    }
                }
            }
            if (lostCount > 0) {
                report(out, kind, "round-trip",
                       std::to_string(lostCount) + " component(s) did not survive save -> load; " +
                           "the first of them: " + lost);
            }
            unregisterAtmosphericParameters(after, registeredAfter);
        }
    }

    // 4. Resolution attributes the effect to its own kind. This check is the reason the file
    //    exists: nothing else in the suite would notice a kind resolving as a neighbour.
    //
    //    `resolveAtmosphericEffects` used to dispatch with `if comet / else if aurora / else`, so
    //    the vortex was the fall-through and a forgotten kind was counted as one. That is now an
    //    exhaustive `switch`, and the check below must not reintroduce the same shape: picking
    //    `mine` with a ternary chain whose own `else` is `counts.vortices` would compare a new
    //    kind's result against the vortex counter and agree with itself, which is how this check
    //    silently stopped being able to fail for a fourth kind. Hence the exhaustive switch here
    //    too, and a "no counter" arm for a kind that is resolved by nothing.
    {
        AtmosphericEffect live = probe;
        live.enabled = true;
        live.activation = Activation::Always;
        live.timing = Timing{};
        live.timing.fadeIn = 0.0;
        live.timing.fadeOut = 0.0;

        AtmosphericContext ctx;
        ctx.seconds = 0.5;
        std::array<ResolvedAtmospheric, kMaxGpuComets> comets{};
        std::array<ResolvedAtmospheric, kMaxGpuAuroras> auroras{};
        const AtmosphericCounts counts =
            resolveAtmosphericEffects(std::span(&live, 1), ctx, comets, auroras);

        // Exhaustive, no `default` -- see the note above.
        std::size_t mine = 0;
        switch (kind) {
        case AtmosphereKind::Comet: mine = counts.comets; break;
        case AtmosphereKind::Aurora: mine = counts.auroras; break;
        case AtmosphereKind::Vortex: mine = counts.vortices; break;
        }
        const std::size_t total = counts.comets + counts.auroras + counts.vortices;
        if (mine != 1) {
            report(out, kind, "resolve-dispatch",
                   "one live effect of this kind resolved as " + std::to_string(counts.comets) +
                       " comet(s), " + std::to_string(counts.auroras) + " aurora(s), " +
                       std::to_string(counts.vortices) + " vortex/vortices, " +
                       std::to_string(counts.dropped) + " dropped -- it is not claimed by its own "
                       "arm of the switch in resolveAtmosphericEffects");
        } else if (total != 1) {
            report(out, kind, "resolve-dispatch",
                   "one live effect resolved as " + std::to_string(total) + " effects");
        }
    }

    return out;
}

Report checkAtmosphericFamily() {
    Report out;
    for (const AtmosphereKind kind : kAtmosphereKinds) {
        Report one = checkAtmospheric(kind);
        out.findings.insert(out.findings.end(), std::make_move_iterator(one.findings.begin()),
                            std::make_move_iterator(one.findings.end()));
    }
    return out;
}

} // namespace avgen::world::conformance
