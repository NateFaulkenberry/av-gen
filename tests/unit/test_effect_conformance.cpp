// The effect registry's contract, checked per type -- since ADR-702 every type, the surface waves
// (Ground Pulse, Travel Beam) as well as ADR-230's sky and medium kinds.
//
// ADR-387 established that ADR-230's family is a table-driven authoring interface and that adding
// two `constexpr` tables buys registration, modulation, apply, capture and a panel row with no
// bespoke code. That is true of three directions. Five more lists -- `toJson`, `fromJson`,
// `sanitise`, the styles and `defaultEffectRoutes` -- plus the panel's string literals are
// still per-kind and hand-written, and none of them fails to compile when a kind is forgotten.
//
// So the family's contract is stated here as a check rather than as prose, and the check obtains
// every path the way the engine obtains it (ADR-382 generalised). Every probe below is shown
// capable of failing (ADR-182).

#include "labs/case.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_conformance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace avgen;
namespace conf = avgen::world::conformance;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The enumerators of `enum class EffectKind`, read out of the header.
//
// This is the guard that actually holds, and it is worth saying why it is not a `static_assert`.
// `conformance::effectKindIndex` has an exhaustive switch with no `default`, so a new
// enumerator is a `-Wswitch` diagnostic -- but `-Werror` is behind `AVGEN_WARNINGS_AS_ERRORS` and
// off in this build, and a warning in a five-thousand-line build log is not a guard. The enum has
// no reflection and no sentinel. Reading the declaration is the only thing left that fails by name,
// and `test_lab_registry` already establishes that a test in this repository may read the source
// tree to check a claim the source tree makes about itself.
std::vector<std::string> declaredKindNames() {
    // ADR-702 moved the enumeration out of atmospherics.hpp into its own header.
    const fs::path header = fs::path(AVGEN_SOURCE_DIR) / "src" / "world" / "effects" / "effect_kind.hpp";
    std::ifstream in(header);
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    const std::size_t decl = text.find("enum class EffectKind");
    REQUIRE(decl != std::string::npos);
    const std::size_t open = text.find('{', decl);
    REQUIRE(open != std::string::npos);
    // The end of the enum is the first `}` **that is not inside a comment**, and the distinction
    // was bought by a failure rather than anticipated.
    //
    // This was `text.find('}', open)`. ADR-580's `Tornado` carries a comment naming the files that
    // implement it -- `core/tornado.{hpp,cpp}` -- and that brace ended the enum three lines early,
    // so the header appeared to declare six kinds when it declares seven. The test then failed
    // with "a schema claims EffectKind::Tornado, which atmospherics.hpp does not declare",
    // which is a true statement about what the parser saw and a badly misleading one about what
    // was wrong: the enumerator was there, correct, and two lines below where the scan stopped.
    //
    // It is worth more than the fix. This guard exists so that a kind added and not finished
    // wiring is NAMED, and its failure mode was to silently shorten the list it checks against --
    // a guard that can be switched off by a punctuation mark in a comment. Rewording the comment
    // would have made the symptom go away and left the next person to find it again.
    std::size_t close = std::string::npos;
    for (std::size_t i = open + 1; i + 1 < text.size(); ++i) {
        if (text[i] == '/' && text[i + 1] == '/') {
            const std::size_t eol = text.find('\n', i);
            if (eol == std::string::npos) {
                break;
            }
            i = eol;
            continue;
        }
        if (text[i] == '}') {
            close = i;
            break;
        }
    }
    REQUIRE(close != std::string::npos);

    std::vector<std::string> names;
    std::string token;
    bool inLineComment = false;
    bool skippingValue = false;
    for (std::size_t i = open + 1; i < close; ++i) {
        const char c = text[i];
        if (inLineComment) {
            if (c == '\n') {
                inLineComment = false;
            }
            continue;
        }
        if (c == '/' && i + 1 < close && text[i + 1] == '/') {
            inLineComment = true;
            ++i;
            continue;
        }
        // ADR-703: an enumerator may carry an explicit value (`SpaceWarp = 12,`), which the Wave 1
        // reservation requires so a type's number never depends on merge order. Its NAME is the
        // token before the '=', and the value up to the next ',' is skipped. Before this, the space
        // before the '=' cleared the token and every explicitly numbered kind was invisible here.
        if (skippingValue) {
            if (c == ',') {
                skippingValue = false;
            }
            continue;
        }
        if (c == '=') {
            if (!token.empty() && (std::isalpha(static_cast<unsigned char>(token.front())) != 0)) {
                names.push_back(token);
            }
            token.clear();
            skippingValue = true;
            continue;
        }
        if (c == ' ' || c == '\t') {
            continue; // whitespace inside an item neither ends nor starts a name
        }
        if ((std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_') {
            token.push_back(c);
            continue;
        }
        if (c == ',' || c == '\n') {
            // An enumerator is the token that ends a comma-or-newline-separated item.
            if (!token.empty() && (std::isalpha(static_cast<unsigned char>(token.front())) != 0)) {
                names.push_back(token);
            }
            token.clear();
            continue;
        }
        token.clear(); // anything else: whatever was accumulating is not an enumerator
    }
    if (!skippingValue && !token.empty() && (std::isalpha(static_cast<unsigned char>(token.front())) != 0)) {
        names.push_back(token);
    }
    return names;
}

} // namespace

