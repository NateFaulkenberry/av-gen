// §68, one field many subscribers: the bus, and the atmospheric family's subscription to it.
//
// The shape of every case here follows ADR-182: a probe that cannot fail proves nothing, so each
// one either carries a control that must fail, or asserts a difference rather than an absence.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/vortex.hpp"
#include "core/wind.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_conformance.hpp"
#include "world/effects/field_bus.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <vector>

using namespace avgen;
namespace fx = avgen::world::fields;
namespace conf = avgen::world::conformance;

namespace {

// A wind hard enough that its strength cannot be near zero anywhere. ADR-055's regional term is
// `max(1 + amount*0.5*(r1 + r2), 0)` with each `r` in [-1, 1], so at `regionAmount` 0.45 the
// strength is at least 0.55 of the speed at every point in the world. That is what stops the cases
// below passing by accident at a quiet spot -- the failure mode a field test is most prone to.
[[nodiscard]] wind::WindParams gale() {
    wind::WindParams w;
    w.enabled = true;
    w.speed = 3.0f;
    w.direction = 0.6f;
    w.regionAmount = 0.45f;
    w.gustAmount = 0.9f;
    return w;
}

[[nodiscard]] vortex::VortexField funnel() {
    vortex::VortexField v;
    v.center = glm::vec3(0.0f, 60.0f, 0.0f);
    v.radius = 400.0f;
    v.funnelDepth = 300.0f;
    return v;
}

[[nodiscard]] fx::FieldBus windBus() {
    fx::FieldBus bus;
    bus.publishWind(std::string(fx::kWindField), wind::packWind(gale()));
    return bus;
}

} // namespace

// ---- the bus itself ------------------------------------------------------------------------------

TEST_CASE("an unpublished name resolves to nothing, and nothing is not the first field",
          "[world][fields][bus]") {
    fx::FieldBus bus = windBus();

    CHECK(bus.resolve(fx::kWindField) == 0);
    CHECK(bus.resolve("gale") == fx::kNoField);
    CHECK(bus.resolve("") == fx::kNoField);

    // The distinction the whole design rests on: `kNoField` is not 0. A caller that forgot to check
    // and passed the result straight to `sample` must not silently get the first published field,
    // because that is precisely how a dead name becomes a picture that looks plausible.
    CHECK(fx::kNoField != 0);
    const fx::FlowSample dead = bus.sample(fx::kNoField, glm::vec3(10.0f, 0.0f, -4.0f), 1.0f);
    CHECK(dead.strength == 0.0f);
    CHECK(dead.gust == 0.0f);
    CHECK(glm::length(dead.flow) == 0.0f);

    // ...and the control, without which the assertions above pass over an empty bus.
    const fx::FlowSample live = bus.sample(bus.resolve(fx::kWindField), glm::vec3(10.0f, 0.0f, -4.0f), 1.0f);
    CHECK(live.strength > 0.0f);
}

TEST_CASE("publishing a name twice replaces it rather than accumulating", "[world][fields][bus]") {
    fx::FieldBus bus = windBus();
    REQUIRE(bus.size() == 1);

    wind::WindParams calmer = gale();
    calmer.speed = 0.5f;
    bus.publishWind(std::string(fx::kWindField), wind::packWind(calmer));

    CHECK(bus.size() == 1);
    const fx::FlowSample s = bus.sample(fx::kWindField, glm::vec3(40.0f, 0.0f, 12.0f), 2.0f);
    // The second publish won, which is what a renamed or re-tuned field has to do.
    CHECK(s.strength < 1.0f);
}

