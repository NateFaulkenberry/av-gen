// ADR-500: the world-effect registry.
//
// `tests/unit/test_effect_conformance.cpp` holds the family's contract -- default routes, the
// round trip, ranges, dispatch, the kind name, the field subscription -- and now runs it over five
// kinds instead of three without a line being added to it. What this file adds is the contract of
// the *registry itself*: the thing ADR-500 introduced, and the thing whose failure would make
// every check in that file quietly pass while an effect was half-declared.
//
// Every probe here is shown able to fail (ADR-182), by constructing the malformed thing rather
// than by asserting that the well-formed thing is well formed.

#include "params/parameter_set.hpp"
#include "world/atmospheric_params.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace avgen;

namespace {

const world::EffectSchema& schemaFor(world::AtmosphereKind kind) {
    const world::EffectSchema* s = world::effectSchema(kind);
    REQUIRE(s != nullptr);
    return *s;
}

} // namespace

TEST_CASE("the registry declares every kind completely", "[world][atmospherics][registry]") {
    const std::vector<world::RegistryFinding> findings = world::checkRegistry();
    std::string summary;
    for (const world::RegistryFinding& f : findings) {
        summary += f.subject + " [" + f.rule + "] " + f.detail + "\n";
    }
    INFO(summary);
    REQUIRE(findings.empty());

    // ...and it is not empty, which is the failure a static self-registering constructor would have
    // produced in a release build and not in a debug one. `builtinSchemas()` is an explicit list
    // for exactly this reason.
    CHECK(world::effectSchemas().size() == world::kAtmosphereKinds.size());
    for (const world::AtmosphereKind kind : world::kAtmosphereKinds) {
        INFO("kind index " << world::atmosphereKindIndex(kind));
        CHECK(world::effectSchema(kind) != nullptr);
    }
}

TEST_CASE("a kind's serialised name round-trips and is its own", "[world][atmospherics][registry]") {
    std::set<std::string> keys;
    for (const world::EffectSchema* s : world::effectSchemas()) {
        INFO(s->key);
        CHECK(keys.insert(s->key).second);
        const auto back = world::atmosphereKindFromName(s->key);
        REQUIRE(back.has_value());
        CHECK(*back == s->kind);
        CHECK(std::string_view(world::atmosphereKindName(s->kind)) == s->key);
    }

    SECTION("a name no kind uses is refused rather than defaulted") {
        // The property that stops a file loading as a neighbour. ADR-392 found the resolve doing
        // exactly that -- an unnamed kind fell into the vortex arm -- and a lenient reader here
        // would put the same defect in the file format.
        CHECK_FALSE(world::atmosphereKindFromName("nosuchkind").has_value());
        CHECK_FALSE(world::atmosphereKindFromName("").has_value());
        CHECK_FALSE(world::atmosphereKindFromName("Comet").has_value()); // case matters in a file
    }
}

TEST_CASE("every row is complete, and an incomplete one is reported by name",
          "[world][atmospherics][registry]") {
    for (const world::EffectSchema* s : world::effectSchemas()) {
        for (const world::EffectField& f : s->fields) {
            INFO(std::string(s->key) + "/" + f.leaf);
            CHECK(f.leaf[0] != '\0');
            CHECK(f.label[0] != '\0');
            CHECK(f.hasAccessors());
            if (f.type == world::FieldType::Float) {
                CHECK(f.hardMax >= f.hardMin);
                CHECK(f.softMin >= f.hardMin);
                CHECK(f.softMax <= f.hardMax);
            }
        }
    }

    SECTION("the completeness test can fail") {
        // ADR-182, and the reason this is the shape it is. The `consteval` factories make a Float
        // row without float accessors a COMPILE error -- which cannot be demonstrated from inside a
        // test. What can be demonstrated is the runtime half, which catches a row built by
        // aggregate initialisation around the factories.
        world::EffectField bare;
        bare.type = world::FieldType::Float;
        bare.leaf = "halfDeclared";
        CHECK_FALSE(bare.hasAccessors());

        world::EffectField wrongType;
        wrongType.type = world::FieldType::Color;
        wrongType.getFloat = +[](const world::AtmosphericEffect&) { return 0.0f; };
        wrongType.setFloat = +[](world::AtmosphericEffect&, float) {};
        CHECK_FALSE(wrongType.hasAccessors()); // float accessors on a colour row
    }
}

