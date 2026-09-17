// The lab registry, and the two probes that make it more than a table of strings (ADR-260).
//
// The registry claims two things about the repository: that each lab's decision is made in a named
// file, and that each lab opens on a fixture that exists. Both claims rot silently -- a rename
// costs nothing and breaks neither the build nor any other test -- so both are checked here against
// the filesystem, and both checks are shown capable of failing (ADR-182).

#include "labs/case.hpp"
#include "labs/lab.hpp"
#include "labs/overlays.hpp"
#include "labs/visibility_reason.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace avgen;

namespace {

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }

// A `decides` entry is `path` or `path:symbol`. Only the path half is a file.
//
// Split on the FIRST colon, not the last: `PostProcessor::run` is a perfectly ordinary symbol and
// `rfind` cut it in half, which is a bug this test found in itself before it found one anywhere
// else. Repository paths carry no colons, so the first one is always the separator.
std::string pathHalf(std::string_view decides) {
    const std::size_t colon = decides.find(':');
    return std::string(colon == std::string_view::npos ? decides : decides.substr(0, colon));
}

std::string symbolHalf(std::string_view decides) {
    const std::size_t colon = decides.find(':');
    return colon == std::string_view::npos ? std::string() : std::string(decides.substr(colon + 1));
}

} // namespace

TEST_CASE("every lab names a decision site that exists", "[labs][registry]") {
    for (const labs::LabDescriptor& d : labs::labs()) {
        INFO("lab: " << d.key << "  decides: " << d.decides);
        const fs::path file = repoRoot() / pathHalf(d.decides);
        CHECK(fs::is_regular_file(file));
    }

    // The control. If the check above passed because `is_regular_file` is being asked about
    // something that always exists -- a directory, a path the loop accidentally emptied -- it would
    // pass for a name nobody wrote too. This is the same call on a name nobody wrote.
    CHECK_FALSE(fs::is_regular_file(repoRoot() / "src/rendering/scene_renderer_that_is_not_there.cpp"));
}

TEST_CASE("every lab names a symbol its decision file actually contains", "[labs][registry]") {
    // The path half is checked above; this is the half a rename usually breaks, because moving a
    // function between files leaves both files in place.
    std::size_t checked = 0;
    for (const labs::LabDescriptor& d : labs::labs()) {
        const std::string symbol = symbolHalf(d.decides);
        if (symbol.empty()) {
            continue;
        }
        const fs::path file = repoRoot() / pathHalf(d.decides);
        std::ifstream in(file);
        REQUIRE(in);
        std::stringstream buffer;
        buffer << in.rdbuf();
        INFO("lab: " << d.key << "  symbol: " << symbol << "  in " << pathHalf(d.decides));
        CHECK(buffer.str().find(symbol) != std::string::npos);
        ++checked;
    }
    // A loop that checked nothing passes. Say how many it checked.
    CHECK(checked >= 5);
}

TEST_CASE("every lab's fixture, doc and case file exist", "[labs][registry]") {
    for (const labs::LabDescriptor& d : labs::labs()) {
        INFO("lab: " << d.key);
        CHECK(fs::is_regular_file(repoRoot() / d.doc));
        if (!d.fixture.empty()) {
            // A fixture may be a directory of looks (`examples/lightrigs`) as well as a file.
            CHECK(fs::exists(repoRoot() / d.fixture));
        }
        if (!d.cases.empty()) {
            CHECK(fs::is_regular_file(repoRoot() / d.cases));
        }
    }
}

TEST_CASE("lab keys are unique, lowercase and resolvable", "[labs][registry]") {
    std::set<std::string_view> keys;
    for (const labs::LabDescriptor& d : labs::labs()) {
        INFO("lab: " << d.key);
        CHECK(keys.insert(d.key).second);
        for (char c : d.key) {
            CHECK((c >= 'a' && c <= 'z'));
        }
        const auto found = labs::findLab(d.key);
        REQUIRE(found.has_value());
        CHECK(*found == d.id);
        // `lab(id)` indexes the table by the enum; a table written out of order returns the wrong
        // descriptor and nothing else would notice.
        CHECK(labs::lab(d.id).key == d.key);
    }
    CHECK(labs::labs().size() == 14);
    CHECK_FALSE(labs::findLab("shadows").has_value()); // the lab is "shadow"
    CHECK_FALSE(labs::findLab("").has_value());
}

TEST_CASE("every lab states what it owns and what it does not", "[labs][registry]") {
    // §34's boundary is the reason the registry exists; a lab with an empty `doesNotOwn` is a lab
    // that will be asked to fix somebody else's bug.
    for (const labs::LabDescriptor& d : labs::labs()) {
        INFO("lab: " << d.key);
        CHECK_FALSE(d.question.empty());
        CHECK_FALSE(d.owns.empty());
        CHECK_FALSE(d.doesNotOwn.empty());
        CHECK_FALSE(d.title.empty());
    }
    CHECK(labs::labKeys().find("visibility") != std::string_view::npos);
}