TEST_CASE("the bus does not re-implement a field -- it dispatches to the one that owns it",
          "[world][fields][bus]") {
    // The single most valuable property here. ADR-055 and ADR-388 each pay for a CPU/GPU parity
    // test; a bus that transliterated the wind a third time would need a third, and would silently
    // drift the day nobody wrote it. So the bus's answer is asserted to be the SAME CALL's answer,
    // exactly, bit for bit where the translation is the identity.
    const wind::WindUniforms u = wind::packWind(gale());
    fx::FieldBus bus;
    bus.publishWind("w", u);

    for (const glm::vec3 p : {glm::vec3(0.0f), glm::vec3(120.0f, 3.0f, -80.0f),
                              glm::vec3(-2000.0f, 0.0f, 640.0f)}) {
        for (const float t : {0.0f, 1.75f, 93.5f}) {
            const wind::WindSample direct = wind::sampleWind(u, p, t);
            const fx::FlowSample viaBus = bus.sample("w", p, t);
            CHECK(viaBus.strength == direct.strength);
            CHECK(viaBus.gust == direct.gust);
            CHECK(viaBus.phase == direct.phase);
            CHECK(viaBus.units == fx::FlowUnits::Normalised);
            // The one thing the bus does compute: direction times strength, with Y flat because
            // ADR-055's field is columnar.
            CHECK(viaBus.flow.y == 0.0f);
        }
    }

    const vortex::VortexUniforms vu = vortex::packVortex(funnel());
    bus.publishVortex("v", vu);
    int inside = 0;
    for (int i = 0; i < 40; ++i) {
        const float a = static_cast<float>(i) * 0.31f;
        const glm::vec3 p(std::cos(a) * 200.0f, 40.0f - static_cast<float>(i) * 4.0f, std::sin(a) * 200.0f);
        const vortex::VortexSample direct = vortex::sampleVortex(vu, p, 3.0f);
        const fx::FlowSample viaBus = bus.sample("v", p, 3.0f);
        CHECK(viaBus.strength == direct.density);
        CHECK(viaBus.flow == direct.velocity);
        CHECK(viaBus.units == fx::FlowUnits::MetresPerSecond);
        CHECK(viaBus.gust == 0.0f); // a funnel has no travelling fronts
        if (direct.density > 0.0f) {
            ++inside;
        }
    }
    // ADR-182: a parity test over a field that is zero everywhere passes perfectly. Count.
    CHECK(inside > 4);
}

TEST_CASE("the units are carried, not assumed", "[world][fields][bus]") {
    fx::FieldBus bus = windBus();
    bus.publishVortex("v", vortex::packVortex(funnel()));

    // The two publishers genuinely differ and the sample says which it is. A subscriber that
    // integrates a displacement has to know; one that only scales its own motion does not, and the
    // atmospheric family is the second sort -- which is why it can subscribe to either.
    CHECK(bus.sample(fx::kWindField, glm::vec3(5.0f), 1.0f).units == fx::FlowUnits::Normalised);
    CHECK(bus.sample("v", glm::vec3(0.0f, -20.0f, 60.0f), 1.0f).units == fx::FlowUnits::MetresPerSecond);
    CHECK(std::string(fx::flowUnitsName(fx::FlowUnits::Normalised)) != fx::flowUnitsName(fx::FlowUnits::MetresPerSecond));
}

TEST_CASE("sampling is a pure function of position and time, in any order",
          "[world][fields][bus][determinism]") {
    // ADR-091's two-tier determinism, in the only form that is checkable: a thing that integrates a
    // frame delta cannot answer arbitrary (position, time) pairs in arbitrary order and agree with
    // itself. The control below is that the field is not merely constant, which would pass this.
    const fx::FieldBus bus = windBus();
    const glm::vec3 p(37.0f, 0.0f, -211.0f);
    const std::array<float, 6> times{4.0f, 0.5f, 91.25f, 0.5f, 4.0f, 91.25f};

    std::array<fx::FlowSample, 6> forward{};
    for (std::size_t i = 0; i < times.size(); ++i) {
        forward[i] = bus.sample(fx::kWindField, p, times[i]);
    }
    std::array<fx::FlowSample, 6> backward{};
    for (std::size_t i = times.size(); i-- > 0;) {
        backward[i] = bus.sample(fx::kWindField, p, times[i]);
    }
    for (std::size_t i = 0; i < times.size(); ++i) {
        CHECK(forward[i].strength == backward[i].strength);
        CHECK(forward[i].gust == backward[i].gust);
        CHECK(forward[i].flow == backward[i].flow);
    }
    // The same time answers the same thing whenever it is asked...
    CHECK(forward[0].gust == forward[4].gust);
    // ...and a different time answers a different thing, or the four checks above are vacuous.
    CHECK(forward[0].gust != forward[2].gust);
}

