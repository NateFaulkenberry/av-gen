// The Parameters panel's sections: can a person tell what a checkbox switches on?
//
// The owner opened the "post" group and found three checkboxes all labelled "enabled", above three
// sliders all labelled "intensity", and reported it as "I have no idea what I'm enabling when I
// click a checkbox". The cause is structural rather than cosmetic: a parameter's `group()` is the
// FIRST path segment and its `label()` is the LAST, so for `post/bloom/enabled` the word that
// identifies it -- "bloom" -- is the one part the panel was throwing away.
//
// These tests do the panel's OWN arithmetic (`ui::parameterSubGroup`, `ui::parameterLeaf`) against
// the real registered parameter set. That distinction is the point: this repository has twice
// shipped a panel whose sections were empty because its string arithmetic was wrong, and both times
// the tests passed because they asserted the REGISTRATION paths and never the paths the panel
// computed from them (ADR-382, ADR-387). Asserting `post/bloom/enabled` exists proves nothing about
// whether anything on screen says "bloom".

#include "params/parameter_set.hpp"
#include "scene/post_settings.hpp"
#include "ui/ui_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace avgen;

TEST_CASE("a parameter's section is the middle of its path", "[ui][params][sections]") {
    CHECK(ui::parameterSubGroup("post/bloom/intensity", "post") == "bloom");
    CHECK(ui::parameterSubGroup("post/halation/enabled", "post") == "halation");
    // A direct member of the group has no section and must not invent one.
    CHECK(ui::parameterSubGroup("post/enabled", "post").empty());
    CHECK(ui::parameterSubGroup("root/scale", "root").empty());
    // Deeper paths keep every segment between the group and the leaf, so two nodes' wind groups
    // cannot collide into one heading.
    CHECK(ui::parameterSubGroup("nodes/tree-of-life/wind/lag", "nodes") == "tree-of-life/wind");
    CHECK(ui::parameterSubGroup("nodes/oak/wind/lag", "nodes") == "oak/wind");
    // A path that does not start with its group is left alone rather than silently mis-split -- the
    // off-by-N that produced `nodes/tree-of-energy/intensity` came from arithmetic that assumed.
    CHECK(ui::parameterSubGroup("post/bloom/intensity", "camera") == "post/bloom");

    CHECK(ui::parameterLeaf("post/bloom/enabled") == "enabled");
    CHECK(ui::parameterLeaf("enabled") == "enabled");
}

TEST_CASE("every 'enabled' checkbox in the post group is told apart by its section",
          "[ui][params][sections]") {
    params::ParameterSet params;
    scene::PostSettings settings;
    static_cast<void>(scene::registerPostParameters(params, settings));

    // Group the way the panel does.
    std::map<std::string, std::vector<const params::IParameter*>> sections;
    std::vector<const params::IParameter*> direct;
    for (const params::IParameter* p : params.ordered()) {
        if (!p->flags().exposed || p->group() != "post") {
            continue;
        }
        const std::string sub = ui::parameterSubGroup(p->path(), "post");
        if (sub.empty()) {
            direct.push_back(p);
        } else {
            sections[sub].push_back(p);
        }
    }

    // THE PREMISE, and it is what makes the rest mean anything: there really is more than one
    // switch in this group. With a single section nothing could be confused with anything and the
    // test below would pass against the broken panel too.
    std::size_t switches = 0;
    for (const auto& [name, members] : sections) {
        for (const params::IParameter* p : members) {
            if (ui::parameterLeaf(p->path()) == "enabled") {
                ++switches;
            }
        }
    }
    INFO("sections found: " << sections.size() << ", switches: " << switches);
    REQUIRE(sections.size() > 1);
    REQUIRE(switches > 1);

    // Bloom is the one the owner was looking at, so it is named rather than merely counted.
    REQUIRE(sections.count("bloom") == 1);
    std::set<std::string> bloomLeaves;
    for (const params::IParameter* p : sections["bloom"]) {
        bloomLeaves.insert(std::string(ui::parameterLeaf(p->path())));
    }
    CHECK(bloomLeaves.count("enabled") == 1);
    CHECK(bloomLeaves.count("intensity") == 1);
    CHECK(bloomLeaves.count("levels") == 1);

    // Every section that has a switch has a NAME, and the names are distinct -- which is the whole
    // of the owner's complaint restated as an assertion. Before this change every one of these
    // resolved to the empty string and they all collided.
    std::set<std::string> named;
    for (const auto& [name, members] : sections) {
        for (const params::IParameter* p : members) {
            if (ui::parameterLeaf(p->path()) == "enabled") {
                INFO("switch at " << p->path());
                CHECK_FALSE(name.empty());
                named.insert(name);
            }
        }
    }
    CHECK(named.size() == switches);
}

TEST_CASE("no label in the post group repeats the path the heading already gives",
          "[ui][params][sections]") {
    params::ParameterSet params;
    scene::PostSettings settings;
    static_cast<void>(scene::registerPostParameters(params, settings));

    // `post/bloom/levels` shipped with its whole path as its label, so it was the one row in the
    // panel shouting its own address while its neighbours said "intensity" and "knee". Now that the
    // section is headed "bloom", the prefix said a third time what the group and the heading say.
    for (const params::IParameter* p : params.ordered()) {
        if (p->group() != "post") {
            continue;
        }
        INFO(p->path() << " labelled '" << p->label() << "'");
        CHECK(p->label().find("post/") == std::string::npos);
    }
}
