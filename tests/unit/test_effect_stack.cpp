// The effect stack (ADR-702): one list of effect instances, each attached to an owner.
//
// Before ADR-702 a scene had two effect lists and neither knew what its effects belonged to. Now
// "the World's effects" and "rook's effects" are two filtered views of ONE list, every instance has
// a stable id that is half of its parameter paths (`fx/<id>/<leaf>`), and every instance reports
// what happened to it on the last frame. The questions here are the ones that design has to answer
// without a GPU and without a panel:
//
//   * how many effects may an owner have, and does one owner's stack touch another's?
//   * does the World carry several effects of different techniques at once?
//   * are two entities' pulses two independent things -- parameters, enable, modulation?
//   * is an id stable across rename, reorder and duplicate, and unique when minted?
//   * is the stored order deterministic, and is normalising it idempotent?
//   * what does the whole-list validator refuse, and what does it accept?
//   * in what order does the evaluator walk the list?
//   * what does each instance say about itself on a frame -- drawn, dropped, disabled, orphaned?
//
// Every guard below that protects an invariant has a control that shows it can come out the other
// way (ADR-182), and each was additionally broken in src/ and seen red before it was trusted.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/hero.hpp"
#include "world/wave_effect.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
using world::EffectKind;
using world::EffectOwner;
using world::EffectStatus;
using world::EffectTarget;

namespace {

const EffectOwner kWorld = EffectOwner::world();

std::string add(std::vector<world::EffectInstance>& effects, const EffectOwner& owner, EffectKind kind) {
    auto id = world::addEffect(effects, owner, kind);
    INFO((id ? std::string() : id.error().message));
    REQUIRE(id.has_value());
    return *id;
}

world::EffectInstance& byId(std::vector<world::EffectInstance>& effects, std::string_view id) {
    const std::size_t at = world::findEffect(effects, id);
    REQUIRE(at < effects.size());
    return effects[at];
}

std::vector<std::string> idsOf(const std::vector<world::EffectInstance>& effects,
                               const std::vector<std::size_t>& indices) {
    std::vector<std::string> out;
    for (const std::size_t i : indices) {
        out.push_back(effects[i].id);
    }
    return out;
}

// Every path the registrar writes for this list, sorted.
std::vector<std::string> registeredPaths(const std::vector<world::EffectInstance>& effects) {
    params::ParameterSet set;
    world::EffectParameters reg = world::registerEffectParameters(set, effects);
    std::vector<std::string> paths = reg.registered;
    std::sort(paths.begin(), paths.end());
    return paths;
}

// A surface wave that is live from t = 0 wherever it is: a World source, always active, no fade.
world::EffectInstance livePulse(const std::string& id) {
    world::EffectInstance e = world::makeEffect(EffectKind::GroundPulse, id);
    e.id = id;
    e.wave.source.kind = world::SourceKind::World;
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.timing.repeatSeconds = 2.0;
    return e;
}

// Any type, made live from t = 0: always active, no fade, no delay.
world::EffectInstance alwaysOn(EffectKind kind, const std::string& id) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = id;
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

world::EffectContext contextAt(double seconds) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.cameraPosition = glm::vec3(0.0f, 20.0f, 60.0f);
    return ctx;
}

// Every stage's builder over one list, with one status table -- the shape of `Engine::updateEffects`.
std::vector<EffectStatus> evaluate(const std::vector<world::EffectInstance>& effects, double seconds,
                                   world::WaveFrame& waves, world::AtmosphericFrame& sky) {
    std::vector<std::uint32_t> order;
    world::effectEvaluationOrder(effects, order);
    std::vector<EffectStatus> status(effects.size(), EffectStatus::Dormant);
    const world::EffectContext ctx = contextAt(seconds);
    world::buildWaveFrame(effects, ctx, waves, order, status);
    world::buildAtmosphericFrame(effects, ctx, sky, order, status);
    return status;
}

std::vector<EffectStatus> evaluate(const std::vector<world::EffectInstance>& effects, double seconds) {
    world::WaveFrame waves{};
    world::AtmosphericFrame sky{};
    return evaluate(effects, seconds, waves, sky);
}

} // namespace

// ---- owners and their stacks ---------------------------------------------------------------------