TEST_CASE("an inert debug overlay is in no lab's profile", "[labs][overlays]") {
    // `DebugViewOptions::culling` has a checkbox in `world_panel.cpp` and `debug_visualizer.cpp`
    // reads the field nowhere, so it draws nothing. A lab profile that switched it on would be the
    // suite showing a person a control that does nothing -- ADR-225, committed by the thing built
    // to prevent it. When the Visibility Lab wires it up, this test is what says the prohibition
    // can be lifted.
    //
    // `::lod` was here too and is not any more: the LOD Lab wired it to the rung the cull pass
    // writes for each record (`ProceduralRenderer::readLodLevels`), so its profile is allowed to
    // turn it on and the assertion below is now the opposite one.
    for (const labs::LabDescriptor& d : labs::labs()) {
        const rendering::DebugViewOptions o = labs::overlaysFor(d.id);
        INFO("lab: " << d.key);
        CHECK_FALSE(o.culling);
    }
    // The LOD profile does switch the rung overlay on, and it is the only one that does: an
    // overlay every profile enabled would be a decoration rather than a selection.
    std::size_t withLod = 0;
    for (const labs::LabDescriptor& d : labs::labs()) {
        withLod += labs::overlaysFor(d.id).lod ? 1 : 0;
    }
    CHECK(labs::overlaysFor(labs::LabId::Lod).lod);
    CHECK(withLod == 1);

    // And the control: the profiles are not simply all-default. If they were, the loop above would
    // pass while saying nothing.
    const rendering::DebugViewOptions visibility = labs::overlaysFor(labs::LabId::Visibility);
    CHECK(visibility.frustum);
    CHECK(visibility.entityBounds);
    CHECK(visibility.submittedOnly);
    // The Rendering Lab measures frames, so it draws nothing over them.
    const rendering::DebugViewOptions measured = labs::overlaysFor(labs::LabId::Rendering);
    CHECK_FALSE(measured.frustum);
    CHECK_FALSE(measured.entityBounds);
    CHECK_FALSE(measured.skeletons);
    CHECK_FALSE(measured.points);
}

TEST_CASE("the visibility vocabulary maps every reason the renderer writes", "[labs][visibility]") {
    // The probe that matters: read `scene_renderer.cpp` and find every string it assigns to
    // `cullReason`. If the renderer grows a sixth reason, this fails -- which is the only way the
    // vocabulary and the renderer can be kept in step, since nothing links them.
    const fs::path file = repoRoot() / "src/rendering/scene_renderer.cpp";
    std::ifstream in(file);
    REQUIRE(in);
    std::string line;
    std::set<std::string> written;
    while (std::getline(in, line)) {
        // Every string literal on a line that assigns `cullReason`. Not `cullReason = "..."`: one
        // of the three assignments is a nested ternary picking between three literals, and a scan
        // that only matched the direct form found two of the five reasons and reported success.
        if (line.find("cullReason") == std::string::npos || line.find('=') == std::string::npos) {
            continue;
        }
        std::size_t at = line.find('"');
        while (at != std::string::npos) {
            const std::size_t end = line.find('"', at + 1);
            if (end == std::string::npos) {
                break;
            }
            written.insert(line.substr(at + 1, end - at - 1));
            at = line.find('"', end + 1);
        }
    }
    INFO("scene_renderer.cpp writes " << written.size() << " distinct cull reasons");
    CHECK(written.size() >= 4); // the scan found something; an empty set would pass the loop below
    for (const std::string& reason : written) {
        INFO("cullReason: " << reason);
        CHECK(labs::fromCullReason(reason).has_value());
    }

    // The control: a string the renderer does not write must not map, or the mapping is a function
    // that says yes to everything.
    CHECK_FALSE(labs::fromCullReason("occluded").has_value());
    CHECK_FALSE(labs::fromCullReason("").has_value());
}

TEST_CASE("the vocabulary invents no reason the engine cannot reach", "[labs][visibility]") {
    // §9: "Do not invent reasons that don't correspond to real decisions." There is no occlusion
    // culling in this engine -- no HiZ, no depth pyramid, no query -- so there is no code for it.
    for (const labs::ReasonInfo& info : labs::visibilityReasons()) {
        INFO("code: " << info.code);
        CHECK(info.code.find("OCCLUSION") == std::string_view::npos);
        CHECK_FALSE(info.decidedAt.empty());
        // A `Reported` reason must carry the string the renderer writes, or the claim that it is
        // reported is unbacked.
        if (info.status == labs::ReasonStatus::Reported) {
            CHECK_FALSE(info.cullReason.empty());
        }
    }
    std::set<std::string_view> codes;
    for (const labs::ReasonInfo& info : labs::visibilityReasons()) {
        CHECK(codes.insert(info.code).second);
        CHECK(labs::reasonInfo(info.reason).code == info.code); // indexed in enum order
        const auto back = labs::reasonFromCode(info.code);
        REQUIRE(back.has_value());
        CHECK(*back == info.reason);
    }
    CHECK_FALSE(labs::reasonFromCode("OCCLUSION_CULLED").has_value());
}