TEST_CASE("one field answers many subscribers, and answers them differently",
          "[world][fields][bus]") {
    // The whole claim of §68 in one case. Two subscribers at one place get one answer -- which five
    // private `flowSpeed`s can only do if somebody types the same number into all five. Two
    // subscribers a long way apart get different answers -- which a shared CLOCK cannot do, and
    // which is the difference between a field and a global.
    const fx::FieldBus bus = windBus();
    const float t = 12.0f;

    const fx::FlowSample here = bus.sample(fx::kWindField, glm::vec3(0.0f, 0.0f, 0.0f), t);
    const fx::FlowSample alsoHere = bus.sample(fx::kWindField, glm::vec3(0.0f, 900.0f, 0.0f), t);
    // Same column: the field is columnar, so height does not change the answer. Two effects over
    // one patch of valley agree exactly.
    CHECK(here.strength == alsoHere.strength);
    CHECK(here.gust == alsoHere.gust);
    CHECK(here.phase == alsoHere.phase);

    const fx::FlowSample faraway = bus.sample(fx::kWindField, glm::vec3(1800.0f, 0.0f, -1400.0f), t);
    CHECK(faraway.phase != here.phase);
    const bool differs = faraway.strength != here.strength || faraway.gust != here.gust;
    CHECK(differs);
}

TEST_CASE("a subscription naming a field nobody publishes is reported by name",
          "[world][fields][bus]") {
    const fx::FieldBus bus = windBus();

    const std::vector<std::string> names{"Opening Streak", "Valley Aurora", "Quiet One"};
    std::vector<fx::Subscription> subs(3);
    subs[0].field = "gale";              // dead
    subs[0].influence = 1.0f;
    subs[1].field = std::string(fx::kWindField); // live
    subs[1].influence = 0.8f;
    subs[2].field.clear();               // never subscribed; not a problem and not reported

    const std::vector<fx::DeadSubscription> dead = bus.unresolved(names, subs);
    REQUIRE(dead.size() == 1);
    CHECK(dead[0].subscriber == "Opening Streak");
    CHECK(dead[0].field == "gale");

    // An influence of 0 does not excuse a dead name: an artist who set a subscription up and turned
    // the influence down still wants to be told the name went stale, because turning it back up is
    // one drag away and would do nothing.
    subs[0].influence = 0.0f;
    CHECK(bus.unresolved(names, subs).size() == 1);

    // Publishing the missing field clears it -- the report is a statement about this frame, not a
    // record that accumulates.
    fx::FieldBus richer = bus;
    richer.publishWind("gale", wind::packWind(gale()));
    CHECK(richer.unresolved(names, subs).empty());

    // Short `subscribers` must not read off the end.
    CHECK(bus.unresolved({}, subs).size() == 1);
    CHECK(bus.unresolved({}, subs)[0].subscriber == "<unnamed>");
}

// ---- the atmospheric family's subscription -------------------------------------------------------

TEST_CASE("the subscription survives save and load, both halves",
          "[world][atmospherics][fields]") {
    // ADR-350, and specifically the hole ADR-392 names: `toJson(fromJson(toJson(e))) == toJson(e)`
    // passes when a key is missing from BOTH directions. So the value is checked by name on the way
    // out and by value on the way back, and the document is checked for the key's presence.
    world::EffectInstance e = world::glowmereAurora("Valley Aurora");
    e.flow.field = "atmos/Cosmic Vortex";
    e.flow.influence = 1.37f;

    const nlohmann::json doc = e.toJson();
    REQUIRE(doc.contains("flow"));
    CHECK(doc.at("flow").at("field").get<std::string>() == "atmos/Cosmic Vortex");

    const auto back = world::EffectInstance::fromJson(doc);
    REQUIRE(back.has_value());
    CHECK(back->flow.field == "atmos/Cosmic Vortex");
    CHECK(back->flow.influence == Catch::Approx(1.37f));

    // A document written before §68 existed has no `flow` block and must load as an unsubscribed
    // effect rather than failing -- which is every scene and project in the repository today.
    nlohmann::json old = doc;
    old.erase("flow");
    const auto legacy = world::EffectInstance::fromJson(old);
    REQUIRE(legacy.has_value());
    CHECK(legacy->flow.field.empty());
    CHECK(legacy->flow.influence == 0.0f);
    CHECK_FALSE(legacy->flow.requested());
}