TEST_CASE("the registry covers every kind the enum declares", "[world][atmospherics][conformance]") {
    const std::vector<std::string> declared = declaredKindNames();
    REQUIRE_FALSE(declared.empty());

    // ADR-500 changed how the two sides are matched, and the change was bought by a failure.
    //
    // ADR-392 lowercased the enumerator and compared it with `effectKindName`. That worked for
    // three kinds because "Comet" lowercases to "comet" **by coincidence**: an enumerator's
    // spelling and a kind's file format are two different decisions, and the first kind for which
    // they differed -- `MeteorShower`, which serialises as "meteors" -- failed this test while
    // being entirely correct. A guard that fires on working code is not a guard.
    //
    // So the mapping is declared: `EffectSchema::enumName` says which enumerator a schema claims,
    // and the two sets are compared by that. The property that matters is unchanged and is in fact
    // stronger -- an enumerator with no schema and a schema claiming an enumerator that no longer
    // exists are now both named -- and `checkRegistry` catches an empty or duplicate `enumName`.
    std::set<std::string> fromHeader(declared.begin(), declared.end());
    std::set<std::string> claimed;
    for (const world::EffectSchema* schema : world::effectSchemas()) {
        claimed.insert(schema->enumName);
    }

    // Printed rather than counted, because the point of this failing is that it names the kind
    // somebody added and did not finish wiring.
    for (const std::string& n : fromHeader) {
        INFO("EffectKind::" << n << " is declared in effect_kind.hpp but no schema claims it -- "
             "add a line to builtinSchemas() in effect_registry.cpp");
        CHECK(claimed.count(n) == 1);
    }
    for (const std::string& n : claimed) {
        INFO("a schema claims EffectKind::" << n << ", which effect_kind.hpp does not declare");
        CHECK(fromHeader.count(n) == 1);
    }
    // ...and the array the exhaustive switches are indexed by, which is the third place a kind has
    // to appear and the one a `-Wswitch` warning nobody reads would otherwise be the only guard for.
    INFO("kEffectKinds has " << conf::kEffectKinds.size() << " entries for "
         << declared.size() << " enumerators");
    CHECK(conf::kEffectKinds.size() == declared.size());

    SECTION("the reader can fail") {
        // The control ADR-182 asks for: the parser is shown finding what is there, so an empty
        // result above would be a broken parser rather than an empty enum.
        CHECK(fromHeader.count("Comet") == 1);
        CHECK(fromHeader.count("Aurora") == 1);
        CHECK(fromHeader.count("Vortex") == 1);
        // ADR-702's two surface waves, the last two enumerators -- the ones a scan that stopped
        // early would miss first.
        CHECK(fromHeader.count("GroundPulse") == 1);
        CHECK(fromHeader.count("TravelBeam") == 1);
        CHECK(fromHeader.count("NoSuchKind") == 0);
    }
}

