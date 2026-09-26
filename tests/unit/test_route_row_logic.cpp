// ADR-900/902: the decisions behind the Modulation panel's route rows -- the liveness badge beside a
// route's header, the signature that decides when a row is re-checked, and the depth-source combo.
// The drawing is ImGui (control_panel.cpp); these are the parts a CPU test can hold.

#include "ui/route_row_logic.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <vector>

using namespace avgen;
using namespace avgen::params;
using namespace avgen::params::liveness;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("A route row's badge names the worst finding and lists them all", "[ui][liveness][adr902]") {
    CHECK_FALSE(ui::routeBadge(std::vector<Finding>{}).show); // live: nothing beside the header

    const std::vector<Finding> hazard{{"phase-rate", Verdict::Hazard, "time x rate"}};
    const ui::RouteBadge h = ui::routeBadge(hazard);
    CHECK(h.show);
    CHECK_FALSE(h.dead);
    CHECK(h.text == "[hazard: phase-rate]");
    CHECK_THAT(h.tooltip, ContainsSubstring("time x rate"));

    const std::vector<Finding> mixed{{"phase-rate", Verdict::Hazard, "time x rate"},
                                     {"node-emits-nothing", Verdict::Dead, "no surface emits"}};
    const ui::RouteBadge d = ui::routeBadge(mixed);
    CHECK(d.dead);
    CHECK(d.text == "[dead: node-emits-nothing +1]"); // the worst verdict's rule, and the count of the rest
    CHECK_THAT(d.tooltip, ContainsSubstring("no surface emits"));
    CHECK_THAT(d.tooltip, ContainsSubstring("time x rate"));
}

TEST_CASE("A route row is re-checked when anything the rules read changes", "[ui][liveness][adr902]") {
    ModRoute r;
    r.source = "audio.bass";
    r.target = "orb/scale";
    const std::uint64_t base = ui::routeSignature(r);
    CHECK(ui::routeSignature(r) == base); // stable
    const auto changed = [&](auto&& edit) {
        ModRoute copy = r;
        edit(copy);
        return ui::routeSignature(copy) != base;
    };
    CHECK(changed([](ModRoute& x) { x.chain.delayMs = 120.0f; }));
    CHECK(changed([](ModRoute& x) { x.depthSource = "section.energy"; }));
    CHECK(changed([](ModRoute& x) { x.depthMin = 0.3f; }));
    CHECK(changed([](ModRoute& x) { x.chain.attackMs = 40.0f; }));
    CHECK(changed([](ModRoute& x) { x.chain.threshold = ThresholdMode::Gate; }));
    CHECK(changed([](ModRoute& x) { x.target = "orb/color"; }));
    CHECK(changed([](ModRoute& x) { x.enabled = false; }));
    CHECK(changed([](ModRoute& x) { x.op = ModOp::Multiply; }));
    // "ab" + "c" and "a" + "bc" are different routes.
    ModRoute a = r;
    a.source = "ab";
    a.target = "c";
    ModRoute b = r;
    b.source = "a";
    b.target = "bc";
    CHECK(ui::routeSignature(a) != ui::routeSignature(b));
}

TEST_CASE("The depth-source combo lists (none) first and every signal, sorted", "[ui][depth][adr900]") {
    signals::SignalBus bus;
    bus.declare("lfo.wob");
    bus.declare("audio.bass");
    bus.declare("section.energy");
    const auto choices = ui::depthSourceChoices(bus);
    REQUIRE(choices.size() == 4);
    CHECK(choices[0] == "(none)");
    CHECK(choices[1] == "audio.bass");
    CHECK(choices[3] == "section.energy");
    CHECK(ui::depthSourceIndex(choices, "") == 0);
    CHECK(ui::depthSourceIndex(choices, "lfo.wob") == 2);
    // A depth source the bus does not carry yet stays listed, so the combo shows the route's own choice.
    const auto pending = ui::depthSourceChoices(bus, "timeline.sectionEnergy");
    REQUIRE(pending.size() == 5);
    CHECK(ui::depthSourceIndex(pending, "timeline.sectionEnergy") != 0);
    CHECK(pending[static_cast<std::size_t>(ui::depthSourceIndex(pending, "timeline.sectionEnergy"))] ==
          "timeline.sectionEnergy");
}