TEST_CASE("a registered path exists for every row, and for no row that is not there",
          "[world][atmospherics][registry][params]") {
    for (const world::AtmosphereKind kind : world::kAtmosphereKinds) {
        const world::EffectSchema& s = schemaFor(kind);
        REQUIRE(s.factory != nullptr);
        const world::AtmosphericEffect probe = s.factory("registry probe");
        params::ParameterSet set;
        const world::AtmosphericParameters registered =
            world::registerAtmosphericParameters(set, std::span(&probe, 1));
        const std::string prefix = world::atmosphericParameterPrefix(probe.name);

        // Every declared row produced a parameter...
        for (const world::EffectField& f : s.fields) {
            INFO(prefix + f.leaf);
            CHECK(set.find(prefix + f.leaf) != nullptr);
        }
        for (const world::EffectField& f : world::sharedEffectFields()) {
            INFO(prefix + f.leaf);
            CHECK(set.find(prefix + f.leaf) != nullptr);
        }
        // ...and the cached pointers line up with the rows one-for-one, which is what
        // `copyParameters` walks positionally.
        REQUIRE(registered.effects.size() == 1);
        CHECK(registered.effects.front().values.size() ==
              s.fields.size() + world::sharedEffectFields().size());

        INFO("kind: " << world::atmosphereKindName(kind));
        CHECK(set.find(prefix + "noSuchLeaf") == nullptr);
    }
}

TEST_CASE("a value set through a row survives the file, by name", "[world][atmospherics][registry]") {
    // The round trip ADR-392 argued for, asked of the derived serialiser rather than of a
    // hand-written pair. The comparison is deliberately NOT `toJson(fromJson(toJson(e)))` against
    // `toJson(e)`: that identity holds when a key is missing from BOTH directions, because the
    // reader leaves the struct's default in place and the writer omits it again. Setting a distinct
    // value through the row and reading it back through the row asks the only question worth
    // asking -- did this number survive?
    for (const world::AtmosphereKind kind : world::kAtmosphereKinds) {
        const world::EffectSchema& s = schemaFor(kind);
        world::AtmosphericEffect e = s.factory("round trip");

        // A distinct value per row, varied by index so a serialiser that swapped two fields is
        // caught rather than agreeing with itself.
        std::size_t salt = 0;
        for (const world::EffectField& f : s.fields) {
            const float t = 0.17f + 0.11f * static_cast<float>(salt % 7);
            switch (f.type) {
            case world::FieldType::Float:
                world::setFieldFloat(f, s, e, f.hardMin + t * (f.hardMax - f.hardMin));
                break;
            case world::FieldType::Color:
                world::setFieldColor(f, s, e, glm::vec3(t, t * 0.5f, 1.0f - t));
                break;
            case world::FieldType::Bool:
                world::setFieldBool(f, s, e, (salt % 2) == 0);
                break;
            // ADR-566: an INTEGER index, deliberately not the lerp the float rows get. A Choice
            // serialises as a name, so a fractional index would come back rounded and the
            // mismatch would be the test's rather than the code's -- and it would look exactly
            // like a real round-trip failure.
            case world::FieldType::Choice:
                REQUIRE(f.choiceCount > 0);
                world::setFieldFloat(f, s, e,
                                     static_cast<float>(static_cast<int>(salt) % f.choiceCount));
                break;
            }
            ++salt;
        }

        const nlohmann::json doc = e.toJson();
        const auto back = world::AtmosphericEffect::fromJson(doc);
        INFO("kind: " << world::atmosphereKindName(kind));
        REQUIRE(back.has_value());
        CHECK(back->kind == kind);

        for (const world::EffectField& f : s.fields) {
            INFO(std::string(s.key) + "/" + f.leaf);
            switch (f.type) {
            case world::FieldType::Float:
                CHECK(world::fieldFloat(f, s, *back) == world::fieldFloat(f, s, e));
                break;
            case world::FieldType::Color:
                CHECK(world::fieldColor(f, s, *back) == world::fieldColor(f, s, e));
                break;
            case world::FieldType::Bool:
                CHECK(world::fieldBool(f, s, *back) == world::fieldBool(f, s, e));
                break;
            case world::FieldType::Choice:
                CHECK(world::fieldFloat(f, s, *back) == world::fieldFloat(f, s, e));
                // The file has to carry the NAME. Checking only the value back would pass just as
                // well if the index were written, and the whole reason the name is there is the
                // day a primitive is appended to the list.
                CHECK(doc[s.key][f.leaf].is_string());
                break;
            }
        }
    }

    SECTION("the round trip can fail") {
        // ADR-182 again, and the thing the document comparison cannot see: a key deleted from the
        // saved file comes back as the factory's value, and this check names it. Deleting the key
        // here is the same event as a row being dropped from the serialiser.
        const world::EffectSchema& s = schemaFor(world::AtmosphereKind::Vortex);
        world::AtmosphericEffect e = s.factory("deleted key");
        e.vortex.filaments = 3.5f;
        nlohmann::json doc = e.toJson();
        REQUIRE(doc["vortex"].contains("filaments"));
        doc["vortex"].erase("filaments");
        const auto back = world::AtmosphericEffect::fromJson(doc);
        REQUIRE(back.has_value());
        CHECK(back->vortex.filaments != 3.5f);
    }
}