TEST_CASE("the registry's own contract holds for every declared kind",
          "[world][atmospherics][conformance][registry]") {
    // ADR-500's replacement for "a forgotten piece is silent". Every finding names the kind and the
    // row, so a half-declared effect fails here rather than drawing an empty box.
    const std::vector<world::RegistryFinding> findings = world::checkRegistry();
    std::string summary;
    for (const world::RegistryFinding& f : findings) {
        summary += f.subject + " [" + f.rule + "] " + f.detail + "\n";
    }
    INFO(summary);
    CHECK(findings.empty());
}

TEST_CASE("every effect type conforms to the family's contract",
          "[world][atmospherics][conformance][params]") {
    for (const world::EffectKind kind : conf::kEffectKinds) {
        const conf::Report r = conf::checkAtmospheric(kind);
        INFO("kind: " << world::effectKindName(kind));
        INFO(r.summary());
        CHECK(r.clean());
    }
}

TEST_CASE("the family report is the union of the per-kind reports",
          "[world][atmospherics][conformance]") {
    const conf::Report all = conf::checkAtmosphericFamily();
    INFO(all.summary());
    CHECK(all.clean());
    CHECK(all.summary().empty());
}

TEST_CASE("a leaf check reports a leaf that is not there, and passes one that is",
          "[world][atmospherics][conformance]") {
    // ADR-182: the probe every panel row table is held to, shown failing. Without this, "the rows
    // are fine" and "the checker is inert" look identical.
    for (const world::EffectKind kind : conf::kEffectKinds) {
        INFO("kind: " << world::effectKindName(kind));

        static constexpr std::string_view kMissing[] = {"noSuchLeafOnAnyEffect"};
        const conf::Report bad = conf::checkLeavesExist(kind, kMissing, "control");
        CHECK(bad.findings.size() == 1);
        CHECK_FALSE(bad.clean());

        // `enabled` and the shared lifetime rows are registered for every kind, whatever it is.
        static constexpr std::string_view kShared[] = {"enabled", "delay", "lifetime", "fadeIn",
                                                       "fadeOut", "repeat", "windowStart",
                                                       "windowSeconds"};
        const conf::Report good = conf::checkLeavesExist(kind, kShared, "shared");
        INFO(good.summary());
        CHECK(good.clean());

        // ADR-702: the ground-glow rows only for a type that lights the ground. A surface wave
        // registering `groundIntensity` would be a slider that moves nothing.
        static constexpr std::string_view kGround[] = {"groundIntensity"};
        const world::EffectSchema* schema = world::effectSchema(kind);
        REQUIRE(schema != nullptr);
        const conf::Report ground = conf::checkLeavesExist(kind, kGround, "ground");
        // ADR-703: only the sky and medium types register the shared ground rows.
        CHECK(ground.clean() == world::isAtmosphericBucket(schema->resolve.bucket));
    }
}

TEST_CASE("the beat-response target is a parameter of the kind it is chosen for",
          "[world][atmospherics][conformance][ui]") {
    // The Beat response control drives `EffectSchema::beatLeaf`. Before ADR-702 the panel computed
    // the target with a kind switch of its own (`ui::atmosphericBeatTarget`); the schema declares it
    // now, and it is checked through the registrar -- the same mechanism as everything else --
    // because being right today is not the same as being checked (ADR-385).
    for (const world::EffectKind kind : conf::kEffectKinds) {
        const world::EffectSchema* schema = world::effectSchema(kind);
        REQUIRE(schema != nullptr);
        INFO("kind: " << world::effectKindName(kind));
        REQUIRE(schema->beatLeaf[0] != '\0');
        const std::string_view leaf(schema->beatLeaf);
        const conf::Report r = conf::checkLeavesExist(kind, std::span(&leaf, 1), "beat-target");
        INFO(r.summary());
        CHECK(r.clean());
    }
}