TEST_CASE("an owner may have no effects, one, or several, and each stack is its own",
          "[effects][stack][owner]") {
    std::vector<world::EffectInstance> effects;
    const EffectOwner rook = EffectOwner::entity("rook");
    const EffectOwner sage = EffectOwner::entity("sage");

    // Zero: an owner nobody attached anything to has an empty stack, not an error.
    CHECK(world::effectsOf(effects, kWorld).empty());
    CHECK(world::effectsOf(effects, rook).empty());

    // One.
    const std::string rookPulse = add(effects, rook, EffectKind::GroundPulse);
    REQUIRE(world::effectsOf(effects, rook).size() == 1);
    CHECK(world::effectsOf(effects, sage).empty());
    CHECK(byId(effects, rookPulse).owner == rook);
    CHECK(byId(effects, rookPulse).order == 0);

    // Several on one owner, and several owners. The list is one; the stacks are views of it.
    const std::string rookPulse2 = add(effects, rook, EffectKind::GroundPulse);
    const std::string sagePulse = add(effects, sage, EffectKind::GroundPulse);
    const std::string aurora = add(effects, kWorld, EffectKind::Aurora);
    const std::string beam = add(effects, kWorld, EffectKind::TravelBeam);
    REQUIRE(effects.size() == 5);
    CHECK(idsOf(effects, world::effectsOf(effects, rook)) == std::vector<std::string>{rookPulse, rookPulse2});
    CHECK(idsOf(effects, world::effectsOf(effects, sage)) == std::vector<std::string>{sagePulse});
    CHECK(idsOf(effects, world::effectsOf(effects, kWorld)) == std::vector<std::string>{aurora, beam});
    // New effects go to the BOTTOM of their own stack and nowhere else.
    CHECK(byId(effects, rookPulse2).order == 1);
    CHECK(byId(effects, beam).order == 1);
    CHECK(byId(effects, sagePulse).order == 0);
    REQUIRE(world::validateEffects(effects).has_value());

    SECTION("moving within one stack never moves an effect of another") {
        const auto sageBefore = idsOf(effects, world::effectsOf(effects, sage));
        const auto worldBefore = idsOf(effects, world::effectsOf(effects, kWorld));
        REQUIRE(world::moveEffect(effects, rookPulse2, -1));
        CHECK(idsOf(effects, world::effectsOf(effects, rook)) == std::vector<std::string>{rookPulse2, rookPulse});
        CHECK(idsOf(effects, world::effectsOf(effects, sage)) == sageBefore);
        CHECK(idsOf(effects, world::effectsOf(effects, kWorld)) == worldBefore);
        // Clamped at the top: nothing moves, and it says so.
        CHECK_FALSE(world::moveEffect(effects, rookPulse2, -1));
        CHECK(world::validateEffects(effects).has_value());
    }
    SECTION("removing one closes the gap in its own stack only") {
        REQUIRE(world::removeEffect(effects, rookPulse));
        CHECK(byId(effects, rookPulse2).order == 0);
        CHECK(byId(effects, beam).order == 1);
        CHECK_FALSE(world::removeEffect(effects, rookPulse)); // already gone
        CHECK(world::validateEffects(effects).has_value());
    }
    SECTION("deleting an entity takes its effects and no one else's") {
        CHECK(world::removeEffectsOf(effects, rook) == 2);
        CHECK(world::effectsOf(effects, rook).empty());
        CHECK(effects.size() == 3);
        CHECK(world::validateEffects(effects).has_value());
    }
}

