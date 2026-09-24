// ADR-500: the effect registry -- since ADR-702 the registry of EVERY effect type, the surface
// waves (Ground Pulse, Travel Beam) included.
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
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <utility>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using namespace avgen;

namespace {

const world::EffectSchema& schemaFor(world::EffectKind kind) {
    const world::EffectSchema* s = world::effectSchema(kind);
    REQUIRE(s != nullptr);
    return *s;
}

// Where a row lives in an instance's document: `parameters/<jsonPath or leaf>`, or a root-relative
// path when the row's `jsonPath` starts with '/' (the meteor shower's aliased comet rows).
const nlohmann::json* rowJson(const nlohmann::json& doc, const world::EffectField& f) {
    const std::string declared = f.jsonPath[0] != '\0' ? f.jsonPath : f.leaf;
    const std::string pointer =
        declared.front() == '/' ? declared : "/parameters/" + declared;
    const nlohmann::json::json_pointer ptr(pointer);
    return doc.contains(ptr) ? &doc.at(ptr) : nullptr;
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
    CHECK(world::effectSchemas().size() == world::kEffectKinds.size());
    for (const world::EffectKind kind : world::kEffectKinds) {
        INFO("kind index " << world::effectKindIndex(kind));
        CHECK(world::effectSchema(kind) != nullptr);
    }
}

TEST_CASE("a kind's serialised name round-trips and is its own", "[world][atmospherics][registry]") {
    std::set<std::string> keys;
    for (const world::EffectSchema* s : world::effectSchemas()) {
        INFO(s->key);
        CHECK(keys.insert(s->key).second);
        const auto back = world::effectKindFromName(s->key);
        REQUIRE(back.has_value());
        CHECK(*back == s->kind);
        CHECK(std::string_view(world::effectKindName(s->kind)) == s->key);
    }

    SECTION("a name no kind uses is refused rather than defaulted") {
        // The property that stops a file loading as a neighbour. ADR-392 found the resolve doing
        // exactly that -- an unnamed kind fell into the vortex arm -- and a lenient reader here
        // would put the same defect in the file format.
        CHECK_FALSE(world::effectKindFromName("nosuchkind").has_value());
        CHECK_FALSE(world::effectKindFromName("").has_value());
        CHECK_FALSE(world::effectKindFromName("Comet").has_value()); // case matters in a file
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
        wrongType.getFloat = +[](const world::EffectInstance&) { return 0.0f; };
        wrongType.setFloat = +[](world::EffectInstance&, float) {};
        CHECK_FALSE(wrongType.hasAccessors()); // float accessors on a colour row
    }
}

TEST_CASE("a registered path exists for every row, and for no row that is not there",
          "[world][atmospherics][registry][params]") {
    for (const world::EffectKind kind : world::kEffectKinds) {
        const world::EffectSchema& s = schemaFor(kind);
        REQUIRE(s.factory != nullptr);
        world::EffectInstance probe = s.factory("registry probe");
        probe.id = "registry-probe"; // ADR-702: the prefix is the id's, never the display name's
        params::ParameterSet set;
        const world::EffectParameters registered =
            world::registerEffectParameters(set, std::span(&probe, 1));
        const std::string prefix = world::effectParameterPrefix(probe.id);
        REQUIRE(prefix == "fx/registry-probe/");

        // Every declared row produced a parameter...
        for (const world::EffectField& f : s.fields) {
            INFO(prefix + f.leaf);
            CHECK(set.find(prefix + f.leaf) != nullptr);
        }
        // ...every shared row that APPLIES to this type did, and no shared row that does not
        // (ADR-702: a surface wave registers no ground glow nothing would read)...
        std::size_t shared = 0;
        for (const world::EffectField& f : world::sharedEffectFields()) {
            INFO(prefix + f.leaf);
            const bool applies = world::sharedFieldApplies(s, f);
            CHECK((set.find(prefix + f.leaf) != nullptr) == applies);
            shared += applies ? 1u : 0u;
        }
        // ...and the cached pointers line up with the rows one-for-one, which is what
        // `copyParameters` walks positionally.
        REQUIRE(registered.effects.size() == 1);
        CHECK(registered.effects.front().values.size() == s.fields.size() + shared);

        INFO("kind: " << world::effectKindName(kind));
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
    for (const world::EffectKind kind : world::kEffectKinds) {
        const world::EffectSchema& s = schemaFor(kind);
        world::EffectInstance e = world::makeEffect(kind, "round trip"); // the factory, with an id
        REQUIRE(e.id == "round-trip");

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
        const auto back = world::EffectInstance::fromJson(doc);
        INFO("kind: " << world::effectKindName(kind));
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
                REQUIRE(rowJson(doc, f) != nullptr);
                CHECK(rowJson(doc, f)->is_string());
                break;
            }
        }
    }

