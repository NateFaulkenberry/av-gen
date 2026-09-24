// The atmospheric effect family's contract, checked per kind.
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
#include "ui/world_effects_panel.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_conformance.hpp"
#include "world/effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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
    const fs::path header = fs::path(AVGEN_SOURCE_DIR) / "src" / "world" / "atmospherics.hpp";
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
        if ((std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_') {
            token.push_back(c);
            continue;
        }
        if (c == ',' || c == '\n') {
            // An enumerator is the token that ends a comma-or-newline-separated item, and an
            // explicit `= 3` would leave the number as the token -- which is why a token that does
            // not start with a letter is dropped rather than recorded.
            if (!token.empty() && (std::isalpha(static_cast<unsigned char>(token.front())) != 0)) {
                names.push_back(token);
            }
            token.clear();
            continue;
        }
        token.clear(); // '=' and friends: whatever was accumulating is not an enumerator
    }
    if (!token.empty() && (std::isalpha(static_cast<unsigned char>(token.front())) != 0)) {
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
        INFO("EffectKind::" << n << " is declared in atmospherics.hpp but no schema claims it -- "
             "add a line to builtinSchemas() in effect_registry.cpp");
        CHECK(claimed.count(n) == 1);
    }
    for (const std::string& n : claimed) {
        INFO("a schema claims EffectKind::" << n << ", which atmospherics.hpp does not declare");
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

TEST_CASE("every atmospheric kind conforms to the family's contract",
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
                                                       "windowSeconds", "groundIntensity"};
        const conf::Report good = conf::checkLeavesExist(kind, kShared, "shared");
        INFO(good.summary());
        CHECK(good.clean());
    }
}

TEST_CASE("the beat-response target is a parameter of the kind it is chosen for",
          "[world][atmospherics][conformance][ui]") {
    // `ui::atmosphericBeatTarget` is the one kind switch in the UI that is exhaustive and has no
    // `default`. It is checked here anyway, through the same mechanism as everything else, because
    // being right today is not the same as being checked -- ADR-385.
    for (const world::EffectKind kind : conf::kEffectKinds) {
        const world::EffectInstance probe = conf::probeEffect(kind, "conformance probe");
        const std::string target = ui::atmosphericBeatTarget(probe.name, kind);
        const std::string prefix = world::effectParameterPrefix(probe.name);
        REQUIRE(target.starts_with(prefix));

        const std::string_view leaf(target.data() + prefix.size(), target.size() - prefix.size());
        const conf::Report r = conf::checkLeavesExist(kind, std::span(&leaf, 1), "beat-target");
        INFO("kind: " << world::effectKindName(kind) << ", target: " << target);
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
        REQUIRE(leaves.size() > 10);

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
        REQUIRE(paths.size() > 20);

        const std::string prefix = world::effectParameterPrefix(probe.name);
        for (const std::string& p : paths) {
            INFO(p);
            CHECK(p.starts_with(prefix));
        }
        // A name is half of a path, so two effects of a kind must not collide.
        const std::set<std::string> unique(paths.begin(), paths.end());
        CHECK(unique.size() == paths.size());
    }
}