TEST_CASE("the World carries a tornado, an aurora, a fog bank and a comet at once",
          "[effects][stack][world]") {
    // Four types, three techniques (sky curtains, a sky trail, two placed media), one owner. Each
    // must draw -- and "draw" is read off each instance's own status, not off a frame total that
    // could be made up by any three of them.
    std::vector<world::EffectInstance> effects{
        alwaysOn(EffectKind::Tornado, "tornado"),
        alwaysOn(EffectKind::Aurora, "aurora"),
        alwaysOn(EffectKind::VolumetricFog, "fog"),
        alwaysOn(EffectKind::Comet, "comet"),
    };
    world::normaliseEffectOrder(effects);
    REQUIRE(world::validateEffects(effects).has_value());
    REQUIRE(world::effectsOf(effects, kWorld).size() == 4);

    world::WaveFrame waves{};
    world::AtmosphericFrame sky{};
    const std::vector<EffectStatus> status = evaluate(effects, 1.0, waves, sky);
    for (std::size_t i = 0; i < effects.size(); ++i) {
        INFO(effects[i].id << " is " << world::effectStatusName(status[i]));
        CHECK(status[i] == EffectStatus::Drawn);
    }
    CHECK(sky.mediumCount == 2);
    CHECK(sky.auroraCount == 1);
    CHECK(sky.cometCount == 1);
    CHECK(sky.mediaDropped == 0);
    CHECK(waves.count == 0);

    SECTION("disabling one leaves the other three drawing") {
        byId(effects, "fog").enabled = false;
        const std::vector<EffectStatus> after = evaluate(effects, 1.0, waves, sky);
        CHECK(after[world::findEffect(effects, "fog")] == EffectStatus::Disabled);
        CHECK(after[world::findEffect(effects, "tornado")] == EffectStatus::Drawn);
        CHECK(after[world::findEffect(effects, "aurora")] == EffectStatus::Drawn);
        CHECK(after[world::findEffect(effects, "comet")] == EffectStatus::Drawn);
        CHECK(sky.mediumCount == 1);
    }
}

TEST_CASE("an effect's type decides which owners it may be attached to",
          "[effects][stack][owner][targets]") {
    std::vector<world::EffectInstance> effects;
    // A tornado stands in the world; it is not a thing a character carries.
    const auto refused = world::addEffect(effects, EffectOwner::entity("rook"), EffectKind::Tornado);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("Tornado") != std::string::npos);
    CHECK(effects.empty()); // refused with the list untouched
    // A malformed owner is refused too.
    CHECK_FALSE(world::addEffect(effects, EffectOwner{EffectTarget::World, "not a world"}, EffectKind::Aurora));
    CHECK_FALSE(world::addEffect(effects, EffectOwner{EffectTarget::Entity, ""}, EffectKind::GroundPulse));
    CHECK(effects.empty());

    // The control: the same calls with owners the types support succeed.
    CHECK(world::addEffect(effects, kWorld, EffectKind::Tornado));
    CHECK(world::addEffect(effects, EffectOwner::entity("rook"), EffectKind::GroundPulse));
    CHECK(world::addEffect(effects, EffectOwner::camera(), EffectKind::TravelBeam));
    CHECK(effects.size() == 3);

    // What the Add Effect menu offers an entity is exactly what may be attached to one.
    for (const EffectKind kind : world::effectKindsFor(EffectTarget::Entity)) {
        CHECK(world::effectAllowedOn(kind, EffectTarget::Entity));
    }
    const std::vector<EffectKind> onEntity = world::effectKindsFor(EffectTarget::Entity);
    CHECK(std::find(onEntity.begin(), onEntity.end(), EffectKind::GroundPulse) != onEntity.end());
    CHECK(std::find(onEntity.begin(), onEntity.end(), EffectKind::Tornado) == onEntity.end());
}

TEST_CASE("an entity-owned pulse rides its owner; on the World it follows the cut",
          "[effects][stack][owner]") {
    std::vector<world::EffectInstance> effects;
    const std::string onRook = add(effects, EffectOwner::entity("rook"), EffectKind::GroundPulse);
    const std::string onWorld = add(effects, kWorld, EffectKind::GroundPulse);
    CHECK(byId(effects, onRook).wave.source.kind == world::SourceKind::Owner);
    // The World has no position to lend, so the stack fits the pulse to it (`adaptEffectToOwner`).
    CHECK(byId(effects, onWorld).wave.source.kind == world::SourceKind::FocusHero);
    CHECK(world::validateEffects(effects).has_value());
}

// ---- two entities, two pulses ------------------------------------------------------------------