    SECTION("the round trip can fail") {
        // ADR-182 again, and the thing the document comparison cannot see: a key deleted from the
        // saved file comes back as the factory's value, and this check names it. Deleting the key
        // here is the same event as a row being dropped from the serialiser.
        const world::EffectSchema& s = schemaFor(world::EffectKind::Vortex);
        world::EffectInstance e = world::makeEffect(world::EffectKind::Vortex, "deleted key");
        e.vortex.filaments = 3.5f;
        nlohmann::json doc = e.toJson();
        REQUIRE(doc["parameters"].contains("filaments"));
        doc["parameters"].erase("filaments");
        const auto back = world::EffectInstance::fromJson(doc);
        REQUIRE(back.has_value());
        CHECK(back->vortex.filaments != 3.5f);
    }
}

TEST_CASE("a kind whose numbers live in the store keeps them apart from its neighbours",
          "[world][atmospherics][registry]") {
    // The property that lets a kind be declared in its own file at all: it has no struct on the
    // shared header, so its numbers are keyed by `<kind>/<leaf>` in `EffectInstance::values`.
    // Two kinds' `density` must not be one number. (ADR-230/500 also asked that an effect which
    // CHANGED kind keep what the kind it left was holding. ADR-702 fixed an instance's type --
    // switching is removing one and adding another -- so that half is now the opposite promise: the
    // file carries this type's numbers and no other's.)
    const world::EffectSchema& shower = schemaFor(world::EffectKind::MeteorShower);
    world::EffectInstance e = world::makeEffect(world::EffectKind::MeteorShower, "store");

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
        REQUIRE(doc["parameters"].contains("meteors"));
        CHECK(doc["parameters"]["meteors"].get<float>() == 4.0f);
        const auto back = world::EffectInstance::fromJson(doc);
        REQUIRE(back.has_value());
        CHECK(world::fieldFloat(*meteors, shower, *back) == 4.0f);
    }

    SECTION("the file carries this type's stored numbers and no other type's") {
        // A fog bank's stored rows live under `fog/` in the same store; a shower's file must not
        // carry them. The control is the shower's own row, which it must carry.
        e.values.setFloat("fog/bankLength", 123.0f);
        const nlohmann::json doc = e.toJson();
        CHECK(doc["parameters"].contains("meteors"));
        CHECK_FALSE(doc["parameters"].contains("bankLength"));
        CHECK_FALSE(doc.contains("fog"));
        const auto back = world::EffectInstance::fromJson(doc);
        REQUIRE(back.has_value());
        CHECK(back->values.getFloat("fog/bankLength", -1.0f) == -1.0f);
    }
}