TEST_CASE("a kind whose numbers live in the store keeps them apart from its neighbours",
          "[world][atmospherics][registry]") {
    // The property that lets a kind be declared in its own file at all: it has no struct on the
    // shared header, so its numbers are keyed by `<kind>/<leaf>` in `AtmosphericEffect::values`.
    // Two kinds' `density` must not be one number, and an effect that changes kind must keep what
    // the kind it left was holding -- which is why `AtmosphericEffect` is not a variant.
    const world::EffectSchema& shower = schemaFor(world::AtmosphereKind::MeteorShower);
    world::AtmosphericEffect e = shower.factory("store");

    const world::EffectField* meteors = nullptr;
    for (const world::EffectField& f : shower.fields) {
        if (std::string_view(f.leaf) == "meteors") { meteors = &f; }
    }
    REQUIRE(meteors != nullptr);
    REQUIRE(meteors->stored);

    world::setFieldFloat(*meteors, shower, e, 4.0f);
    CHECK(world::fieldFloat(*meteors, shower, e) == 4.0f);
    CHECK(e.values.getFloat("meteors/meteors", -1.0f) == 4.0f);
    // ...and nothing else claims that key.
    CHECK(e.values.getFloat("fog/meteors", -1.0f) == -1.0f);

    SECTION("it survives the file") {
        const nlohmann::json doc = e.toJson();
        REQUIRE(doc.contains("meteors"));
        CHECK(doc["meteors"]["meteors"].get<float>() == 4.0f);
        const auto back = world::AtmosphericEffect::fromJson(doc);
        REQUIRE(back.has_value());
        CHECK(world::fieldFloat(*meteors, shower, *back) == 4.0f);
    }

    SECTION("changing kind does not throw the other kind's settings away") {
        e.comet.appearance.coreIntensity = 33.0f;
        world::applyEffectStyle(e, world::AtmosphereKind::Aurora, world::effectStyleNames(
                                                                      world::AtmosphereKind::Aurora)[0]);
        CHECK(e.kind == world::AtmosphereKind::Aurora);
        CHECK(e.values.getFloat("meteors/meteors", -1.0f) == 4.0f);
    }
}