TEST_CASE("two entities' Ground Pulses are two independent effects",
          "[effects][stack][owner][params]") {
    std::vector<world::EffectInstance> effects;
    const std::string rook = add(effects, EffectOwner::entity("rook"), EffectKind::GroundPulse);
    const std::string sage = add(effects, EffectOwner::entity("sage"), EffectKind::GroundPulse);
    REQUIRE(rook != sage);
    CHECK(rook == "rook-ground-pulse");
    CHECK(sage == "sage-ground-pulse");

    params::ParameterSet set;
    world::EffectParameters registered = world::registerEffectParameters(set, effects);
    const std::string rookIntensity = world::effectParameterPrefix(rook) + "intensity";
    const std::string sageIntensity = world::effectParameterPrefix(sage) + "intensity";
    REQUIRE(set.find(rookIntensity) != nullptr);
    REQUIRE(set.find(sageIntensity) != nullptr);
    const float rookBefore = byId(effects, rook).wave.appearance.intensity;
    const float sageBefore = byId(effects, sage).wave.appearance.intensity;

    SECTION("a parameter of one is not a parameter of the other") {
        set.find(rookIntensity)->setFinalComponent(0, 9.0f);
        set.find(sageIntensity)->setFinalComponent(0, sageBefore);
        world::applyEffectParameters(registered, effects);
        CHECK(byId(effects, rook).wave.appearance.intensity == 9.0f);
        CHECK(byId(effects, sage).wave.appearance.intensity == sageBefore);
    }

    SECTION("disabling one does not disable the other") {
        set.find(world::effectParameterPrefix(rook) + "enabled")->setFinalComponent(0, 0.0f);
        world::applyEffectParameters(registered, effects);
        CHECK_FALSE(byId(effects, rook).enabled);
        CHECK(byId(effects, sage).enabled);
    }

    SECTION("a modulation route onto one changes only that one") {
        signals::SignalBus bus;
        const signals::SignalId beat = bus.declare("beat.pulse", 0.0f, 1.0f, true);
        params::Modulator modulator;
        params::ModRoute route;
        route.source = "beat.pulse";
        route.target = rookIntensity;
        route.amount = 3.0f;
        modulator.addRoute(route);
        REQUIRE(modulator.bind(bus, set));
        set.resetFinals();
        bus.setEvent(beat, true, 1.0f);
        modulator.applyRoutes(bus, set, 1.0 / 60.0);
        world::applyEffectParameters(registered, effects);
        // The control: the route reached its target, so "the other did not move" is not a route
        // that reached nothing.
        CHECK(byId(effects, rook).wave.appearance.intensity > rookBefore);
        CHECK(byId(effects, sage).wave.appearance.intensity == sageBefore);
    }
}

// ---- identity ------------------------------------------------------------------------------------

TEST_CASE("an id is stable: renaming, reordering and re-owning change no parameter path",
          "[effects][stack][identity]") {
    std::vector<world::EffectInstance> effects;
    const std::string aurora = add(effects, kWorld, EffectKind::Aurora);
    const std::string comet = add(effects, kWorld, EffectKind::Comet);
    const std::string pulse = add(effects, EffectOwner::entity("rook"), EffectKind::GroundPulse);
    const std::vector<std::string> before = registeredPaths(effects);
    REQUIRE_FALSE(before.empty());
    // The control: the paths are keyed by id, so a changed id WOULD show here.
    CHECK(std::find(before.begin(), before.end(), "fx/" + aurora + "/intensity") != before.end());

    SECTION("renaming changes the display name only") {
        byId(effects, aurora).name = "Northern Lights";
        byId(effects, pulse).name = "Rook's Ring";
        CHECK(byId(effects, aurora).id == aurora);
        CHECK(registeredPaths(effects) == before);
    }
    SECTION("reordering a stack changes no path") {
        REQUIRE(world::moveEffect(effects, comet, -1));
        REQUIRE(world::effectsOf(effects, kWorld).front() == world::findEffect(effects, comet));
        CHECK(registeredPaths(effects) == before);
    }
    SECTION("an entity renamed keeps its effects and their ids") {
        CHECK(world::renameEffectOwner(effects, EffectOwner::entity("rook"), EffectOwner::entity("castle")) == 1);
        CHECK(byId(effects, pulse).owner == EffectOwner::entity("castle"));
        CHECK(registeredPaths(effects) == before);
    }
}