TEST_CASE("every parameter a kind registers is a row the panel can draw",
          "[world][atmospherics][conformance][ui]") {
    // ADR-500 turned this question round, and the reason is ADR-182.
    //
    // ADR-392's version asked "is every row the panel draws a parameter that kind has". That was
    // the right question while the rows were a SECOND list in `ui_logic.hpp` that a five-character
    // typo could break silently. The panel now walks `EffectSchema::fields` directly, so the row
    // and the registration are the same declaration and the old question cannot fail -- which
    // makes it a probe that proves nothing.
    //
    // The question that can still fail is the inverse, and it is also the owner's standing rule:
    // if it is in the picture an artist must be able to find it and change it. Every path the
    // REGISTRAR writes must correspond to a row the panel will draw. A row marked `Hidden`, a row
    // with no label, or a shared row the panel forgets fails here, by path.
    for (const world::EffectKind kind : conf::kEffectKinds) {
        const world::EffectSchema* schema = world::effectSchema(kind);
        REQUIRE(schema != nullptr);
        const world::EffectInstance probe = conf::probeEffect(kind, "conformance probe");
        const std::vector<std::string> leaves = conf::registeredLeaves(probe);
        // Every row, plus `enabled` and the seven timing rows every type registers. ADR-703: a
        // fixed "more than 10" stopped being true of the family when a one-row type (Bloom Source)
        // arrived; this is the same guard -- the registrar produced a real set -- per type.
        REQUIRE(leaves.size() >= schema->fields.size() + 8);

        for (const std::string& leaf : leaves) {
            if (leaf == "enabled") {
                continue; // the header's own checkbox, drawn beside the effect's name
            }
            const world::EffectField* row = nullptr;
            for (const world::EffectField& f : schema->fields) {
                if (leaf == f.leaf) { row = &f; }
            }
            for (const world::EffectField& f : world::sharedEffectFields()) {
                if (leaf == f.leaf) { row = &f; }
            }
            INFO("kind: " << world::effectKindName(kind) << ", leaf: " << leaf);
            REQUIRE(row != nullptr);
            // A registered parameter with no label is one an artist cannot identify, and one on
            // the Hidden page is one they cannot reach. Both are ADR-375's defect.
            CHECK(row->label[0] != '\0');
            CHECK(row->page != world::FieldPage::Hidden);
        }
    }

    SECTION("the check can fail") {
        // ADR-182. A leaf no kind declares is reported by the same machinery the panel's rows go
        // through, which is what shows the comparison above is not vacuous.
        const std::array<std::string_view, 2> bogus{"noSuchLeaf", "coreIntensity"};
        const conf::Report r =
            conf::checkLeavesExist(world::EffectKind::Comet, bogus, "panel-rows");
        CHECK_FALSE(r.clean());
        CHECK(r.findings.size() == 1); // the second one is real
    }

    SECTION("a kind that declares no hue cycle registers none of its leaves") {
        // The other half of the same claim, and the reason the panel does not draw a rainbow for a
        // vortex: a vortex declares no rainbow rows at all, so these leaves find nothing. ADR-375's
        // lesson the other way round -- a control that draws and does nothing.
        const std::array<std::string_view, 5> rainbow{"rainbowSpeed", "rainbowScale", "rainbowHue",
                                                      "rainbowSaturation", "rainbowBrightness"};
        for (const world::EffectKind kind :
             {world::EffectKind::Vortex, world::EffectKind::VolumetricFog}) {
            const conf::Report r = conf::checkLeavesExist(kind, rainbow, "panel-rows");
            INFO("kind: " << world::effectKindName(kind));
            CHECK_FALSE(r.clean());
            CHECK(r.findings.size() == rainbow.size());
        }
        // ...and the two sky kinds that DO declare it register all five.
        for (const world::EffectKind kind :
             {world::EffectKind::Comet, world::EffectKind::Aurora}) {
            const conf::Report r = conf::checkLeavesExist(kind, rainbow, "panel-rows");
            INFO("kind: " << world::effectKindName(kind) << "\n" << r.summary());
            CHECK(r.clean());
        }
    }
}