TEST_CASE("a meteor shower resolves as several streaks and a fog bank as one medium",
          "[world][atmospherics][registry][resolve]") {
    // The two proof effects, at the one place where "it is declared" and "it reaches the picture"
    // are different claims. ADR-387's defect was a kind that was declared everywhere and resolved
    // as a neighbour.
    SECTION("the shower puts one record per meteor in the comet bucket") {
        world::AtmosphericEffect e =
            world::makeAtmosphericEffect(world::AtmosphereKind::MeteorShower, "shower");
        e.activation = world::Activation::Always;
        e.timing = world::Timing{};
        e.values.setFloat("meteors/meteors", 4.0f);
        e.values.setFloat("meteors/stagger", 0.0f); // all four in the air at once

        world::AtmosphericContext ctx;
        ctx.seconds = 0.3;
        std::array<world::ResolvedAtmospheric, world::kMaxGpuComets> comets{};
        std::array<world::ResolvedAtmospheric, world::kMaxGpuAuroras> auroras{};
        const world::AtmosphericCounts counts =
            world::resolveAtmosphericEffects(std::span(&e, 1), ctx, comets, auroras);

        CHECK(counts.comets == 4);
        CHECK(counts.auroras == 0);
        CHECK(counts.vortices == 0);

        // Each streak is in a different place, which is the difference between a shower and one
        // comet drawn four times. Compared on the launch bearing, which is what the spread moves.
        CHECK(comets[0].dir0 != comets[1].dir0);
        CHECK(comets[1].dir0 != comets[2].dir0);
        CHECK(comets[2].dir0 != comets[3].dir0);
        // ...and each one packs, which is the claim that it reaches the shader.
        for (std::size_t i = 0; i < counts.comets; ++i) {
            const world::CometGpu g = world::packComet(comets[i]);
            INFO("streak " << i);
            CHECK(g.core.w > 0.0f); // a head radius, so the record is not the zero struct
        }
    }

    SECTION("the count control is a control") {
        // ADR-375: a control that does nothing teaches an artist the system is broken. This is the
        // half of that rule a test can check.
        for (const float n : {1.0f, 3.0f, 6.0f}) {
            world::AtmosphericEffect e =
                world::makeAtmosphericEffect(world::AtmosphereKind::MeteorShower, "shower");
            e.activation = world::Activation::Always;
            e.timing = world::Timing{};
            e.values.setFloat("meteors/meteors", n);
            e.values.setFloat("meteors/stagger", 0.0f);
            world::AtmosphericContext ctx;
            ctx.seconds = 0.3;
            std::array<world::ResolvedAtmospheric, world::kMaxGpuComets> comets{};
            std::array<world::ResolvedAtmospheric, world::kMaxGpuAuroras> auroras{};
            const world::AtmosphericCounts counts =
                world::resolveAtmosphericEffects(std::span(&e, 1), ctx, comets, auroras);
            INFO("asked for " << n);
            CHECK(counts.comets == static_cast<std::size_t>(n));
        }
    }

    SECTION("a shower is a pure function of the transport second") {
        // ADR-360's contract: two resolves of the same range may not differ from each other. The
        // arrangement is a hash of (seed, index), never a stream, for exactly this reason.
        world::AtmosphericEffect e =
            world::makeAtmosphericEffect(world::AtmosphereKind::MeteorShower, "shower");
        e.activation = world::Activation::Always;
        e.timing = world::Timing{};
        const auto resolveAt = [&](double t) {
            world::AtmosphericContext ctx;
            ctx.seconds = t;
            world::AtmosphericFrame frame{};
            world::buildAtmosphericFrame(std::span(&e, 1), ctx, frame);
            return frame;
        };
        const world::AtmosphericFrame a = resolveAt(2.75);
        const world::AtmosphericFrame b = resolveAt(9.0);
        const world::AtmosphericFrame again = resolveAt(2.75);
        CHECK(a.cometCount == again.cometCount);
        CHECK(std::memcmp(&a.comets, &again.comets, sizeof(a.comets)) == 0);
        // ...and the scene is not simply static, which is what makes the line above mean something.
        CHECK(std::memcmp(&a.comets, &b.comets, sizeof(a.comets)) != 0);
    }

    SECTION("a fog bank takes the volumetric march's one medium slot") {
        world::AtmosphericEffect fog =
            world::makeAtmosphericEffect(world::AtmosphereKind::VolumetricFog, "bank");
        fog.activation = world::Activation::Always;
        fog.timing = world::Timing{};
        world::AtmosphericContext ctx;
        ctx.seconds = 1.0;
        world::AtmosphericFrame frame{};
        world::buildAtmosphericFrame(std::span(&fog, 1), ctx, frame);
        // ADR-562: lanes, not an authored struct. Lane 0's `.w` is the radius and the gate; the
        // map is beside each kind's `packMedium`.
        REQUIRE(frame.mediumCount == 1);
        CHECK(frame.media[0].lane[0].w > 0.0f);
        // What makes it a bank rather than a funnel, and the reason it is a kind rather than a
        // preset: the panel it draws has no swirl, no throat and no funnel depth on it.
        CHECK(frame.media[0].lane[1].y == 0.0f); // swirl
        CHECK(frame.media[0].lane[4].x == 0.0f); // funnelDepth
    }

    SECTION("a bank and a funnel in one scene are BOTH marched") {
        // ADR-562, and this section used to assert the defect.
        //
        // It was called "a reported limit, not a silent no-op" and it checked
        // `counts.vortices == 1` and `counts.dropped == 1` -- enshrining the drop as correct
        // behaviour, in a test whose own name claimed the drop was reported. It was not reported:
        // ADR-560 measured that `dropped` had exactly one reader in the tree and it was a CPU
        // conformance finding, so a person running the editor saw nothing at all. The test passed
        // for two ADRs while the owner's bug sat underneath it.
        //
        // That is worth more than the assertion it replaces: **a test can hold a defect in place by
        // asserting it, and the name can describe the behaviour somebody intended rather than the
        // behaviour that exists.** Read a test's name as a claim to be checked, not as documentation.
        std::array<world::AtmosphericEffect, 2> both{
            world::makeAtmosphericEffect(world::AtmosphereKind::Vortex, "funnel"),
            world::makeAtmosphericEffect(world::AtmosphereKind::VolumetricFog, "bank")};
        for (world::AtmosphericEffect& e : both) {
            e.activation = world::Activation::Always;
            e.timing = world::Timing{};
        }
        world::AtmosphericContext ctx;
        ctx.seconds = 1.0;
        std::array<world::ResolvedAtmospheric, world::kMaxGpuComets> comets{};
        std::array<world::ResolvedAtmospheric, world::kMaxGpuAuroras> auroras{};
        std::array<world::ResolvedAtmospheric, world::kMaxMedia> media{};
        const world::AtmosphericCounts counts =
            world::resolveAtmosphericEffects(both, ctx, comets, auroras, media);
        CHECK(counts.vortices == 2);
        CHECK(counts.dropped == 0);

        // ...and both reach the frame, with distinct radii, so this cannot pass by seating one
        // medium twice. ADR-560's proof of the defect was that the two arms were byte-identical;
        // the proof of the fix has to be that they are not.
        world::AtmosphericFrame frame{};
        world::buildAtmosphericFrame(both, ctx, frame);
        REQUIRE(frame.mediumCount == 2);
        CHECK(frame.media[0].lane[0].w > 0.0f);
        CHECK(frame.media[1].lane[0].w > 0.0f);
        CHECK(frame.mediaDropped == 0);
        CHECK(std::memcmp(&frame.media[0], &frame.media[1], sizeof(frame.media[0])) != 0);
    }

}