TEST_CASE("add and duplicate mint ids no effect already has", "[effects][stack][identity]") {
    std::vector<world::EffectInstance> effects;
    std::set<std::string> ids;
    for (int i = 0; i < 3; ++i) {
        ids.insert(add(effects, kWorld, EffectKind::Aurora));
        ids.insert(add(effects, EffectOwner::entity("rook"), EffectKind::GroundPulse));
    }
    CHECK(ids.size() == 6);
    CHECK(ids.count("aurora") == 1);
    CHECK(ids.count("aurora-2") == 1);
    CHECK(ids.count("rook-ground-pulse-3") == 1);

    const auto copy = world::duplicateEffect(effects, "aurora");
    REQUIRE(copy.has_value());
    CHECK(ids.count(*copy) == 0);
    CHECK(copy->find('/') == std::string::npos);
    const world::EffectInstance& dup = byId(effects, *copy);
    CHECK(dup.name == byId(effects, "aurora").name + " copy");
    CHECK(dup.kind == EffectKind::Aurora);
    // Directly below its original, in the same stack.
    CHECK(dup.order == byId(effects, "aurora").order + 1);
    CHECK(dup.owner == kWorld);
    CHECK(world::validateEffects(effects).has_value());
    CHECK_FALSE(world::duplicateEffect(effects, "no-such-effect"));

    SECTION("an inserted effect whose id is taken or malformed is given a fresh one") {
        world::EffectInstance paste = byId(effects, "aurora");
        const auto pasted = world::insertEffect(effects, paste);
        REQUIRE(pasted.has_value());
        CHECK(*pasted != "aurora");
        world::EffectInstance slashed = byId(effects, "aurora");
        slashed.id = "a/b";
        const auto fixed = world::insertEffect(effects, slashed);
        REQUIRE(fixed.has_value());
        CHECK(fixed->find('/') == std::string::npos);
        CHECK(world::validateEffects(effects).has_value());
    }
    SECTION("an id is a slug of what it is made from, with no '/' in it") {
        CHECK(world::uniqueEffectId({}, "Rook / Pulse") == "rook-pulse");
        CHECK(world::uniqueEffectId({}, "") == "effect");
    }
}

// ---- order -----------------------------------------------------------------------------------------

TEST_CASE("the stored order is deterministic and normalising it is idempotent",
          "[effects][stack][order]") {
    // Built in a scrambled order: owners interleaved, orders out of sequence and with gaps.
    std::vector<world::EffectInstance> effects;
    const auto make = [](const std::string& id, EffectOwner owner, int order) {
        world::EffectInstance e = world::makeEffect(
            owner.isWorld() ? EffectKind::Aurora : EffectKind::GroundPulse, id);
        e.id = id;
        e.owner = std::move(owner);
        e.order = order;
        world::adaptEffectToOwner(e);
        return e;
    };
    effects.push_back(make("w-b", kWorld, 7));
    effects.push_back(make("r-a", EffectOwner::entity("rook"), 3));
    effects.push_back(make("w-a", kWorld, 2));
    effects.push_back(make("s-a", EffectOwner::entity("sage"), 0));
    effects.push_back(make("r-b", EffectOwner::entity("rook"), 9));
    effects.push_back(make("w-c", kWorld, 7)); // a tie with w-b: list position breaks it
    // Not a valid stack yet: the orders say so.
    CHECK_FALSE(world::validateEffects(effects).has_value());

    world::normaliseEffectOrder(effects);
    std::vector<std::string> once;
    for (const auto& e : effects) {
        once.push_back(e.id + "@" + std::to_string(e.order));
    }
    // Grouped by owner in first-appearance order, sorted by order inside each group, renumbered.
    CHECK(once == std::vector<std::string>{"w-a@0", "w-b@1", "w-c@2", "r-a@0", "r-b@1", "s-a@0"});
    CHECK(world::validateEffects(effects).has_value());

    // Idempotent.
    world::normaliseEffectOrder(effects);
    std::vector<std::string> twice;
    for (const auto& e : effects) {
        twice.push_back(e.id + "@" + std::to_string(e.order));
    }
    CHECK(twice == once);

    // Deterministic: the same input list gives the same output list, every time.
    std::vector<world::EffectInstance> again{make("w-b", kWorld, 7), make("r-a", EffectOwner::entity("rook"), 3),
                                             make("w-a", kWorld, 2), make("s-a", EffectOwner::entity("sage"), 0),
                                             make("r-b", EffectOwner::entity("rook"), 9), make("w-c", kWorld, 7)};
    world::normaliseEffectOrder(again);
    REQUIRE(again.size() == effects.size());
    for (std::size_t i = 0; i < again.size(); ++i) {
        CHECK(again[i].id == effects[i].id);
        CHECK(again[i].order == effects[i].order);
    }
}