TEST_CASE("registered paths are read back from the registrar, not from the tables",
          "[world][atmospherics][conformance][params]") {
    // The property that makes the rest of this file mean anything: the path set comes from the
    // call the engine makes. If `registerEffectParameters` stopped writing a row, every check
    // above would notice, because none of them has a second source of truth to agree with.
    for (const world::EffectKind kind : conf::kEffectKinds) {
        const world::EffectInstance probe = conf::probeEffect(kind, "conformance probe");
        const std::vector<std::string> paths = conf::registeredPaths(probe);
        const std::vector<std::string> leaves = conf::registeredLeaves(probe);
        INFO("kind: " << world::effectKindName(kind));
        REQUIRE(paths.size() == leaves.size());
        // Every row plus `enabled` and the seven timing rows (ADR-703: per type, not "more than 20",
        // which a small lane type is not and need not be).
        REQUIRE(paths.size() >= world::effectSchema(kind)->fields.size() + 8);

        // ADR-702: keyed by the instance's id, never its display name.
        const std::string prefix = world::effectParameterPrefix(probe.id);
        REQUIRE(prefix == "fx/" + probe.id + "/");
        for (const std::string& p : paths) {
            INFO(p);
            CHECK(p.starts_with(prefix));
        }
        // An id is half of a path, so two rows of a kind must not collide.
        const std::set<std::string> unique(paths.begin(), paths.end());
        CHECK(unique.size() == paths.size());
    }
}

// ---- ADR-702: what a type must declare to be attached to anything -------------------------------
//
// Each case below holds TWO declarations against each other, never a schema against itself. The
// targets are held against the ADR's own table, written out here; the stage against the bucket
// through a mapping written out here (not `checkRegistry`'s); the endpoint accessors against a
// probe that sets a value and reads it back.

