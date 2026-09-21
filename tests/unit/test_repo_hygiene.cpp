// Guards against damage that lands in the tree and survives, because nothing looks for it.
//
// Both cases here happened. A merge-resolution script reported "conflicts resolved: N" for the two
// source files it was pointed at, and `docs/decisions/README.md` -- which also conflicted -- kept
// its markers and was committed. It then nested inside the next merge. A script's own report is not
// evidence that the tree is clean, and neither suite would have failed: conflict markers are legal
// text inside a Markdown table.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

std::vector<std::filesystem::path> filesUnder(const std::filesystem::path& root,
                                              const std::set<std::string>& extensions) {
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::exists(root)) {
        return out;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (extensions.count(entry.path().extension().string()) != 0) {
            out.push_back(entry.path());
        }
    }
    return out;
}

} // namespace

TEST_CASE("no source or document carries a merge conflict marker", "[hygiene]") {
    const std::filesystem::path root(AVGEN_SOURCE_DIR);
    std::vector<std::filesystem::path> files;
    for (const char* dir : {"src", "tests", "docs", "shaders", "tools"}) {
        auto found = filesUnder(root / dir, {".cpp", ".hpp", ".wgsl", ".md", ".sh", ".py", ".json"});
        files.insert(files.end(), found.begin(), found.end());
    }
    REQUIRE(files.size() > 100); // the walk found the tree, not an empty directory

    std::vector<std::string> offenders;
    for (const auto& file : files) {
        std::ifstream in(file);
        std::string line;
        int number = 0;
        while (std::getline(in, line)) {
            ++number;
            // Anchored at column 0 and length-exact, so prose about conflicts does not trip it.
            const bool marker = line.rfind("<<<<<<< ", 0) == 0 || line.rfind(">>>>>>> ", 0) == 0 ||
                                line == "=======";
            if (marker) {
                offenders.push_back(std::filesystem::relative(file, root).string() + ":" +
                                    std::to_string(number));
            }
        }
    }
    INFO("files carrying conflict markers: " << offenders.size());
    for (const auto& o : offenders) {
        INFO("  " << o);
    }
    CHECK(offenders.empty());
}

TEST_CASE("every ADR is indexed and every indexed ADR exists", "[hygiene]") {
    const std::filesystem::path decisions = std::filesystem::path(AVGEN_SOURCE_DIR) / "docs" / "decisions";
    REQUIRE(std::filesystem::exists(decisions / "README.md"));

    std::set<int> onDisk;
    const std::regex fileRe(R"(ADR-(\d+))");
    for (const auto& entry : std::filesystem::directory_iterator(decisions)) {
        std::smatch m;
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && std::regex_search(name, m, fileRe)) {
            onDisk.insert(std::stoi(m[1]));
        }
    }
    REQUIRE(onDisk.size() > 100); // the directory was actually read

    std::set<int> indexed;
    std::ifstream in(decisions / "README.md");
    std::string line;
    const std::regex rowRe(R"(^\|\s*\[(\d+)\])");
    while (std::getline(in, line)) {
        std::smatch m;
        if (std::regex_search(line, m, rowRe)) {
            indexed.insert(std::stoi(m[1]));
        }
    }

    std::vector<int> missingFromIndex;
    for (const int n : onDisk) {
        if (indexed.count(n) == 0) {
            missingFromIndex.push_back(n);
        }
    }
    std::vector<int> danglingRows;
    for (const int n : indexed) {
        if (onDisk.count(n) == 0) {
            danglingRows.push_back(n);
        }
    }
    INFO("ADR files not listed in README: " << missingFromIndex.size());
    INFO("README rows with no ADR file: " << danglingRows.size());
    CHECK(missingFromIndex.empty());
    CHECK(danglingRows.empty());
}