TEST_CASE("the evaluator walks by render stage, then priority, then stack position",
          "[effects][stack][order][stage]") {
    // Listed deliberately against the frame's order: a medium first, then a wave, then sky, then a
    // second wave. The walk must put the waves (Material) first in their stack order, then the sky
    // types, then the media.
    std::vector<world::EffectInstance> effects{
        alwaysOn(EffectKind::VolumetricFog, "fog"),
        livePulse("pulse-a"),
        alwaysOn(EffectKind::Aurora, "aurora"),
        alwaysOn(EffectKind::Tornado, "tornado"),
        alwaysOn(EffectKind::Comet, "comet"),
        livePulse("pulse-b"),
    };
    std::vector<std::uint32_t> order;
    world::effectEvaluationOrder(effects, order);
    std::vector<std::string> walked;
    for (const std::uint32_t i : order) {
        walked.push_back(effects[i].id);
    }
    CHECK(walked == std::vector<std::string>{"pulse-a", "pulse-b", "aurora", "comet", "fog", "tornado"});

    // The rule in general, over what the schemas declare -- which covers `priority` the day a type
    // declares a non-zero one (none does yet, so today every tie inside a stage is list position).
    for (std::size_t k = 1; k < order.size(); ++k) {
        const world::EffectSchema* a = world::effectSchema(effects[order[k - 1]].kind);
        const world::EffectSchema* b = world::effectSchema(effects[order[k]].kind);
        const auto key = [](const world::EffectSchema* s) {
            return std::pair<int, int>(static_cast<int>(s->stage), s->priority);
        };
        CHECK(key(a) <= key(b));
        if (key(a) == key(b)) {
            CHECK(order[k - 1] < order[k]); // stack position breaks a tie
        }
    }

    SECTION("the walk decides which wave wins the last GPU slot") {
        // Nine live waves, eight slots: the one walked last is the one dropped, whichever it is.
        std::vector<world::EffectInstance> waves;
        for (int i = 0; i < 9; ++i) {
            waves.push_back(livePulse("w" + std::to_string(i)));
        }
        std::vector<EffectStatus> status = evaluate(waves, 0.5);
        CHECK(status[8] == EffectStatus::Dropped);
        std::rotate(waves.begin(), waves.begin() + 8, waves.end()); // w8 to the top of the list
        status = evaluate(waves, 0.5);
        CHECK(waves[0].id == "w8");
        CHECK(status[0] == EffectStatus::Drawn);
        CHECK(status[8] == EffectStatus::Dropped); // now w7, walked last
    }
}

// ---- validation --------------------------------------------------------------------------------

TEST_CASE("the whole-list check refuses what cannot be one scene's effects, and names it",
          "[effects][stack][validate]") {
    std::vector<world::EffectInstance> good;
    const std::string aurora = add(good, kWorld, EffectKind::Aurora);
    const std::string pulse = add(good, EffectOwner::entity("rook"), EffectKind::GroundPulse);
    const std::string beam = add(good, kWorld, EffectKind::TravelBeam);
    // The control, and one of the spec's points: the World is an owner like any other.
    REQUIRE(world::validateEffects(good).has_value());
    CHECK(byId(good, aurora).owner.isWorld());

    const auto refusedFor = [](const std::vector<world::EffectInstance>& effects, const std::string& word) {
        const auto ok = world::validateEffects(effects);
        REQUIRE_FALSE(ok.has_value());
        INFO(ok.error().message);
        CHECK(ok.error().message.find(word) != std::string::npos);
    };

    SECTION("two effects with one id") {
        std::vector<world::EffectInstance> e = good;
        byId(e, beam).id = aurora;
        refusedFor(e, aurora);
    }
    SECTION("an id containing '/'") {
        std::vector<world::EffectInstance> e = good;
        byId(e, beam).id = "sky/beam";
        refusedFor(e, "sky/beam");
    }
    SECTION("an empty id") {
        std::vector<world::EffectInstance> e = good;
        byId(e, beam).id.clear();
        refusedFor(e, "no id");
    }
    SECTION("an owner the type does not support: a tornado on an entity") {
        std::vector<world::EffectInstance> e = good;
        world::EffectInstance t = alwaysOn(EffectKind::Tornado, "twister");
        t.owner = EffectOwner::entity("rook");
        t.order = 1;
        e.push_back(t);
        world::normaliseEffectOrder(e);
        refusedFor(e, "twister");
    }
    SECTION("a World owner with a name") {
        std::vector<world::EffectInstance> e = good;
        byId(e, aurora).owner.name = "the world";
        refusedFor(e, "World owner");
    }
    SECTION("orders that are not 0..n-1 in a stack") {
        std::vector<world::EffectInstance> e = good;
        byId(e, beam).order = 5; // the World's stack is now {0, 5}
        refusedFor(e, "orders");
        byId(e, beam).order = 0; // and now {0, 0}
        refusedFor(e, "orders");
    }
    SECTION("an `owner` source on a World-owned pulse") {
        std::vector<world::EffectInstance> e = good;
        world::EffectInstance p = byId(e, pulse);
        p.id = "homeless-pulse";
        p.owner = kWorld;
        p.order = 2;
        REQUIRE(p.wave.source.kind == world::SourceKind::Owner);
        e.push_back(p);
        world::normaliseEffectOrder(e);
        refusedFor(e, "homeless-pulse");
    }
}

