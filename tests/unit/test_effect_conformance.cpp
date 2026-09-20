// The atmospheric effect family's contract, checked per kind.
//
// ADR-387 established that ADR-230's family is a table-driven authoring interface and that adding
// two `constexpr` tables buys registration, modulation, apply, capture and a panel row with no
// bespoke code. That is true of three directions. Five more lists -- `toJson`, `fromJson`,
// `sanitise`, the styles and `defaultAtmosphericRoutes` -- plus the panel's string literals are
// still per-kind and hand-written, and none of them fails to compile when a kind is forgotten.
//
// So the family's contract is stated here as a check rather than as prose, and the check obtains
// every path the way the engine obtains it (ADR-382 generalised). Every probe below is shown
// capable of failing (ADR-182).

#include "labs/case.hpp"
#include "ui/ui_logic.hpp"
#include "ui/world_effects_panel.hpp"
#include "world/atmospheric_params.hpp"
#include "world/world_effects/effect_conformance.hpp"

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

// The enumerators of `enum class AtmosphereKind`, read out of the header.
//
// This is the guard that actually holds, and it is worth saying why it is not a `static_assert`.
// `conformance::atmosphereKindIndex` has an exhaustive switch with no `default`, so a new
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

    const std::size_t decl = text.find("enum class AtmosphereKind");
    REQUIRE(decl != std::string::npos);
    const std::size_t open = text.find('{', decl);
    const std::size_t close = text.find('}', open);
    REQUIRE(open != std::string::npos);
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

TEST_CASE("the conformance table covers every kind the enum declares",
          "[world][atmospherics][conformance]") {
    const std::vector<std::string> declared = declaredKindNames();
    REQUIRE_FALSE(declared.empty());

    std::set<std::string> fromHeader;
    for (const std::string& n : declared) {
        fromHeader.insert(lower(n));
    }
    std::set<std::string> covered;
    for (const world::AtmosphereKind k : conf::kAtmosphereKinds) {
        covered.insert(lower(world::atmosphereKindName(k)));
    }

    // Printed rather than counted, because the point of this failing is that it names the kind
    // somebody added and did not finish wiring.
    for (const std::string& n : fromHeader) {
        INFO("AtmosphereKind::" << n << " is declared in atmospherics.hpp");
        CHECK(covered.count(n) == 1);
    }
    for (const std::string& n : covered) {
        INFO(n << " is in conformance::kAtmosphereKinds");
        CHECK(fromHeader.count(n) == 1);
    }

    SECTION("the reader can fail") {
        // The control ADR-182 asks for: the parser is shown finding what is there, so an empty
        // result above would be a broken parser rather than an empty enum.
        CHECK(fromHeader.count("comet") == 1);
        CHECK(fromHeader.count("aurora") == 1);
        CHECK(fromHeader.count("vortex") == 1);
        CHECK(fromHeader.count("nosuchkind") == 0);
    }
}