TEST_CASE("every kind registers the leaf the panel's field row names",
          "[world][atmospherics][fields][conformance][ui]") {
    // ADR-382, and ADR-500's form of it: the shared rows are a table the panel walks by leaf, so
    // the panel and this test read the same data and cannot disagree. A leaf five characters wrong
    // draws an empty box and says nothing.
    std::vector<std::string_view> leaves;
    for (const world::EffectField& f : world::sharedEffectFields()) {
        if (std::string_view(f.leaf) == "flowInfluence") {
            leaves.push_back(f.leaf);
        }
    }
    REQUIRE_FALSE(leaves.empty());

    for (const world::EffectKind kind : conf::kEffectKinds) {
        INFO("kind: " << world::effectKindName(kind));
        const conf::Report r = conf::checkLeavesExist(kind, leaves, "panel-flow-row");
        INFO(r.summary());
        CHECK(r.clean());
    }
}

TEST_CASE("a comet subscribed to the wind is moved by where it is, not only by when",
          "[world][atmospherics][fields]") {
    // The spatial half, at the level the picture sees it. The same comet, the same second, the same
    // field, two places in the world: the packed lanes must differ. A shared clock would give the
    // same answer to both, and a shared clock is what this replaces.
    fx::FieldBus bus = windBus();
    world::AtmosphericContext ctx;
    ctx.seconds = 3.0;
    ctx.fieldBus = &bus;

    world::EffectInstance e = world::bioluminescentComet("Streak");
    // A comet is an event with a window (§3.5), and its factory's window opens at 4 s. Make it
    // permanent so the case is about the field rather than about the clock.
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.comet.path.anchor = world::SkyAnchor::World;
    e.flow.field = std::string(fx::kWindField);
    e.flow.influence = 1.0f;

    const auto packAt = [&](glm::vec3 where) {
        e.comet.path.anchorPosition = where;
        world::AtmosphericFrame f{};
        world::buildAtmosphericFrame(std::span(&e, 1), ctx, f);
        REQUIRE(f.cometCount == 1);
        return f.comets[0];
    };

    const world::CometGpu here = packAt(glm::vec3(0.0f));
    const world::CometGpu there = packAt(glm::vec3(2600.0f, 0.0f, -1900.0f));

    // `shape.y` is the wisp displacement in metres and `shape.w` the flow phase -- the two lanes the
    // subscription reaches. The anchors differ too, which is why the check is on those two.
    const bool moved = here.shape.y != there.shape.y || here.shape.w != there.shape.w;
    CHECK(moved);

    // And the control: with the subscription off, the two places give the SAME two lanes, so the
    // difference above is the field's doing and not the anchor's.
    e.flow.influence = 0.0f;
    const world::CometGpu stillHere = packAt(glm::vec3(0.0f));
    const world::CometGpu stillThere = packAt(glm::vec3(2600.0f, 0.0f, -1900.0f));
    CHECK(stillHere.shape.y == stillThere.shape.y);
    CHECK(stillHere.shape.w == stillThere.shape.w);
}

TEST_CASE("a dead subscription renders as still air rather than as nothing at all",
          "[world][atmospherics][fields]") {
    // The failure mode a field layer invites: a name goes stale and the effect vanishes, or worse,
    // the scene refuses to load. Neither. The effect renders exactly as an unsubscribed one, and
    // the name is reported elsewhere.
    fx::FieldBus bus = windBus();
    world::AtmosphericContext ctx;
    ctx.seconds = 3.0;
    ctx.fieldBus = &bus;

    world::EffectInstance e = world::glowmereAurora("Curtain");
    world::AtmosphericFrame unsubscribed{};
    world::buildAtmosphericFrame(std::span(&e, 1), ctx, unsubscribed);

    e.flow.field = "a field that does not exist";
    e.flow.influence = 2.0f;
    world::AtmosphericFrame stale{};
    world::buildAtmosphericFrame(std::span(&e, 1), ctx, stale);

    REQUIRE(unsubscribed.auroraCount == 1);
    REQUIRE(stale.auroraCount == 1);
    CHECK(std::memcmp(&unsubscribed.auroras[0], &stale.auroras[0], sizeof(world::AuroraGpu)) == 0);

    // ...and the control, so the case is not asserting that the aurora is inert: a LIVE name at the
    // same influence does change it.
    e.flow.field = std::string(fx::kWindField);
    world::AtmosphericFrame live{};
    world::buildAtmosphericFrame(std::span(&e, 1), ctx, live);
    CHECK(std::memcmp(&unsubscribed.auroras[0], &live.auroras[0], sizeof(world::AuroraGpu)) != 0);
}