// ---- status ------------------------------------------------------------------------------------

TEST_CASE("every instance says what happened to it on the frame", "[effects][stack][status]") {
    SECTION("a tornado and an aurora on one frame are both drawn") {
        const std::vector<world::EffectInstance> effects{alwaysOn(EffectKind::Tornado, "tornado"),
                                                         alwaysOn(EffectKind::Aurora, "aurora")};
        const std::vector<EffectStatus> status = evaluate(effects, 1.0);
        CHECK(status[0] == EffectStatus::Drawn);
        CHECK(status[1] == EffectStatus::Drawn);
    }
    SECTION("the ninth live wave is dropped, and the eight before it are drawn") {
        std::vector<world::EffectInstance> effects;
        for (int i = 0; i < 9; ++i) {
            effects.push_back(livePulse("wave-" + std::to_string(i)));
        }
        world::WaveFrame waves{};
        world::AtmosphericFrame sky{};
        const std::vector<EffectStatus> status = evaluate(effects, 0.5, waves, sky);
        CHECK(waves.count == world::kMaxGpuWaves);
        for (std::size_t i = 0; i < world::kMaxGpuWaves; ++i) {
            INFO(effects[i].id);
            CHECK(status[i] == EffectStatus::Drawn);
        }
        CHECK(status[8] == EffectStatus::Dropped);
    }
    SECTION("the fifth live medium is dropped, and the four before it are drawn") {
        std::vector<world::EffectInstance> effects;
        for (int i = 0; i < 5; ++i) {
            effects.push_back(alwaysOn(EffectKind::VolumetricFog, "fog-" + std::to_string(i)));
        }
        world::WaveFrame waves{};
        world::AtmosphericFrame sky{};
        const std::vector<EffectStatus> status = evaluate(effects, 0.5, waves, sky);
        CHECK(sky.mediumCount == world::kMaxMedia);
        for (std::size_t i = 0; i < world::kMaxMedia; ++i) {
            INFO(effects[i].id);
            CHECK(status[i] == EffectStatus::Drawn);
        }
        CHECK(status[4] == EffectStatus::Dropped);
    }
    SECTION("a disabled effect says disabled, and does not take a slot") {
        std::vector<world::EffectInstance> effects;
        for (int i = 0; i < 9; ++i) {
            effects.push_back(livePulse("wave-" + std::to_string(i)));
        }
        effects[0].enabled = false;
        const std::vector<EffectStatus> status = evaluate(effects, 0.5);
        CHECK(status[0] == EffectStatus::Disabled);
        // Eight live ones remain and eight fit: the one that WOULD have been ninth is drawn.
        CHECK(status[8] == EffectStatus::Drawn);
        CHECK(std::count(status.begin(), status.end(), EffectStatus::Dropped) == 0);
    }
    SECTION("an effect outside its window is dormant") {
        world::EffectInstance w = livePulse("later");
        w.activation = world::Activation::Window;
        w.timing.windowStart = 30.0;
        w.timing.windowSeconds = 5.0;
        const std::vector<EffectStatus> status = evaluate({w}, 1.0);
        CHECK(status[0] == EffectStatus::Dormant);
    }
}