TEST_CASE("every type is attachable to what ADR-702 says, and to nothing else",
          "[effects][conformance][targets]") {
    using world::EffectKind;
    using world::EffectTarget;
    // ADR-702's attachment table. A change to a type's targets is a design decision, and editing
    // this table is how it is said out loud.
    const std::map<EffectKind, std::set<EffectTarget>> expected{
        {EffectKind::Comet, {EffectTarget::World}},
        {EffectKind::Aurora, {EffectTarget::World}},
        {EffectKind::Vortex, {EffectTarget::World}},
        {EffectKind::MeteorShower, {EffectTarget::World}},
        {EffectKind::VolumetricFog, {EffectTarget::World}},
        {EffectKind::Tornado, {EffectTarget::World}},
        {EffectKind::GroundPulse, {EffectTarget::Entity, EffectTarget::World}},
        {EffectKind::TravelBeam, {EffectTarget::World, EffectTarget::Camera}},
        // ADR-703 (FXL). Pulse is Entity only: its Light-owner form needs LIGHTMOD's modulate half.
        {EffectKind::Glow, {EffectTarget::Entity}},
        {EffectKind::Pulse, {EffectTarget::Entity}},
        {EffectKind::BloomSource, {EffectTarget::Entity}},
        {EffectKind::SpaceWarp, {EffectTarget::Entity, EffectTarget::World}}, // ADR-703 (DF)
        {EffectKind::ParticleEmitter, {EffectTarget::Entity, EffectTarget::World}}, // ADR-703
        {EffectKind::Trail, {EffectTarget::Entity}},
        // Wave 2 (XFORM). Entity only: the catalog's Light owners need LIGHTMOD to move a light
        // record, and a Camera orbit or shake is the camera rig's and ADR-098's.
        {EffectKind::Orbit, {EffectTarget::Entity}},
        {EffectKind::Spiral, {EffectTarget::Entity}},
        {EffectKind::Float, {EffectTarget::Entity}},
        {EffectKind::Shake, {EffectTarget::Entity}},
        {EffectKind::Bounce, {EffectTarget::Entity}},
        // Wave 2 (TRIGGER + DF). A front or a membrane can stand at a World point; a wake needs an
        // owner that moves.
        {EffectKind::Shockwave, {EffectTarget::Entity, EffectTarget::World}},
        {EffectKind::Ripple, {EffectTarget::Entity, EffectTarget::World}},
        // Wave 2 (FXL surface). Entity only: a lane is a per-draw change of an owner's surface. The
        // catalog's World preset of Bioluminescence (the recipe ladder) and a Light owner's Rim Light
        // are later waves.
        {EffectKind::Dissolve, {EffectTarget::Entity}},
        {EffectKind::Growth, {EffectTarget::Entity}},
        {EffectKind::Breathing, {EffectTarget::Entity}},
        {EffectKind::OrganicPulsation, {EffectTarget::Entity}},
        {EffectKind::Bioluminescence, {EffectTarget::Entity}},
        {EffectKind::PulsingVeins, {EffectTarget::Entity}},
        {EffectKind::Fresnel, {EffectTarget::Entity}},
        {EffectKind::RimLight, {EffectTarget::Entity}},
        {EffectKind::ColorCycling, {EffectTarget::Entity}},
        {EffectKind::VelocityDistortion, {EffectTarget::Entity}},
        {EffectKind::Stars, {EffectTarget::World}}, // Wave 2: the sky has one star field
        {EffectKind::MotionSmear, {EffectTarget::Entity}},
        // Wave 3 (DF): a hot column or a mass can stand at a World point or ride an entity.
        {EffectKind::HeatShimmer, {EffectTarget::Entity, EffectTarget::World}},
        {EffectKind::GravitationalLens, {EffectTarget::Entity, EffectTarget::World}},
    };
    REQUIRE(expected.size() == conf::kEffectKinds.size());
    for (const EffectKind kind : conf::kEffectKinds) {
        const world::EffectSchema* schema = world::effectSchema(kind);
        REQUIRE(schema != nullptr);
        INFO("type: " << schema->key);
        REQUIRE(expected.count(kind) == 1);
        // At least one target, or the Add Effect menu offers it nowhere and no file can hold it.
        CHECK(schema->targets != 0);
        for (std::size_t t = 0; t < world::kEffectTargetCount; ++t) {
            const auto target = static_cast<EffectTarget>(t);
            INFO("target: " << world::effectTargetName(target));
            const bool want = expected.at(kind).count(target) == 1;
            CHECK(world::effectAllowedOn(kind, target) == want);
            // ...and the Add Effect menu, which lists by the same rule, agrees.
            const std::vector<EffectKind> menu = world::effectKindsFor(target);
            CHECK((std::find(menu.begin(), menu.end(), kind) != menu.end()) == want);
        }
    }

    SECTION("the check can fail") {
        // ADR-182: an owner the type does not support is refused by the stack, so the table above
        // is compared against behaviour that exists.
        std::vector<world::EffectInstance> effects;
        CHECK_FALSE(world::addEffect(effects, world::EffectOwner::entity("rook"), EffectKind::Tornado));
        CHECK(effects.empty());
        CHECK(world::addEffect(effects, world::EffectOwner::entity("rook"), EffectKind::GroundPulse));
    }
}