TEST_CASE("every atmospheric kind conforms to the family's contract",
          "[world][atmospherics][conformance][params]") {
    for (const world::AtmosphereKind kind : conf::kAtmosphereKinds) {
        const conf::Report r = conf::checkAtmospheric(kind);
        INFO("kind: " << world::atmosphereKindName(kind));
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
    for (const world::AtmosphereKind kind : conf::kAtmosphereKinds) {
        INFO("kind: " << world::atmosphereKindName(kind));

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
    for (const world::AtmosphereKind kind : conf::kAtmosphereKinds) {
        const world::AtmosphericEffect probe = conf::probeEffect(kind, "conformance probe");
        const std::string target = ui::atmosphericBeatTarget(probe.name, kind);
        const std::string prefix = world::atmosphericParameterPrefix(probe.name);
        REQUIRE(target.starts_with(prefix));

        const std::string_view leaf(target.data() + prefix.size(), target.size() - prefix.size());
        const conf::Report r = conf::checkLeavesExist(kind, std::span(&leaf, 1), "beat-target");
        INFO("kind: " << world::atmosphereKindName(kind) << ", target: " << target);
        INFO(r.summary());
        CHECK(r.clean());
    }
}

TEST_CASE("every row the World Effects panel draws is a parameter that kind has",
          "[world][atmospherics][conformance][ui]") {
    // ADR-387 put the vortex's rows in `ui_logic.hpp` as data so this question could be asked;
    // ADR-392 put the comet's and the aurora's there for the same reason, which is what turns this
    // from a test of one kind into a test of the family. A leaf five characters wrong in any of
    // them draws an empty box and says nothing (ADR-382), and now fails here with the path printed.
    const auto collect = [](std::span<const ui::EffectRow> a, std::span<const ui::EffectRow> b) {
        std::vector<std::string_view> leaves;
        for (const ui::EffectRow& r : a) {
            leaves.push_back(r.leaf);
        }
        for (const ui::EffectRow& r : b) {
            leaves.push_back(r.leaf);
        }
        return leaves;
    };

    SECTION("vortex") {
        const std::vector<std::string_view> leaves = collect(ui::vortexRows(), ui::vortexAdvancedRows());
        REQUIRE(leaves.size() > 10);
        const conf::Report r =
            conf::checkLeavesExist(world::AtmosphereKind::Vortex, leaves, "panel-rows");
        INFO(r.summary());
        CHECK(r.clean());
    }

    SECTION("comet") {
        const std::vector<std::string_view> leaves = collect(ui::cometRows(), ui::cometAdvancedRows());
        REQUIRE(leaves.size() > 10);
        const conf::Report r =
            conf::checkLeavesExist(world::AtmosphereKind::Comet, leaves, "panel-rows");
        INFO(r.summary());
        CHECK(r.clean());
    }

    SECTION("aurora") {
        const std::vector<std::string_view> leaves = collect(ui::auroraRows(), ui::auroraAdvancedRows());
        REQUIRE(leaves.size() > 10);
        const conf::Report r =
            conf::checkLeavesExist(world::AtmosphereKind::Aurora, leaves, "panel-rows");
        INFO(r.summary());
        CHECK(r.clean());
    }

    SECTION("the rainbow and ground rows belong to both sky kinds and to neither vortex") {
        const std::vector<std::string_view> shared =
            collect(ui::skyRainbowRows(), ui::skyGroundRows());
        for (const world::AtmosphereKind kind :
             {world::AtmosphereKind::Comet, world::AtmosphereKind::Aurora}) {
            const conf::Report r = conf::checkLeavesExist(kind, shared, "panel-rows");
            INFO("kind: " << world::atmosphereKindName(kind));
            INFO(r.summary());
            CHECK(r.clean());
        }
        // The other half of the same claim, and the reason the panel returns before drawing these
        // for a vortex: a vortex registers no rainbow at all, so these rows would find nothing.
        // ADR-375's lesson the other way round -- a control that draws and does nothing.
        std::vector<std::string_view> rainbow;
        for (const ui::EffectRow& r : ui::skyRainbowRows()) {
            rainbow.push_back(r.leaf);
        }
        const conf::Report vortex =
            conf::checkLeavesExist(world::AtmosphereKind::Vortex, rainbow, "panel-rows");
        CHECK_FALSE(vortex.clean());
        CHECK(vortex.findings.size() == rainbow.size());
    }
}

TEST_CASE("registered paths are read back from the registrar, not from the tables",
          "[world][atmospherics][conformance][params]") {
    // The property that makes the rest of this file mean anything: the path set comes from the
    // call the engine makes. If `registerAtmosphericParameters` stopped writing a row, every check
    // above would notice, because none of them has a second source of truth to agree with.
    for (const world::AtmosphereKind kind : conf::kAtmosphereKinds) {
        const world::AtmosphericEffect probe = conf::probeEffect(kind, "conformance probe");
        const std::vector<std::string> paths = conf::registeredPaths(probe);
        const std::vector<std::string> leaves = conf::registeredLeaves(probe);
        INFO("kind: " << world::atmosphereKindName(kind));
        REQUIRE(paths.size() == leaves.size());
        REQUIRE(paths.size() > 20);

        const std::string prefix = world::atmosphericParameterPrefix(probe.name);
        for (const std::string& p : paths) {
            INFO(p);
            CHECK(p.starts_with(prefix));
        }
        // A name is half of a path, so two effects of a kind must not collide.
        const std::set<std::string> unique(paths.begin(), paths.end());
        CHECK(unique.size() == paths.size());
    }
}