TEST_CASE("an effect attached to an entity the scene does not have is orphaned",
          "[effects][stack][status][engine]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    world::HeroPoint rook;
    rook.name = "rook";
    rook.position = glm::vec3(10.0f, 0.0f, 0.0f);
    REQUIRE(engine.composition()->setHeroes({rook}).has_value());

    std::vector<world::EffectInstance> effects;
    const std::string present = add(effects, EffectOwner::entity("rook"), EffectKind::GroundPulse);
    const std::string absent = add(effects, EffectOwner::entity("ghost"), EffectKind::GroundPulse);
    const std::string sky = add(effects, kWorld, EffectKind::Aurora);
    REQUIRE(engine.setEffects(effects).has_value());

    FrameTime time;
    time.renderTime = 1.0;
    engine.update(time);

    CHECK(engine.effectStatus(absent) == EffectStatus::Orphaned);
    // The controls: the same type on an entity that exists is not orphaned, and neither is the
    // World's -- so "orphaned" is about the owner, not about the type or the frame.
    CHECK(engine.effectStatus(present) != EffectStatus::Orphaned);
    CHECK(engine.effectStatus(sky) != EffectStatus::Orphaned);

    SECTION("renaming the entity's effects onto an owner that exists un-orphans them") {
        REQUIRE(engine
                    .editEffects([](std::vector<world::EffectInstance>& list) -> Result<void> {
                        world::renameEffectOwner(list, EffectOwner::entity("ghost"), EffectOwner::entity("rook"));
                        return {};
                    })
                    .has_value());
        engine.update(time);
        CHECK(engine.effectStatus(absent) != EffectStatus::Orphaned);
        // ...and the id survived the move, which is why the status could be asked by it at all.
        CHECK(world::findEffect(engine.effects(), absent) < engine.effects().size());
    }
}

// ---- the instance's own file form ----------------------------------------------------------------

TEST_CASE("several effects round-trip through the instance's canonical JSON",
          "[effects][stack][json]") {
    std::vector<world::EffectInstance> effects;
    add(effects, kWorld, EffectKind::Aurora);
    add(effects, kWorld, EffectKind::Tornado);
    add(effects, EffectOwner::entity("rook"), EffectKind::GroundPulse);
    add(effects, EffectOwner::entity("sage"), EffectKind::GroundPulse);
    add(effects, EffectOwner::camera("crane"), EffectKind::TravelBeam);
    byId(effects, "sage-ground-pulse").enabled = false;
    byId(effects, "rook-ground-pulse").wave.appearance.intensity = 6.5f;
    byId(effects, "tornado").style = "custom";
    REQUIRE(world::validateEffects(effects).has_value());

    nlohmann::json doc = nlohmann::json::array();
    for (const auto& e : effects) {
        doc.push_back(e.toJson());
    }
    std::vector<world::EffectInstance> back;
    for (const auto& j : doc) {
        auto e = world::EffectInstance::fromJson(j);
        INFO(j.value("id", "?") << (e ? "" : ": " + e.error().message));
        REQUIRE(e.has_value());
        back.push_back(std::move(*e));
    }
    REQUIRE(back.size() == effects.size());
    for (std::size_t i = 0; i < effects.size(); ++i) {
        INFO(effects[i].id);
        CHECK(back[i].id == effects[i].id);
        CHECK(back[i].kind == effects[i].kind);
        CHECK(back[i].owner == effects[i].owner);
        CHECK(back[i].order == effects[i].order);
        CHECK(back[i].enabled == effects[i].enabled);
        CHECK(back[i].style == effects[i].style);
        CHECK(back[i].toJson() == doc[i]);
    }
    CHECK_FALSE(back[world::findEffect(back, "sage-ground-pulse")].enabled);
    CHECK(back[world::findEffect(back, "rook-ground-pulse")].wave.appearance.intensity == 6.5f);
    CHECK(world::validateEffects(back).has_value());

    SECTION("the owner is written as {kind, name}, and a World owner has no name") {
        CHECK(doc[0]["owner"] == nlohmann::json{{"kind", "world"}});
        CHECK(doc[2]["owner"] == nlohmann::json{{"kind", "entity"}, {"name", "rook"}});
    }
    SECTION("an unknown owner kind is refused, by name") {
        nlohmann::json bad = doc[0];
        bad["owner"] = nlohmann::json{{"kind", "planet"}};
        const auto e = world::EffectInstance::fromJson(bad);
        REQUIRE_FALSE(e.has_value());
        CHECK(e.error().message.find("planet") != std::string::npos);
    }
}
