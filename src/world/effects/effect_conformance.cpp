#include "world/effects/effect_conformance.hpp"

#include "core/wind.hpp"

#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace avgen::world::conformance {
namespace {

constexpr std::string_view kProbeName = "conformance probe";

// Whether two resolved frames differ in anything that reaches the GPU.
//
// **Not `memcmp` over the whole struct, and that is a finding rather than a style choice.** The
// first version of check 6 did exactly that, and reported all three kinds as failing the
// "influence 0 is exactly off" half. The difference was two bytes at offsets 10 and 11 -- the
// PADDING between `AtmosphericFrame::hasVortex` and `AtmosphericFrame::vortex`. Value-initialising
// the frame zeroes its padding, but member-wise assignment afterwards leaves it indeterminate, and
// the optimiser is entitled to write through it with a wider store; it had, with the low half of a
// float. Comparing indeterminate bytes reported a difference that was not one, which is the same
// class of wrong answer as a check that cannot fail, arrived at from the opposite direction.
//
// So this compares the payload blocks -- each of which is an array of `vec4`s or of `float`s with
// no padding in it, pinned by the `static_assert`s beside their declarations -- and the scalars,
// one at a time. A member added to `AtmosphericFrame` and not added here weakens the check, which
// is why the `sizeof` assertion below is here: it does not name the new member, but it does stop
// the frame growing silently past what this function reads.
[[nodiscard]] bool frameDiffers(const AtmosphericFrame& a, const AtmosphericFrame& b) {
    // 1612 -> 1640 when Vortex 2.0 added seven floats to `Vortex` (§7-§11's macro structure).
    // Checked, which is what this assertion is for: the `memcmp` at the bottom covered the WHOLE of
    // `Vortex`, so the new members were read with no edit here -- the assertion fired, the question
    // was asked, and the answer was yes.
    //
    // 1640 -> 2400 when ADR-390's Cosmic Ocean arrived, and that time the answer was **no**: the
    // frame had gained `hasCosmicOcean`, `cosmicOcean` and `cosmicOceanEnvelope` and this function
    // read none of the three. The assertion earned its keep twice in one day.
    //
    // 2400 -> 1640 when the Cosmic Ocean was removed (ADR-441), the same question in the other
    // direction and the easy one: a member this function read is gone and what is left is what it
    // read before. A SHRINKING frame is the case an assertion on `sizeof` catches and a checklist
    // does not.
    //
    // 1640 -> 2564 for ADR-562's medium slots, and this time the answer changes the FUNCTION rather
    // than the list. The frame no longer carries an authored `Vortex` behind a `bool`; it carries
    // `MediumSlot media[kMaxMedia]` plus two counts, and a slot is an array of `vec4` with its
    // padding declared. So:
    //
    //   * there is no member list here to fall behind any more. The old body named `vortex` and
    //     would have gone on compiling, and silently comparing nothing, if a second medium had been
    //     added beside it -- which is exactly the shape of the defect ADR-390's arrival produced.
    //   * the padding question is gone at the source. This function's preamble is about two
    //     indeterminate bytes between `hasVortex` and `vortex` that a `memcmp` read and reported as
    //     a difference that was not one. `MediumSlot` has an explicit `pad[3]`, so a whole-struct
    //     compare is total and correct by construction rather than by care.
    //
    // **That is why the tripwire can finally relax.** It still pins the size, because a frame that
    // grows a member OUTSIDE the slots is still a question worth being asked. It no longer guards a
    // hand-maintained list, because there is no longer a hand-maintained list to guard.
    static_assert(sizeof(AtmosphericFrame) == 2564,
                  "AtmosphericFrame changed size: check that frameDiffers still reads all of it");
    if (a.cometCount != b.cometCount || a.auroraCount != b.auroraCount ||
        a.cometSteps != b.cometSteps || a.mediumCount != b.mediumCount ||
        a.mediaDropped != b.mediaDropped) {
        return true;
    }
    if (std::memcmp(&a.comets, &b.comets, sizeof(a.comets)) != 0) { return true; }
    if (std::memcmp(&a.auroras, &b.auroras, sizeof(a.auroras)) != 0) { return true; }
    if (std::memcmp(&a.ground, &b.ground, sizeof(a.ground)) != 0) { return true; }
    // Lanes are `vec4` and the only sub-word member is padded explicitly, so the whole array
    // compares with no indeterminate byte in it -- which the preamble above is the story of.
    static_assert(sizeof(MediumSlot) == sizeof(glm::vec4) * kMediumLanes + 16);
    return std::memcmp(&a.media, &b.media, sizeof(a.media)) != 0;
}


void report(Report& out, EffectKind kind, std::string_view rule, std::string detail) {
    out.findings.push_back(Finding{effectKindName(kind), std::string(rule), std::move(detail)});
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
    // ADR-566: an Int parameter's representable states are the integers, so a fraction of its
    // range is a value it cannot hold -- and the check below would then compare the fraction that
    // was requested against the integer that was stored and call a working round trip lost. Worse
    // in the other direction: for a two-state Int, a fraction that rounds back to the default
    // makes this probe VACUOUS, because it would then be asserting that the default survives.
    // So quantise here and search the integers for one that differs.
    const bool integral = p.kind() == params::ParamKind::Int;
    // Seven fractions, so consecutive fields differ and the cycle does not align with any
    // plausible field ordering.
    static constexpr float kFractions[] = {0.31f, 0.67f, 0.44f, 0.82f, 0.23f, 0.58f, 0.71f};
    for (std::size_t attempt = 0; attempt < std::size(kFractions); ++attempt) {
        const float t = kFractions[(salt + attempt) % std::size(kFractions)];
        float v = std::clamp(slo + t * (shi - slo), lo, hi);
        if (integral) {
            v = std::round(v);
        }
        if (std::abs(v - def) > 1e-4f * std::max(1.0f, std::abs(def))) {
            return v;
        }
    }
    if (integral) {
        // Every fraction rounded back to the default: walk the integers instead. A two-choice row
        // reaches here every time.
        for (float v = std::round(lo); v <= std::round(hi); v += 1.0f) {
            if (std::abs(v - def) > 1e-4f * std::max(1.0f, std::abs(def))) {
                return v;
            }
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

// ADR-703. The world an entity-owned probe is attached to: every node exists, stands at the origin
// with a 2 m box and one scene entity, and is moving at 5 m/s along +X -- so a type that asks its
// owner for a view, bounds or a velocity gets an answer, and "resolves into its own bucket" is a
// question about the type rather than about a scene the probe does not have.
class ProbeScene final : public EffectSceneQuery {
public:
    [[nodiscard]] bool nodePosition(std::string_view, glm::vec3& out) const override {
        out = glm::vec3(0.0f);
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view, NodeView& out) const override {
        out = NodeView{};
        out.boundsMin = glm::vec3(-1.0f);
        out.boundsMax = glm::vec3(1.0f);
        out.hasBounds = true;
        out.firstEntity = 0;
        out.entityCount = 1;
        return true;
    }
    [[nodiscard]] bool nodeVelocity(std::string_view, glm::vec3& out) const override {
        out = glm::vec3(5.0f, 0.0f, 0.0f);
        return true;
    }
};

EffectInstance probeEffect(EffectKind kind, std::string name) {
    // ADR-500: the registry's factory, which is the same one the "Add" button calls. A switch here
    // would be one more per-kind list that a new kind has to be remembered into -- and this file's
    // whole job is to make forgetting loud, not to have something to forget.
    EffectInstance e = makeEffect(kind, std::move(name));
    // ADR-702: an instance needs an id and an owner its type supports. The World when the type takes
    // one, else the first target it does, named -- and a source that means "my owner" is pinned to
    // a world position, so every check below can resolve the probe without a scene.
    e.id = "conformance-probe";
    if (const EffectSchema* schema = effectSchema(kind)) {
        if ((schema->targets & targetBit(EffectTarget::World)) != 0) {
            e.owner = EffectOwner::world();
        } else {
            for (std::size_t t = 0; t < kEffectTargetCount; ++t) {
                if ((schema->targets & targetBit(static_cast<EffectTarget>(t))) != 0) {
                    e.owner = EffectOwner{static_cast<EffectTarget>(t), "probe-owner"};
                    break;
                }
            }
        }
        if (schema->getSource != nullptr && schema->setSource != nullptr) {
            EffectEndpoint src = schema->getSource(e);
            if (src.kind == SourceKind::Owner || src.kind == SourceKind::FocusHero) {
                src.kind = SourceKind::World;
                src.position = glm::vec3(0.0f);
                schema->setSource(e, src);
            }
        }
    }
    return e;
}

std::vector<std::string> registeredPaths(const EffectInstance& effect) {
    params::ParameterSet params;
    const EffectInstance one = effect;
    EffectParameters registered = registerEffectParameters(params, std::span(&one, 1));
    // `registered.registered` is the registrar's own list of every path it wrote -- the same list
    // `unregisterEffectParameters` uses -- so this is what the engine believes it created,
    // not what a table says it should have.
    return registered.registered;
}

std::vector<std::string> registeredLeaves(const EffectInstance& effect) {
    const std::string prefix = effectParameterPrefix(effect.id);
    std::vector<std::string> leaves;
    for (const std::string& path : registeredPaths(effect)) {
        leaves.push_back(path.starts_with(prefix) ? path.substr(prefix.size()) : path);
    }
    return leaves;
}

Report checkLeavesExist(EffectKind kind, std::span<const std::string_view> leaves, std::string_view rule) {
    Report out;
    const EffectInstance probe = probeEffect(kind, std::string(kProbeName));
    params::ParameterSet params;
    registerEffectParameters(params, std::span(&probe, 1));
    const std::string prefix = effectParameterPrefix(probe.id);
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

Report checkAtmospheric(EffectKind kind) {
    Report out;
    const EffectInstance probe = probeEffect(kind, std::string(kProbeName));

    // The probe has to be of the kind asked for, or every check below is about something else.
    if (probe.kind != kind) {
        report(out, kind, "probe-kind",
               std::string("probeEffect returned a ") + effectKindName(probe.kind));
        return out;
    }

    // 5. The name survives the file format. A kind whose name does not parse back is a scene that
    //    silently loads as a comet.
    {
        const auto back = effectKindFromName(effectKindName(kind));
        if (!back.has_value() || *back != kind) {
            report(out, kind, "kind-name",
                   std::string("effectKindFromName(\"") + effectKindName(kind) + "\") does not "
                   "return this kind -- effectKindFromName is an if-chain, not a switch");
        }
    }

    params::ParameterSet params;
    std::vector<EffectInstance> effects{probe};
    EffectParameters registered = registerEffectParameters(params, effects);
    const std::string prefix = effectParameterPrefix(probe.id);

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
        const std::vector<params::ModRoute> routes = defaultEffectRoutes(probe.id, kind);
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
        captureEffectParameters(registered, effects);

        const nlohmann::json first = effects.front().toJson();
        const auto reloaded = EffectInstance::fromJson(first);
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
            const EffectInstance loaded = *reloaded;
            EffectParameters registeredAfter = registerEffectParameters(after, std::span(&loaded, 1));
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
            unregisterEffectParameters(after, registeredAfter);
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
        EffectInstance live = probe;
        live.enabled = true;
        live.activation = Activation::Always;
        live.timing = Timing{};
        live.timing.fadeIn = 0.0;
        live.timing.fadeOut = 0.0;

        EffectContext ctx;
        ctx.seconds = 0.5;
        const ProbeScene probeScene;
        ctx.scene = &probeScene;
        std::array<ResolvedAtmospheric, kMaxGpuComets> comets{};
        std::array<ResolvedAtmospheric, kMaxGpuAuroras> auroras{};
        const AtmosphericCounts counts =
            resolveAtmosphericEffects(std::span(&live, 1), ctx, comets, auroras);
        // ADR-702: the surface waves are the one bucket `resolveAtmosphericEffects` does not own.
        std::array<ResolvedWave, kMaxGpuWaves> waves{};
        const std::size_t waveCount = resolveWaves(std::span(&live, 1), ctx, waves);

        // ADR-500: the counter this kind's records land in comes from its own declaration, and
        // the switch is over `EffectBucket` -- exhaustive, no `default`, one arm per GPU payload
        // the engine has. That is what keeps this check able to fail for a kind that does not
        // exist yet: it asks the schema where this kind SAYS it goes and then checks that it went
        // there, rather than picking a counter with a chain whose own `else` agrees with whatever
        // `resolveAtmosphericEffects` did.
        const EffectSchema* schema = effectSchema(kind);
        std::size_t mine = 0;
        if (schema == nullptr) {
            report(out, kind, "resolve-dispatch", "this kind has no schema, so nothing resolves it");
        } else {
            switch (schema->resolve.bucket) {
            case EffectBucket::Comet: mine = counts.comets; break;
            case EffectBucket::Aurora: mine = counts.auroras; break;
            case EffectBucket::Medium: mine = counts.vortices; break;
            case EffectBucket::Surface: mine = waveCount; break;
            // ADR-703: a bucket with a builder of its own answers through its `records` hook -- the
            // same code its builder runs -- and puts nothing in the four buckets counted above.
            case EffectBucket::EntityLanes:
            case EffectBucket::Ribbon:
            case EffectBucket::Distortion:
            case EffectBucket::Emitter:
            case EffectBucket::Transform:
            case EffectBucket::Starfield:
                mine = schema->resolve.records != nullptr ? schema->resolve.records(live, ctx) : 0;
                break;
            }
        }
        const std::size_t own =
            schema != nullptr && !isAtmosphericBucket(schema->resolve.bucket) &&
                    schema->resolve.bucket != EffectBucket::Surface
                ? mine
                : 0;
        const std::size_t total = counts.comets + counts.auroras + counts.vortices + waveCount + own;
        // How many records one live effect of this kind is entitled to. One for everything ADR-230
        // shipped; a meteor shower declares more, and asserting "exactly one" would have made this
        // check fail on correct code -- which is the failure ADR-182 is about from the other side.
        const std::size_t expected =
            (schema != nullptr && schema->resolve.count != nullptr)
                ? std::min<std::size_t>(schema->resolve.count(live), kMaxGpuComets)
                : 1;
        if (mine == 0) {
            report(out, kind, "resolve-dispatch",
                   "one live effect of this kind resolved as " + std::to_string(counts.comets) +
                       " comet(s), " + std::to_string(counts.auroras) + " aurora(s), " +
                       std::to_string(counts.vortices) + " vortex/vortices, " +
                       std::to_string(counts.dropped) + " dropped -- nothing put a record in the "
                       "bucket its own schema names");
        } else if (total != mine) {
            report(out, kind, "resolve-dispatch",
                   "one live effect of this kind put records in more than one bucket: " +
                       std::to_string(mine) + " in its own and " + std::to_string(total) + " in all");
        } else if (mine > expected) {
            report(out, kind, "resolve-dispatch",
                   "one live effect resolved as " + std::to_string(mine) + " records, more than the " +
                       std::to_string(expected) + " its own schema declares");
        }
    }

    // 6. §68. The kind's subscription reaches its picture.
    //
    //    A shared table row buys registration, apply, capture, a panel row and a save entry for
    //    every kind at once -- and buys NOTHING about whether the resolver for a given kind does
    //    anything with the number. `flowInfluence` could be registered, modulated, keyed, saved and
    //    reloaded for an aurora while `packAurora` never read it, and every other check in this
    //    file would pass. That is ADR-387's "a correct value is not a reached value" in the one
    //    shape the rest of this file cannot see.
    //
    //    So the check is a difference, not an inspection: build the frame this kind produces with
    //    no subscription, build it again with a subscription to a field that is genuinely blowing,
    //    and require the two to differ. It is kind-agnostic by construction -- it compares whole
    //    `AtmosphericFrame`s -- so a fourth kind is covered on the day it is added, and covered
    //    correctly, which a per-kind lane comparison would not be.
    //
    //    ADR-182: it is shown able to fail. With `packAurora`'s two uses of the flow removed, it
    //    reports the aurora by name; with the whole of `resolveEffectFlow` stubbed to return {},
    //    it reports all three.
    // ADR-702: only for a type the evaluator samples a field for. A surface wave registers no
    // `flowInfluence` (see `sharedFieldApplies`), so there is no row whose reach to check.
    const bool subscribes = [&] {
        const EffectSchema* schema = effectSchema(kind);
        if (schema == nullptr) {
            return false;
        }
        for (const EffectField& f : sharedEffectFields()) {
            if (std::string_view(f.leaf) == "flowInfluence") {
                return sharedFieldApplies(*schema, f);
            }
        }
        return false;
    }();
    if (subscribes) {
        EffectInstance live = probe;
        live.enabled = true;
        live.activation = Activation::Always;
        live.timing = Timing{};

        // A wind strong enough that `strength` cannot be near zero anywhere. ADR-055's regional
        // term is `max(1 + amount*0.5*(r1+r2), 0)` with each r in [-1, 1], so at amount 0.45 the
        // strength is at least 0.55 of the speed at every point in the world -- which is what makes
        // this probe incapable of passing by accident at a quiet spot.
        wind::WindParams gale;
        gale.enabled = true;
        gale.speed = 3.0f;
        gale.regionAmount = 0.45f;
        gale.gustAmount = 0.9f;
        fields::FieldBus bus;
        bus.publishWind(std::string(fields::kWindField), wind::packWind(gale));

        EffectContext ctx;
        ctx.seconds = 0.5;
        ctx.cameraPosition = glm::vec3(120.0f, 40.0f, -260.0f); // off the origin, so `phase` is not 0
        ctx.fieldBus = &bus;

        AtmosphericFrame still{};
        live.flow = fields::Subscription{};
        buildAtmosphericFrame(std::span(&live, 1), ctx, still);

        AtmosphericFrame blown{};
        live.flow.field = std::string(fields::kWindField);
        live.flow.influence = 1.0f;
        buildAtmosphericFrame(std::span(&live, 1), ctx, blown);

        // A comparison of the payload blocks, not of the struct -- see `frameDiffers`. And a
        // comparison rather than `REQUIRE(a == b)` over the buffers themselves, because printing a
        // kilobyte of vec4s on failure is ADR-362's killed run.
        if (!frameDiffers(still, blown)) {
            report(out, kind, "flow-reaches",
                   "subscribing this kind to a field that is blowing hard changes nothing in the "
                   "frame it builds -- `flowInfluence` is registered and saved for it but no "
                   "resolver or packer reads it, so the row is a setting the picture does not keep");
        }

        // And the other end, which is the half that makes the first one mean something: with the
        // subscription present but the influence at 0, the frame must be EXACTLY the unsubscribed
        // one. A kind that responded to the mere presence of a name would pass the check above
        // while making every scene that names a field change the moment it loaded.
        AtmosphericFrame named{};
        live.flow.influence = 0.0f;
        buildAtmosphericFrame(std::span(&live, 1), ctx, named);
        if (frameDiffers(still, named)) {
            report(out, kind, "flow-zero-is-off",
                   "naming a field with influence 0 changed the frame -- 0 must be exactly off, or "
                   "every scene that records a subscription it is not using renders differently");
        }
    }

    return out;
}

Report checkAtmosphericFamily() {
    Report out;
    for (const EffectKind kind : kEffectKinds) {
        Report one = checkAtmospheric(kind);
        out.findings.insert(out.findings.end(), std::make_move_iterator(one.findings.begin()),
                            std::make_move_iterator(one.findings.end()));
    }
    return out;
}

} // namespace avgen::world::conformance