// ADR-211. The World panel's Inspector answers "why is this moving?", and its answer is only worth
// anything if it is complete. It was not: entity behaviours write a node's transform parameters
// directly, so the panel said "nothing modulates this object; its parameters are static" about a
// character walking across the valley.
//
// Completeness cannot be asserted by asking `influencesOf` what it covers -- that is the function
// under test answering a question about itself. What can be asserted is the *premise* the
// completeness argument rests on: the set of places that write a parameter's final value. Every one
// of them has to be a kind the Inspector knows about, so a new writer added later fails here and
// whoever adds it has to decide what the panel should say.
//
// A grep rather than a link-time check because `influencesOf` lives in `world_panel.cpp`, behind
// ImGui, which the unit-test binary does not link.
TEST_CASE("every writer of a parameter final is a kind the Inspector can name", "[hygiene][ui]") {
    const std::filesystem::path src = std::filesystem::path(AVGEN_SOURCE_DIR) / "src";
    std::set<std::string> writers;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
            continue;
        }
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line)) {
            // The call, not the word in a comment explaining it.
            if (line.find("setFinalComponent(") == std::string::npos &&
                line.find("addToFinal(") == std::string::npos) {
                continue;
            }
            if (line.find("//") != std::string::npos &&
                line.find("//") < line.find("setFinalComponent(")) {
                continue;
            }
            writers.insert(std::filesystem::relative(entry.path(), src).string());
        }
    }

    // What each of these is, and the `Influence::Kind` that names it:
    //   params/modulation.cpp  -> Kind::Route
    //   params/timeline.cpp    -> Kind::Timeline  (and Cue and State, which reach a parameter
    //                             through a preset the timeline applies)
    //   entity/entity.cpp      -> Kind::Entity    (behaviours and the action system)
    //   params/parameter_set.cpp -> resets finals; it establishes the base rather than modulating
    //   ui/edit_history.cpp    -> an undo. A user action, not an influence on a running scene.
    // World macros do not appear because a macro writes through routes.
    //   stage/staging.cpp      -> Kind::Entity *and* Kind::Staging. A scenario (ADR-210) drives a
    //                             body through `entity::DirectorMotion`, which the entity branch
    //                             names; it also writes parameters directly -- a tractor beam's
    //                             visibility, a spawn rate, any absolute path a `set` step gives --
    //                             and `Staging::writersOf` names those (ADR-241). The one final it
    //                             writes that is NOT an influence is a scenario's own knob in
    //                             `setParameter`: that is an author moving a slider, like an undo.
    //
    // This test found `stage/staging.cpp` the day it landed, which is what it is for.
    const std::set<std::string> known{
        "params/modulation.cpp", "params/timeline.cpp", "entity/entity.cpp",
        "params/parameter_set.cpp", "ui/edit_history.cpp", "stage/staging.cpp",
    };
    std::vector<std::string> unexpected;
    std::set_difference(writers.begin(), writers.end(), known.begin(), known.end(),
                        std::back_inserter(unexpected));
    for (const std::string& w : unexpected) {
        INFO("writes a parameter final and the Inspector has no Influence::Kind for it: " << w);
    }
    CHECK(unexpected.empty());
    // And the premise itself: if this ever empties, the grep has stopped matching and the check
    // above would pass by finding nothing at all.
    CHECK(writers.size() >= 4);
}

// A credit obligation that lives only in a document is one edit away from vanishing, and nothing
// about the resulting tree looks wrong. 100STYLE is CC0 AND asks to be credited for creative or
// commercial work -- two separate facts, the second not implied by the first, which is exactly the
// shape of obligation someone drops while tidying. The assertion is on the exact line, because a
// paraphrase satisfies a reader and not the request.
TEST_CASE("the credits a published work must carry are still there", "[hygiene]") {
    const std::filesystem::path root(AVGEN_SOURCE_DIR);

    const std::filesystem::path credits = root / "CREDITS.md";
    REQUIRE(std::filesystem::exists(credits));

    std::ifstream in(credits);
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // The line as the dataset asks for it, not as someone remembered it.
    REQUIRE(text.find("The 100STYLE Dataset - Ian Mason") != std::string::npos);

    // And the reason it survives a CC0 waiver, so a future reader cannot conclude it lapsed.
    REQUIRE(text.find("CC0") != std::string::npos);

    // The engineering record must keep pointing at the same obligation.
    const std::filesystem::path deps = root / "docs" / "dependencies.md";
    REQUIRE(std::filesystem::exists(deps));
    std::ifstream depsIn(deps);
    const std::string depsText((std::istreambuf_iterator<char>(depsIn)),
                               std::istreambuf_iterator<char>());
    REQUIRE(depsText.find("The 100STYLE Dataset - Ian Mason") != std::string::npos);
}