TEST_CASE("the two new kinds cost nothing to the scenes that do not use them",
          "[world][atmospherics][registry]") {
    // The claim the render hashes prove at the pixel level, asked here at the level where it is
    // cheap to ask every build: a comet, an aurora and a vortex serialise to a document whose
    // per-kind blocks for the new kinds are either absent or carry no values of their own.
    //
    // It matters because `toJson` writes EVERY kind's block. A kind whose rows are all absolute
    // (they alias another kind's struct) writes an empty object, and a kind with stored rows writes
    // its defaults -- neither of which changes what any existing kind loads as.
    for (const world::AtmosphereKind kind : {world::AtmosphereKind::Comet,
                                             world::AtmosphereKind::Aurora,
                                             world::AtmosphereKind::Vortex}) {
        const world::AtmosphericEffect e = world::makeAtmosphericEffect(kind, "unchanged");
        const nlohmann::json doc = e.toJson();
        const auto back = world::AtmosphericEffect::fromJson(doc);
        INFO(doc.dump().substr(0, 400));
        REQUIRE(back.has_value());
        CHECK(back->kind == kind);
        // ADR-563: the fog block is no longer EMPTY, and this assertion changing is the test
        // reporting a real consequence of that change rather than breaking.
        //
        // It used to alias the vortex's struct entirely -- every row absolute -- so it wrote `{}`.
        // §46 B gave a fog bank six shape controls of its own (`bankLength`, `bankRotation`,
        // `edgeSoftness`, `groundHug`, `heightFalloff`, `domeShape`), and they live in
        // `EffectValueStore` because `world::Vortex` is shared with the tornado. Stored rows
        // serialise their defaults, exactly as the shower's six do.
        //
        // The consequence worth stating: **every effect of every kind now writes six more numbers
        // in its `fog` block**, because `toJson` writes every kind's block. They are defaults, so
        // nothing loads differently -- which is the property this case exists to check and still
        // checks, one line down.
        REQUIRE(doc.contains("fog"));
        // Fourteen since ADR-572 added §17's `driftWind` to ADR-571's thirteen. Counted with
        // `grep -c '    storedFloat("'` (13) plus `grep -c 'storedChoice("'` (1), not read off
        // the failure -- which is the third time that distinction has caught something. This number
        // moving is the case doing its job rather than breaking: it is the only thing in the suite
        // that notices a kind's stored rows changing what EVERY effect of every kind serialises,
        // because `toJson` writes every kind's block.
        //
        // **Take the number from the code, not from the failure.** `grep -c '    storedFloat('`
        // plus `grep -c 'storedChoice('` on `volumetric_fog_effect.cpp` is 9 + 1; a count derived
        // that way is still an assertion, and a count copied out of the red output is a test that
        // now agrees with whatever the code does. Third time this case has reported a true
        // consequence, and both times it caught me it was because a row was added and the count
        // was not re-derived.
        CHECK(doc["fog"].size() == 14);
        // The shower block holds only its six own numbers.
        REQUIRE(doc.contains("meteors"));
        CHECK(doc["meteors"].size() == 6);
    }
}