TEST_CASE("a subscribed frame is a pure function of the transport second",
          "[world][atmospherics][fields][determinism]") {
    // ADR-091: scrub must equal play. The field bus is the newest thing in the resolve that could
    // break it, so the frame it produces is built in ascending order and then in scrambled order
    // and the two must agree exactly.
    fx::FieldBus bus = windBus();
    bus.publishVortex(fx::vortexFieldName("Cosmic Vortex"), vortex::packVortex(funnel()));

    std::vector<world::EffectInstance> effects;
    effects.push_back(world::bioluminescentComet("Streak"));
    effects.push_back(world::glowmereAurora("Curtain"));
    effects.push_back(world::cosmicVortex("Cosmic Vortex"));
    for (world::EffectInstance& e : effects) {
        // Permanent, so every second sampled below has all three effects live in it and the case is
        // about determinism rather than about which windows happened to be open.
        e.activation = world::Activation::Always;
        e.timing = world::Timing{};
        e.flow.field = std::string(fx::kWindField);
        e.flow.influence = 1.5f;
    }

    const std::array<double, 5> ascending{0.25, 1.5, 4.0, 5.5, 7.25};
    const std::array<double, 5> scrambled{5.5, 0.25, 7.25, 4.0, 1.5};

    const auto frameAt = [&](double t) {
        world::AtmosphericContext ctx;
        ctx.seconds = t;
        ctx.fieldBus = &bus;
        world::AtmosphericFrame f{};
        world::buildAtmosphericFrame(effects, ctx, f);
        return f;
    };

    std::array<world::AtmosphericFrame, 5> play{};
    for (std::size_t i = 0; i < ascending.size(); ++i) {
        play[i] = frameAt(ascending[i]);
    }
    for (const double t : scrambled) {
        const auto it = std::find(ascending.begin(), ascending.end(), t);
        REQUIRE(it != ascending.end());
        const std::size_t i = static_cast<std::size_t>(it - ascending.begin());
        const world::AtmosphericFrame scrubbed = frameAt(t);
        // Per block, not over the struct. `AtmosphericFrame` USED to have padding between
        // `hasVortex` and `vortex` that member-wise assignment left indeterminate, and comparing it
        // reported differences that were not differences -- that cost an hour; see `frameDiffers`.
        // ADR-562 removed the cause (a slot is `vec4` lanes with its padding declared), so the
        // media block is now safe to compare whole. The per-block habit is kept because the OTHER
        // blocks have not changed and the reason still applies to them.
        CHECK(std::memcmp(&play[i].comets, &scrubbed.comets, sizeof(play[i].comets)) == 0);
        CHECK(std::memcmp(&play[i].auroras, &scrubbed.auroras, sizeof(play[i].auroras)) == 0);
        CHECK(std::memcmp(&play[i].media, &scrubbed.media, sizeof(play[i].media)) == 0);
        CHECK(play[i].mediumCount == scrubbed.mediumCount);
    }

    // The control: the frames are not all the same frame, or the loop above proves nothing.
    CHECK(std::memcmp(&play[0].comets, &play[4].comets, sizeof(play[0].comets)) != 0);
}

