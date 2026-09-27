// Set-piece templates (ADR-928): a parameterised `ScenarioDesc`, instanced with overrides.
//
// What these pin, each with the partner that would pass against a stub:
//   * the three templates instance the beats and cues their names promise, and an abduction's animal
//     count is cues, not a number nothing reads;
//   * every parameter an instance registers is read by one of its steps (the silent no-op rule) --
//     and the full slot list is NOT, for some variants, so the filter is doing work;
//   * the placed moment's beat carries the clock and the others do not;
//   * refusals name what is wrong: an unknown slot, a value out of range, a moment the template does
//     not have, a flyby asked for a beam colour.

#include "stage/setpiece.hpp"
#include "stage/staging.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;

namespace {

stage::SetPieceSpec abduction(int animals = 1) {
    stage::SetPieceSpec spec;
    spec.id = "field";
    spec.kind = stage::SetPieceKind::Abduction;
    spec.actor = "saucer";
    spec.atSeconds = 60.0;
    spec.place.point = glm::vec2(62.0f, 22.0f);
    spec.overrides = {{"animals", static_cast<float>(animals)}};
    return spec;
}

const stage::BeatDesc* beat(const stage::ScenarioDesc& s, std::string_view name) {
    for (const stage::BeatDesc& b : s.beats) {
        if (b.name == name) {
            return &b;
        }
    }
    return nullptr;
}

std::vector<std::string> beatNames(const stage::ScenarioDesc& s) {
    std::vector<std::string> out;
    for (const stage::BeatDesc& b : s.beats) {
        out.push_back(b.name);
    }
    return out;
}

// Every parameter a scenario's steps, queries and beats read.
std::set<std::string> readByBeats(const stage::ScenarioDesc& s) {
    std::set<std::string> out;
    const auto take = [&](const stage::Value& v) {
        if (v.bound()) {
            out.insert(v.param);
        }
    };
    for (const stage::BeatDesc& b : s.beats) {
        take(b.stillSpeed);
        take(b.startAt);
        for (const stage::QueryDesc& q : b.find) {
            take(q.radius);
            take(q.minRadius);
            take(q.clearance);
        }
        for (const stage::CueDesc& c : b.cues) {
            for (const stage::StepDesc& st : c.steps) {
                for (const stage::Value* v : {&st.duration, &st.height, &st.speed, &st.tolerance, &st.clearance, &st.spin,
                                              &st.wobble, &st.wobbleRate, &st.to, &st.from, &st.rate}) {
                    take(*v);
                }
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("an abduction instances its beats, and its animal count is cues", "[stage][setpiece]") {
    for (const int n : {1, 2, 3}) {
        auto s = stage::instanceSetPiece(abduction(n));
        INFO(n << " animal(s)");
        REQUIRE(s.has_value());
        CHECK(s->name == "setpiece/field");
        CHECK(s->actor == "saucer");
        CHECK(s->autoStart);
        CHECK(s->maxCycles == 1);
        CHECK(beatNames(*s) == std::vector<std::string>{"rest", "transit", "approach", "hover", "beam", "lift", "depart"});
        // One role and one cue each (two cues in the lift: the rise and the dissolve), gathered at the
        // hover, which waits for the craft to stop (ADR-385).
        const stage::BeatDesc* hover = beat(*s, "hover");
        REQUIRE(hover != nullptr);
        CHECK(hover->stillRoles == std::vector<std::string>{"actor"});
        CHECK(hover->find.size() == static_cast<std::size_t>(n));
        const stage::BeatDesc* lift = beat(*s, "lift");
        REQUIRE(lift != nullptr);
        std::set<std::string> lifted;
        for (const stage::CueDesc& c : lift->cues) {
            if (c.role.rfind("target", 0) == 0) {
                lifted.insert(c.role);
            }
        }
        CHECK(lifted.size() == static_cast<std::size_t>(n));
        CHECK(lift->cues.size() == static_cast<std::size_t>(1 + (2 * n)));
        // It installs: every Value names a declared parameter and no two cues form a runaway.
        stage::Staging staging;
        stage::StagingDesc desc;
        desc.actors.push_back(stage::ActorDesc{"saucer", "visitor", {{"beam", "visitor-beam"}}});
        desc.scenarios.push_back(*s);
        const auto ok = staging.setDesc(desc);
        INFO((ok ? std::string() : ok.error().message));
        CHECK(ok.has_value());
    }
}

TEST_CASE("the lifted animals take different places in the column", "[stage][setpiece]") {
    auto s = stage::instanceSetPiece(abduction(3));
    REQUIRE(s.has_value());
    std::vector<glm::vec3> spots;
    for (const stage::CueDesc& c : beat(*s, "lift")->cues) {
        for (const stage::StepDesc& st : c.steps) {
            if (st.kind == stage::StepKind::MoveTo) {
                spots.push_back(st.point);
            }
        }
    }
    REQUIRE(spots.size() == 3);
    for (std::size_t i = 0; i < spots.size(); ++i) {
        for (std::size_t j = i + 1; j < spots.size(); ++j) {
            CHECK(glm::length(spots[i] - spots[j]) > 1.0f); // never rising through each other
        }
    }
    // Control: one animal rises on the axis.
    auto one = stage::instanceSetPiece(abduction(1));
    REQUIRE(one.has_value());
    for (const stage::CueDesc& c : beat(*one, "lift")->cues) {
        for (const stage::StepDesc& st : c.steps) {
            if (st.kind == stage::StepKind::MoveTo) {
                CHECK(st.point == glm::vec3(0.0f));
            }
        }
    }
}

TEST_CASE("a survey sweeps with its beam lit and a flyby crosses", "[stage][setpiece]") {
    stage::SetPieceSpec survey = abduction();
    survey.kind = stage::SetPieceKind::Survey;
    survey.overrides = {{"sweepLength", 80.0f}, {"sweepBearing", 90.0f}};
    auto s = stage::instanceSetPiece(survey);
    REQUIRE(s.has_value());
    CHECK(beatNames(*s) == std::vector<std::string>{"rest", "transit", "approach", "hover", "beam", "sweep", "depart"});
    // Nothing is lifted: no queries, no target roles.
    for (const stage::BeatDesc& b : s->beats) {
        CHECK(b.find.empty());
        for (const stage::CueDesc& c : b.cues) {
            CHECK(c.role.rfind("target", 0) != 0);
        }
    }
    // The sweep crosses the field: from 40 m west of the place to 40 m east of it.
    auto timeline = stage::setPieceTimeline(survey);
    REQUIRE(timeline.has_value());
    CHECK(timeline->station.x == Approx(62.0f - 40.0f).margin(1e-3));
    const stage::StepDesc& across = beat(*s, "sweep")->cues.front().steps.front();
    CHECK(across.point.x == Approx(62.0f + 40.0f).margin(1e-3));

    stage::SetPieceSpec flyby = abduction();
    flyby.kind = stage::SetPieceKind::Flyby;
    flyby.overrides = {{"pathLength", 400.0f}, {"pathBearing", 270.0f}};
    auto f = stage::instanceSetPiece(flyby);
    REQUIRE(f.has_value());
    CHECK(beatNames(*f) == std::vector<std::string>{"rest", "transit", "cross", "depart"});
    auto ft = stage::setPieceTimeline(flyby);
    REQUIRE(ft.has_value());
    // Flying west: it appears 200 m east of the place and leaves 200 m west of it.
    CHECK(ft->entry.x == Approx(62.0f + 200.0f).margin(1e-3));
    CHECK(ft->exit.x == Approx(62.0f - 200.0f).margin(1e-3));
}

TEST_CASE("every parameter a set piece registers is read by one of its steps", "[stage][setpiece]") {
    // The variants that leave slots unread: named animals (no gather), a region with one animal (no
    // gather either), a coloured beam (three more parameters, all read), and each template.
    std::vector<stage::SetPieceSpec> variants;
    variants.push_back(abduction(1));
    variants.push_back(abduction(3));
    stage::SetPieceSpec named = abduction(2);
    named.animals = {"cow-b1", "cow-b2"};
    named.overrides.clear();
    variants.push_back(named);
    stage::SetPieceSpec region = abduction(1);
    region.place.kind = stage::SetPiecePlace::Kind::Region;
    region.place.radius = 30.0f;
    variants.push_back(region);
    stage::SetPieceSpec coloured = abduction(2);
    coloured.beamColor = glm::vec3(1.0f, 0.3f, 0.1f);
    variants.push_back(coloured);
    stage::SetPieceSpec survey = abduction();
    survey.kind = stage::SetPieceKind::Survey;
    survey.overrides.clear();
    variants.push_back(survey);
    stage::SetPieceSpec flyby = survey;
    flyby.kind = stage::SetPieceKind::Flyby;
    variants.push_back(flyby);

    bool filterMattered = false;
    for (const stage::SetPieceSpec& spec : variants) {
        auto s = stage::instanceSetPiece(spec);
        INFO(stage::setPieceKindName(spec.kind) << " " << spec.animals.size() << " named");
        REQUIRE(s.has_value());
        const std::set<std::string> read = readByBeats(*s);
        for (const stage::ScenarioParam& p : s->params) {
            INFO(p.name);
            CHECK(read.contains(p.name));
        }
        // And nothing is read that is not declared (setDesc would refuse it; checked here directly).
        for (const std::string& name : read) {
            CHECK(std::any_of(s->params.begin(), s->params.end(), [&](const stage::ScenarioParam& p) { return p.name == name; }));
        }
        // The control: the template's own Parameter slots include some this variant does not read.
        for (const stage::SetPieceSlot& slot : stage::setPieceSlots(spec.kind)) {
            if (slot.use == stage::SlotUse::Parameter && !read.contains(slot.name)) {
                filterMattered = true;
            }
        }
    }
    CHECK(filterMattered); // e.g. gatherRadius when the animals are named: registering it would be inert
}

TEST_CASE("the placed moment's beat carries the clock, and no other beat but the transit does",
          "[stage][setpiece][time]") {
    for (const char* moment : {"approach", "beam", "lift", "depart"}) {
        stage::SetPieceSpec spec = abduction(2);
        spec.moment = moment;
        auto s = stage::instanceSetPiece(spec);
        INFO(moment);
        REQUIRE(s.has_value());
        for (const stage::BeatDesc& b : s->beats) {
            if (b.name == moment) {
                CHECK(b.startAt.param == std::string(moment) + "At");
            } else if (b.name == "transit") {
                CHECK(b.startAt.param == "transitAt");
            } else {
                CHECK_FALSE(b.startAt.bound());
            }
        }
        const auto clockValue = std::find_if(s->params.begin(), s->params.end(),
                                             [&](const stage::ScenarioParam& p) { return p.name == std::string(moment) + "At"; });
        REQUIRE(clockValue != s->params.end());
        CHECK(clockValue->value == Approx(60.0f));
        // The nominal timeline agrees: the placed moment is exactly where the plan put it.
        auto timeline = stage::setPieceTimeline(spec);
        REQUIRE(timeline.has_value());
        CHECK(*timeline->at(moment) == Approx(60.0));
        // ...and the moments are in order, the craft taken before the first and let go after the last.
        double last = timeline->start;
        for (const auto& [name, t] : timeline->moments) {
            CHECK(t > last);
            last = t;
        }
        CHECK(timeline->end > last);
    }
}

TEST_CASE("a set piece refuses what it could not honour, and says what", "[stage][setpiece][refusal]") {
    stage::SetPieceSpec spec = abduction();
    spec.overrides = {{"hoverHieght", 30.0f}};
    auto r = stage::instanceSetPiece(spec);
    REQUIRE_FALSE(r.has_value());
    CHECK_THAT(r.error().message, ContainsSubstring("no slot 'hoverHieght'"));

    spec.overrides = {{"hoverHeight", 400.0f}};
    r = stage::instanceSetPiece(spec);
    REQUIRE_FALSE(r.has_value());
    CHECK_THAT(r.error().message, ContainsSubstring("outside"));

    spec.overrides = {{"animals", 4.0f}};
    r = stage::instanceSetPiece(spec);
    REQUIRE_FALSE(r.has_value());

    spec.overrides.clear();
    spec.moment = "sweep"; // a survey's, not an abduction's
    r = stage::instanceSetPiece(spec);
    REQUIRE_FALSE(r.has_value());
    CHECK_THAT(r.error().message, ContainsSubstring("no moment 'sweep'"));

    stage::SetPieceSpec flyby = abduction();
    flyby.kind = stage::SetPieceKind::Flyby;
    flyby.overrides.clear();
    flyby.beamColor = glm::vec3(1.0f);
    r = stage::instanceSetPiece(flyby);
    REQUIRE_FALSE(r.has_value());
    CHECK_THAT(r.error().message, ContainsSubstring("shows no beam"));

    // Placed so early the craft would have to be taken before the film starts.
    stage::SetPieceSpec early = abduction();
    early.atSeconds = 5.0;
    r = stage::instanceSetPiece(early);
    REQUIRE_FALSE(r.has_value());
    CHECK_THAT(r.error().message, ContainsSubstring("before the film starts"));

    // The control: the same spec, placed later, instances.
    early.atSeconds = 30.0;
    CHECK(stage::instanceSetPiece(early).has_value());
}

TEST_CASE("a beat's clock and a step's component round-trip through the scene format", "[stage][setpiece][json]") {
    auto s = stage::instanceSetPiece([] {
        stage::SetPieceSpec spec = abduction(2);
        spec.beamColor = glm::vec3(1.0f, 0.3f, 0.1f);
        return spec;
    }());
    REQUIRE(s.has_value());
    const nlohmann::json j = stage::scenarioToJson(*s);
    auto back = stage::scenarioFromJson(j);
    REQUIRE(back.has_value());
    CHECK(stage::scenarioToJson(*back) == j);
    // The fields survived, not just the bytes: the clock on the beam, and a blue channel written to 2.
    CHECK(beat(*back, "beam")->startAt.param == "beamAt");
    bool blue = false;
    for (const stage::CueDesc& c : beat(*back, "transit")->cues) {
        for (const stage::StepDesc& st : c.steps) {
            blue = blue || (st.kind == stage::StepKind::Set && st.target == "colorStart" && st.component == 2 &&
                            st.to.param == "beamBlue");
        }
    }
    CHECK(blue);
    // A component outside a colour is refused by the reader rather than written to channel 0.
    nlohmann::json bad = j;
    bad["beats"][1]["cues"][2]["steps"][0]["component"] = 7;
    CHECK_FALSE(stage::scenarioFromJson(bad).has_value());
}