TEST_CASE("a shower authored in a file, with no comet block, still flies",
          "[world][atmospherics][registry][resolve]") {
    // This test exists because the render said otherwise, and the render was right.
    //
    // A meteor shower's streak aliases `e.comet`, so a hand-written scene that declares only a
    // `meteors` block gets the *struct defaults* for the trajectory rather than a preset's. That is
    // correct and is what "an effect keeps the settings of the kind it is not" means -- but it is
    // also the case nothing else in this file covers, because every other probe comes from the
    // factory, which has run a preset.
    const nlohmann::json doc = nlohmann::json{
        {"name", "authored shower"},
        {"enabled", true},
        {"kind", "meteors"},
        {"activation", "always"},
        {"timing", {{"delay", 0.0}, {"lifetime", 0.0}, {"fadeIn", 0.0}, {"fadeOut", 0.0},
                    {"windowStart", 0.0}, {"windowSeconds", 0.0}, {"repeatSeconds", 8.0}}},
        {"ground", {{"mode", "off"}}},
        {"flow", {{"field", ""}}},
        {"meteors", {{"meteors", 6.0}, {"spread", 52.0}, {"stagger", 0.35},
                     {"sizeVariation", 0.5}, {"radiantDrift", 8.0}, {"seed", 3.0}}}};

    const auto loaded = world::AtmosphericEffect::fromJson(doc);
    REQUIRE(loaded.has_value());
    CHECK(loaded->kind == world::AtmosphereKind::MeteorShower);
    CHECK(loaded->values.getFloat("meteors/meteors", -1.0f) == 6.0f);
    CHECK(loaded->values.getFloat("meteors/stagger", -1.0f) == 0.35f);

    world::AtmosphericContext ctx;
    ctx.seconds = 0.6;
    std::array<world::ResolvedAtmospheric, world::kMaxGpuComets> comets{};
    std::array<world::ResolvedAtmospheric, world::kMaxGpuAuroras> auroras{};
    world::AtmosphericEffect live = *loaded;
    const world::AtmosphericCounts counts =
        world::resolveAtmosphericEffects(std::span(&live, 1), ctx, comets, auroras);
    INFO("comets " << counts.comets << " dropped " << counts.dropped);
    CHECK(counts.comets > 0);

    // ...and it reaches a frame the renderer would draw, which is the half the resolve count does
    // not prove: `AtmosphericFrame::any()` gates the whole sky draw.
    world::AtmosphericFrame frame{};
    world::buildAtmosphericFrame(std::span(&live, 1), ctx, frame);
    CHECK(frame.cometCount > 0);
    CHECK(frame.any());
    // A head with a real radius and a real radiance, rather than the zero struct a mis-bucketed
    // record would leave behind.
    CHECK(frame.comets[0].core.w > 0.0f);
    CHECK((frame.comets[0].core.x + frame.comets[0].core.y + frame.comets[0].core.z) > 0.0f);

    SECTION("and at a second inside a later repeat pass, on a world-anchored track") {
        // The case a render disagreed about, pinned here so the disagreement has a witness. A
        // shower with `repeatSeconds` 6 at t = 6.8 is 0.8 s into its second pass, which is
        // mid-flight for the first meteors and nothing at all for the last.
        nlohmann::json later = doc;
        later["timing"]["repeatSeconds"] = 6.0;
        later["comet"] = nlohmann::json{
            {"path", {{"anchor", "world"}, {"anchorPosition", {0.0, 0.0, 0.0}},
                      {"startAzimuth", -70.0}, {"startElevation", 34.0},
                      {"endAzimuth", 55.0}, {"endElevation", 9.0},
                      {"distance", 2600.0}, {"travelSeconds", 2.2}, {"speedScale", 1.0},
                      {"acceleration", 0.3}, {"curvature", 0.0}, {"arcLift", 180.0}}},
            {"appearance", {{"coreIntensity", 40.0}, {"headSize", 22.0}, {"tailLength", 1400.0},
                            {"tailWidth", 40.0}, {"tailIntensity", 16.0}}}};
        const auto e2 = world::AtmosphericEffect::fromJson(later);
        REQUIRE(e2.has_value());
        CHECK(e2->comet.path.travelSeconds == 2.2f);
        CHECK(e2->comet.appearance.coreIntensity == 40.0f);

        world::AtmosphericEffect live2 = *e2;
        world::AtmosphericContext ctx2;
        ctx2.seconds = 6.8;
        world::AtmosphericFrame f2{};
        world::buildAtmosphericFrame(std::span(&live2, 1), ctx2, f2);
        INFO("cometCount at t=6.8 is " << f2.cometCount);
        CHECK(f2.cometCount > 0);
    }
}