TEST_CASE("the funnel leans downwind, by a bounded amount, and only when asked",
          "[world][atmospherics][fields]") {
    fx::FieldBus bus = windBus();
    world::AtmosphericContext ctx;
    ctx.seconds = 6.0;
    ctx.fieldBus = &bus;

    world::EffectInstance e = world::cosmicVortex("Cosmic Vortex");
    e.vortex.field.radius = 400.0f;
    e.vortex.field.center = glm::vec3(0.0f, 60.0f, 0.0f);

    world::AtmosphericFrame upright{};
    world::buildAtmosphericFrame(std::span(&e, 1), ctx, upright);
    REQUIRE(upright.mediumCount == 1);
    // Lane 0 is centre.xyz + radius (the map is beside each kind's `packMedium`).
    CHECK(glm::vec3(upright.media[0].lane[0]) == e.vortex.field.center);

    e.flow.field = std::string(fx::kWindField);
    e.flow.influence = 2.0f; // the soft maximum
    world::AtmosphericFrame leaning{};
    world::buildAtmosphericFrame(std::span(&e, 1), ctx, leaning);
    REQUIRE(leaning.mediumCount == 1);

    const glm::vec3 offset = glm::vec3(leaning.media[0].lane[0]) - e.vortex.field.center;
    CHECK(glm::length(offset) > 1.0f);
    // A translation in the plane: the funnel drifts, it does not rise or sink.
    CHECK(offset.y == 0.0f);
    // Bounded at a fifth of the radius at the soft maximum, so a subscribed funnel cannot wander
    // off the island it was placed on (ADR-387).
    CHECK(glm::length(offset) <= 0.2f * e.vortex.field.radius + 1e-3f);
}

TEST_CASE("a vortex inside a closed activation window does not reach the march",
          "[world][atmospherics]") {
    // The defect §68's wiring exposed. `buildAtmosphericFrame` used to walk `effects` a second time
    // and test only `enabled`, under a comment claiming the resolve had already applied activation
    // and lifetime. It had not: the resolve takes `span<const>` and cannot write anything back.
    world::AtmosphericContext ctx;
    ctx.seconds = 0.0;

    world::EffectInstance e = world::cosmicVortex("Cosmic Vortex");
    e.vortex.field.radius = 400.0f;
    e.activation = world::Activation::Window;
    e.timing.windowStart = 100.0;
    e.timing.windowSeconds = 10.0;

    world::AtmosphericFrame closed{};
    world::buildAtmosphericFrame(std::span(&e, 1), ctx, closed);
    CHECK_FALSE((closed.mediumCount > 0));

    // ...and the control: inside its window it does march, so the check above is about the window
    // and not about the vortex being broken.
    ctx.seconds = 103.0;
    world::AtmosphericFrame open{};
    world::buildAtmosphericFrame(std::span(&e, 1), ctx, open);
    CHECK((open.mediumCount > 0));
    CHECK(open.media[0].lane[0].w == 400.0f);
}

TEST_CASE("a vortex fades with its lifetime envelope instead of switching off",
          "[world][atmospherics]") {
    // The other half of the same defect. A fade is a fade: the two PER-METRE coefficients (ADR-374)
    // scale, and nothing else does -- fading the colours would leave a full-strength grey funnel and
    // fading the radius would shrink it rather than dim it.
    world::EffectInstance e = world::cosmicVortex("Cosmic Vortex");
    e.vortex.field.radius = 400.0f;
    e.activation = world::Activation::Window;
    e.timing.windowStart = 0.0;
    e.timing.windowSeconds = 10.0;
    e.timing.fadeIn = 4.0;

    world::AtmosphericContext ctx;
    const auto vortexAt = [&](double t) {
        ctx.seconds = t;
        world::AtmosphericFrame f{};
        world::buildAtmosphericFrame(std::span(&e, 1), ctx, f);
        return f;
    };

    const world::AtmosphericFrame early = vortexAt(1.0);
    const world::AtmosphericFrame full = vortexAt(6.0);
    REQUIRE((early.mediumCount > 0));
    REQUIRE((full.mediumCount > 0));

    CHECK(early.media[0].lane[1].w < full.media[0].lane[1].w);
    CHECK(early.media[0].lane[3].z < full.media[0].lane[3].z);
    // Everything else is untouched, including the geometry: a fading funnel is the same funnel.
    CHECK(early.media[0].lane[0].w == full.media[0].lane[0].w);
    CHECK(early.media[0].lane[11] == full.media[0].lane[11]); // accent colour
    // Past the fade the coefficients are the authored ones exactly, so a scene with no fade -- which
    // is every scene in the repository -- is arithmetically untouched by this.
    CHECK(full.media[0].lane[1].w == e.vortex.density);
    CHECK(full.media[0].lane[3].z == e.vortex.emission);
}