TEST_CASE("a meteor shower resolves as several streaks and a fog bank as one medium",
          "[world][atmospherics][registry][resolve]") {
    // The two proof effects, at the one place where "it is declared" and "it reaches the picture"
    // are different claims. ADR-387's defect was a kind that was declared everywhere and resolved
    // as a neighbour.
    SECTION("the shower puts one record per meteor in the comet bucket") {
        world::EffectInstance e =
            world::makeEffect(world::EffectKind::MeteorShower, "shower");
        e.activation = world::Activation::Always;
        e.timing = world::Timing{};
        e.values.setFloat("meteors/meteors", 4.0f);
        e.values.setFloat("meteors/stagger", 0.0f); // all four in the air at once

        world::EffectContext ctx;
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
            world::EffectInstance e =
                world::makeEffect(world::EffectKind::MeteorShower, "shower");
            e.activation = world::Activation::Always;
            e.timing = world::Timing{};
            e.values.setFloat("meteors/meteors", n);
            e.values.setFloat("meteors/stagger", 0.0f);
            world::EffectContext ctx;
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
        world::EffectInstance e =
            world::makeEffect(world::EffectKind::MeteorShower, "shower");
        e.activation = world::Activation::Always;
        e.timing = world::Timing{};
        const auto resolveAt = [&](double t) {
            world::EffectContext ctx;
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
        world::EffectInstance fog =
            world::makeEffect(world::EffectKind::VolumetricFog, "bank");
        fog.activation = world::Activation::Always;
        fog.timing = world::Timing{};
        world::EffectContext ctx;
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
        std::array<world::EffectInstance, 2> both{
            world::makeEffect(world::EffectKind::Vortex, "funnel"),
            world::makeEffect(world::EffectKind::VolumetricFog, "bank")};
        for (world::EffectInstance& e : both) {
            e.activation = world::Activation::Always;
            e.timing = world::Timing{};
        }
        world::EffectContext ctx;
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
    // cheap to ask every build: a comet, an aurora and a vortex serialise to a document that carries
    // none of the stored rows the fog bank and the meteor shower declared, and round-trip unchanged.
    //
    // ADR-500/563 recorded the opposite consequence here for a while: `toJson` wrote EVERY kind's
    // block, so every effect of every kind carried the fog bank's fifteen stored defaults and the
    // shower's six. ADR-702 writes only an instance's own type, so those numbers are now ABSENT
    // from every other type's file -- and this case checks that by the names of the rows, taken
    // from the two schemas rather than typed here.
    std::vector<const world::EffectField*> foreign;
    for (const world::EffectKind k : {world::EffectKind::VolumetricFog, world::EffectKind::MeteorShower}) {
        for (const world::EffectField& f : schemaFor(k).fields) {
            if (f.stored) {
                foreign.push_back(&f);
            }
        }
    }
    // Counted from the code (`grep -c 'storedFloat("\\|storedChoice("'`): 15 fog + 6 shower.
    REQUIRE(foreign.size() == 21);
    const auto head = [](std::string_view path) {
        return std::string(path.substr(0, path.find('/')));
    };
    for (const world::EffectKind kind : {world::EffectKind::Comet,
                                             world::EffectKind::Aurora,
                                             world::EffectKind::Vortex}) {
        const world::EffectSchema& own = schemaFor(kind);
        const world::EffectInstance e = world::makeEffect(kind, "unchanged");
        const nlohmann::json doc = e.toJson();
        const auto back = world::EffectInstance::fromJson(doc);
        INFO(doc.dump().substr(0, 400));
        REQUIRE(back.has_value());
        CHECK(back->kind == kind);
        CHECK(back->toJson() == doc);
        CHECK_FALSE(doc.contains("fog"));
        CHECK_FALSE(doc.contains("meteors"));
        REQUIRE(doc.contains("parameters"));

        // Every key in `parameters` is one this type's own declaration accounts for: the head of one
        // of its rows' paths, its anchor block, or what its `writeExtra` writes.
        std::set<std::string> accounted;
        for (const world::EffectField& f : own.fields) {
            const std::string_view path = f.jsonPath[0] != '\0' ? f.jsonPath : f.leaf;
            if (!path.starts_with('/')) {
                accounted.insert(head(path));
            }
        }
        if (own.anchorJson != nullptr && own.anchorJson[0] != '/') {
            accounted.insert(head(own.anchorJson));
        }
        if (own.writeExtra != nullptr) {
            nlohmann::json extra = nlohmann::json::object();
            own.writeExtra(e, extra);
            for (const auto& [key, value] : extra.items()) {
                accounted.insert(key);
            }
        }
        for (const auto& [key, value] : doc["parameters"].items()) {
            INFO(world::effectKindName(kind) << " writes parameters/" << key
                                             << ", which none of its own rows declares");
            CHECK(accounted.count(key) == 1);
        }
        // ...and, named, the two new types' stored rows are not among them (a stored row's leaf
        // that happens to spell one of this type's own groups -- the fog's `shape` against the
        // aurora's `shape` block -- is this type's group, and is skipped).
        for (const world::EffectField* f : foreign) {
            if (accounted.count(head(f->jsonPath[0] != '\0' ? f->jsonPath : f->leaf)) == 1) {
                continue;
            }
            INFO(world::effectKindName(kind) << " carries another type's stored row '" << f->leaf << "'");
            CHECK_FALSE(doc["parameters"].contains(f->leaf));
        }
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
        {"id", "authored-shower"},
        {"type", "meteors"},
        {"name", "authored shower"},
        {"owner", {{"kind", "world"}}},
        {"enabled", true},
        {"activation", "always"},
        {"timing", {{"delay", 0.0}, {"lifetime", 0.0}, {"fadeIn", 0.0}, {"fadeOut", 0.0},
                    {"windowStart", 0.0}, {"windowSeconds", 0.0}, {"repeatSeconds", 8.0}}},
        {"ground", {{"mode", "off"}}},
        {"flow", {{"field", ""}}},
        {"parameters", {{"meteors", 6.0}, {"spread", 52.0}, {"stagger", 0.35},
                        {"sizeVariation", 0.5}, {"radiantDrift", 8.0}, {"seed", 3.0}}}};

    const auto loaded = world::EffectInstance::fromJson(doc);
    REQUIRE(loaded.has_value());
    CHECK(loaded->kind == world::EffectKind::MeteorShower);
    CHECK(loaded->values.getFloat("meteors/meteors", -1.0f) == 6.0f);
    CHECK(loaded->values.getFloat("meteors/stagger", -1.0f) == 0.35f);

    world::EffectContext ctx;
    ctx.seconds = 0.6;
    std::array<world::ResolvedAtmospheric, world::kMaxGpuComets> comets{};
    std::array<world::ResolvedAtmospheric, world::kMaxGpuAuroras> auroras{};
    world::EffectInstance live = *loaded;
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
        const auto e2 = world::EffectInstance::fromJson(later);
        REQUIRE(e2.has_value());
        CHECK(e2->comet.path.travelSeconds == 2.2f);
        CHECK(e2->comet.appearance.coreIntensity == 40.0f);

        world::EffectInstance live2 = *e2;
        world::EffectContext ctx2;
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
TEST_CASE("no row is drawn under a section header the panel never emitted",
          "[world][atmospherics][registry]") {
    // **The panel emits a section header only on the row that declares one, and it filters by page
    // FIRST.** `drawSchemaRows` is `if (field.page != page) continue;` then
    // `if (field.section[0]) SeparatorText(...)`. So if a section is declared by a row on one page
    // and some LATER row of that same section sits on the other page, that row is drawn with no
    // header of its own -- appended under whatever separator happened to precede it on that page,
    // which is a different section's, or none.
    //
    // It is invisible in the source, where the rows read as one tidy list, and invisible in a
    // screenshot unless you already know which section a row was meant to be under. It is one
    // `.main()` away at all times: moving a row between pages is the most ordinary edit there is
    // and it silently re-parents every row of its section that stayed behind.
    //
    // **The first version of this check asserted that the first row on each page declares a
    // section, and it was wrong** -- it went red on five of six shipped kinds. That is not a
    // defect, it is the family's deliberate convention: a few ungrouped rows first (a comet's
    // colours, a vortex's density and radius) and named sections after them. A guard that fires on
    // working code is not a guard, which is the lesson the kind-name check twenty lines up was
    // bought with. The property below is the narrow one that is actually true and actually
    // load-bearing: a row may have no section, but it may not have one the panel did not draw.
    for (const world::EffectSchema* schema : world::effectSchemas()) {
        // Each row's owning section is the last one declared at or before it in list order, which
        // is exactly how the panel accumulates them.
        std::string owner;
        std::vector<std::pair<const world::EffectField*, std::string>> owned;
        std::map<std::string, std::set<world::FieldPage>> declaredOn;
        for (const world::EffectField& f : schema->fields) {
            if (f.section[0] != '\0') {
                owner = f.section;
                declaredOn[owner].insert(f.page);
            }
            owned.emplace_back(&f, owner);
        }
        for (const auto& [field, section] : owned) {
            if (section.empty()) {
                continue; // an ungrouped row is drawn ungrouped, which is the convention
            }
            const auto it = declaredOn.find(section);
            const bool drawnHere = it != declaredOn.end() && it->second.count(field->page) == 1;
            INFO(std::string(schema->key) + ": row '" + field->leaf + "' belongs to section '" +
                 section + "', which no row on its own page declares -- the panel will draw it "
                 "under a different section's header, or under none");
            CHECK(drawnHere);
        }
    }
}

TEST_CASE("a kind keeps the leaves its authored scenes and panels already name",
          "[world][atmospherics][registry]") {
    struct Expect {
        world::EffectKind kind;
        std::vector<std::string> leaves;
    };
    const std::vector<Expect> promised{
        // ADR-461, Vortex 2.0 §7-§11: the eye, the bands, and the noise weight that §5's
        // diagnostic render turns off. `innerVoid` is the eye's radius and there is deliberately
        // no second `eyeRadius` beside it.
        {world::EffectKind::Vortex,
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
            INFO(std::string(world::effectKindName(want.kind)) + " must declare '" + leaf + "'");
            CHECK(have.count(leaf) == 1);
        }

        // The probe can fail (ADR-182): a name that is not there is reported as missing rather
        // than passing because the loop found nothing to check.
        CHECK(have.count("eyeRadius") == 0);
        CHECK(have.count("notAField") == 0);
    }
}

TEST_CASE("no panel page draws the same section heading twice", "[world][registry][panels]") {
    // ADR-579, the brief's §36. `EffectField::sec` marks a row as the START of a section, and
    // `drawSchemaRows` emits an `ImGui::SeparatorText` whenever it meets one -- **on the page it
    // is drawing**. So two rows carrying the same section name, with other rows of the same page
    // between them, draw that heading twice with unrelated controls under each.
    //
    // It is a panel defect with no visual test and no compile error, and it has now happened
    // twice: `agent/tornado`'s section guard found one instance in the fog kind (ADR-566 fixed
    // it), and enumerating the rows again for §36 found another that the first fix did not cover.
    // **A defect that recurs in the same file after being fixed once is a defect that needs a
    // check rather than a fix.**
    //
    // The check is per KIND and per PAGE, because a name reused across pages is correct -- each
    // page draws its own copy and an artist sees one of them.
    for (const world::EffectKind kind : world::kEffectKinds) {
        const world::EffectSchema* s = world::effectSchema(kind);
        if (s == nullptr) {
            continue;
        }
        for (const world::FieldPage page :
             {world::FieldPage::Main, world::FieldPage::Advanced, world::FieldPage::Hidden}) {
            std::vector<std::string> seen;
            for (const world::EffectField& f : s->fields) {
                if (f.page != page || f.section[0] == '\0') {
                    continue;
                }
                const std::string name = f.section;
                INFO("kind " << world::effectKindName(kind) << ", page "
                     << static_cast<int>(page) << ", section '" << name << "' at row '" << f.leaf
                     << "'");
                CHECK(std::find(seen.begin(), seen.end(), name) == seen.end());
                seen.push_back(name);
            }
        }
    }
}