// ---- the leaves a kind must not lose in a merge ------------------------------------------------
//
// Every other check in this file and in `test_effect_conformance.cpp` iterates `schema.fields` --
// so each of them asks whether what IS declared is well-formed, and none of them can see a field
// that stopped being declared at all. That is not hypothetical. ADR-500 moved each kind's rows out
// of `ui_logic.hpp` and `atmospheric_params.cpp` and into one file per effect, while ADR-461 was
// adding seven fields to the vortex in exactly those two files on another branch. Resolving that
// merge by taking the registry side -- which is the correct resolution for the structure -- drops
// all seven, and every test in the tree still passes: the JSON is derived from the schema, so a
// field the schema has forgotten is simply absent from the file rather than wrong in it.
//
// The GPU parity test does exercise those seven, but it sets `vortex::VortexField` members
// directly. A value that never survives the schema never reaches that struct, so it would not have
// caught this either.
//
// This is therefore a deliberate hard-coded list: a name here is a promise that an effect keeps a
// control an artist can already find in the panel. Removing a control is allowed -- editing this
// list is how you say so out loud.
TEST_CASE("a kind keeps the leaves its authored scenes and panels already name",
          "[world][atmospherics][registry]") {
    struct Expect {
        world::AtmosphereKind kind;
        std::vector<std::string> leaves;
    };
    const std::vector<Expect> promised{
        // ADR-461, Vortex 2.0 §7-§11: the eye, the bands, and the noise weight that §5's
        // diagnostic render turns off. `innerVoid` is the eye's radius and there is deliberately
        // no second `eyeRadius` beside it.
        {world::AtmosphereKind::Vortex,
         {"innerVoid", "eyeWallWidth", "eyeWallGain", "bandArms", "bandPitchDegrees", "bandDepth",
          "bandHarmonic", "cloudNoise", "smokeWarp", "smokeBillow", "detail"}},
    };

    for (const Expect& want : promised) {
        const world::EffectSchema& s = schemaFor(want.kind);
        std::set<std::string> have;
        for (const world::EffectField& f : s.fields) {
            have.insert(std::string(f.leaf));
        }
        for (const std::string& leaf : want.leaves) {
            INFO(std::string(world::atmosphereKindName(want.kind)) + " must declare '" + leaf + "'");
            CHECK(have.count(leaf) == 1);
        }

        // The probe can fail (ADR-182): a name that is not there is reported as missing rather
        // than passing because the loop found nothing to check.
        CHECK(have.count("eyeRadius") == 0);
        CHECK(have.count("notAField") == 0);
    }
}