TEST_CASE("every type's render stage is the stage its bucket is drawn at",
          "[effects][conformance][stage]") {
    // The stage is a promise to the renderer; the bucket is the integrator that keeps it. They are
    // declared separately, so they are held against each other here -- through this file's own
    // mapping of which pass draws which bucket, which is written from the renderer's frame order
    // (surface term in the lit pass, sky after the opaque pass, media in the volumetric march).
    const auto drawnAt = [](world::EffectBucket b) {
        switch (b) {
        case world::EffectBucket::Surface: return world::RenderStage::Material;
        case world::EffectBucket::Comet: return world::RenderStage::Sky;
        case world::EffectBucket::Aurora: return world::RenderStage::Sky;
        case world::EffectBucket::Medium: return world::RenderStage::Volumetric;
        // ADR-703's own-builder buckets: lanes in the lit pass, ribbons and emitters beside the
        // particles, DF after the volumetric composite.
        case world::EffectBucket::EntityLanes: return world::RenderStage::Material;
        case world::EffectBucket::Ribbon: return world::RenderStage::Particles;
        case world::EffectBucket::Distortion: return world::RenderStage::ScreenSpace;
        case world::EffectBucket::Emitter: return world::RenderStage::Particles;
        // Wave 2: XFORM offsets are composed by the flatten, before any pass draws.
        case world::EffectBucket::Transform: return world::RenderStage::Geometry;
        case world::EffectBucket::Starfield: return world::RenderStage::Sky;
        }
        return world::RenderStage::PostProcess; // unreachable for a real bucket, and wrong for all
    };
    std::set<world::RenderStage> used;
    for (const world::EffectKind kind : conf::kEffectKinds) {
        const world::EffectSchema* schema = world::effectSchema(kind);
        REQUIRE(schema != nullptr);
        INFO("type: " << schema->key << ", stage " << world::renderStageName(schema->stage));
        CHECK(schema->stage == drawnAt(schema->resolve.bucket));
        used.insert(schema->stage);
    }
    // The control: the family spans at least three stages, so the loop compared distinct answers
    // rather than one stage with itself. (At least: each Wave 1 type adds its own.)
    CHECK(used.size() >= 3);

    SECTION("and the evaluator walks stages in frame order") {
        // One instance of every type, listed in REVERSE registry order; the evaluation order must
        // come back sorted by stage whatever order the list is in.
        std::vector<world::EffectInstance> effects;
        for (auto it = conf::kEffectKinds.rbegin(); it != conf::kEffectKinds.rend(); ++it) {
            world::EffectInstance e = conf::probeEffect(*it, "probe");
            e.id = std::string(world::effectKindName(*it));
            effects.push_back(std::move(e));
        }
        std::vector<std::uint32_t> order;
        world::effectEvaluationOrder(effects, order);
        REQUIRE(order.size() == effects.size());
        for (std::size_t i = 1; i < order.size(); ++i) {
            const auto before = world::effectSchema(effects[order[i - 1]].kind)->stage;
            const auto after = world::effectSchema(effects[order[i]].kind)->stage;
            INFO(effects[order[i - 1]].id << " then " << effects[order[i]].id);
            CHECK(static_cast<int>(before) <= static_cast<int>(after));
        }
    }
}

TEST_CASE("a type's endpoint accessors are complete and actually reach the instance",
          "[effects][conformance][endpoint]") {
    // `checkRegistry` asks that the pointers come in whole sets. This asks the other half: that a
    // set declared is a set that WORKS -- a value written through `setSource`/`setTarget` is the
    // value `getSource`/`getTarget`/`hasTarget` read back, and it survives the file.
    std::size_t withEndpoints = 0;
    for (const world::EffectKind kind : conf::kEffectKinds) {
        const world::EffectSchema* s = world::effectSchema(kind);
        REQUIRE(s != nullptr);
        INFO("type: " << s->key);
        CHECK((s->getSource == nullptr) == (s->setSource == nullptr));
        CHECK((s->getTarget == nullptr) == (s->setTarget == nullptr));
        CHECK((s->getTarget == nullptr) == (s->hasTarget == nullptr));
        if (s->getSource == nullptr) {
            continue;
        }
        ++withEndpoints;
        world::EffectInstance e = conf::probeEffect(kind, "endpoint probe");
        world::EffectEndpoint src;
        src.kind = world::SourceKind::Node;
        src.name = "altar";
        src.groundOffset = 1.25f;
        s->setSource(e, src);
        CHECK(s->getSource(e).kind == world::SourceKind::Node);
        CHECK(s->getSource(e).name == "altar");
        if (s->setTarget != nullptr) {
            world::EffectEndpoint dst;
            dst.kind = world::SourceKind::Hero;
            dst.name = "rook";
            s->setTarget(e, true, dst);
            CHECK(s->hasTarget(e));
            CHECK(s->getTarget(e).name == "rook");
        }
        const auto back = world::EffectInstance::fromJson(e.toJson());
        REQUIRE(back.has_value());
        CHECK(s->getSource(*back).kind == world::SourceKind::Node);
        CHECK(s->getSource(*back).name == "altar");
        CHECK(s->getSource(*back).groundOffset == 1.25f);
        if (s->getTarget != nullptr) {
            CHECK(s->hasTarget(*back));
            CHECK(s->getTarget(*back).kind == world::SourceKind::Hero);
            CHECK(s->getTarget(*back).name == "rook");
        }
    }
    // The control: the two surface waves declare endpoints, so the loop body ran.
    CHECK(withEndpoints == 2);
}
