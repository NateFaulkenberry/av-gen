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
        CHECK(frame.hasVortex);
        CHECK(frame.vortex.radius > 0.0f);
        // What makes it a bank rather than a funnel, and the reason it is a kind rather than a
        // preset: the panel it draws has no swirl, no throat and no funnel depth on it.
        CHECK(frame.vortex.swirl == 0.0f);
        CHECK(frame.vortex.funnelDepth == 0.0f);
    }

    SECTION("a bank and a funnel in one scene are a reported limit, not a silent no-op") {
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
        const world::AtmosphericCounts counts =
            world::resolveAtmosphericEffects(both, ctx, comets, auroras);
        CHECK(counts.vortices == 1);
        CHECK(counts.dropped == 1);
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
        // The fog block aliases the vortex's struct entirely, so it holds nothing of its own.
        REQUIRE(doc.contains("fog"));
        CHECK(doc["fog"].empty());
        // The shower block holds only its six own numbers.
        REQUIRE(doc.contains("meteors"));
        CHECK(doc["meteors"].size() == 6);
    }
}
