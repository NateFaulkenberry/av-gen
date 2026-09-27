// ADR-924: a Director Plan's routes and sources, as a document. A route item carries a project route
// verbatim -- its delayMs and depthSource included -- so a plan round-trips byte for byte, a plan
// without them keeps the bytes it always had, and the reader tells a model when it invents a field.

#include "directing/plan.hpp"
#include "directing/plan_route.hpp"
#include "params/serialization.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;

namespace {

PlanRoute kickRoute() {
    PlanRoute r;
    r.key = "kick.elder-gills";
    r.level = ReactiveLevel::Meso;
    r.group = "hero-emission";
    r.owner = "elder-cap";
    r.layer = "kick";
    r.reason = "the elder's heartbeat";
    r.route.source = "audio.onsetLow";
    r.route.target = "nodes/elder-gills/emissiveBoost";
    r.route.op = params::ModOp::Add;
    r.route.amount = 0.6f;
    r.route.chain.delayMs = 35.0f;
    r.route.chain.attackMs = 5.0f;
    r.route.chain.decayMs = 300.0f;
    r.route.depthSource = "section.energy";
    r.route.depthMin = 0.25f;
    r.route.depthMax = 1.1f;
    return r;
}

PlanSource breath() {
    PlanSource s;
    s.key = "source.two-bar-breath";
    s.kind = "lfo";
    s.name = "two-bar-breath";
    s.settings = {{"shape", "sine"}};
    s.parameters = {{"beatSync", 1.0f}, {"beatsPerCycle", 8.0f}};
    s.reason = "a breath across two bars";
    return s;
}

Plan basePlan() {
    Plan p;
    p.id = "reactivity";
    p.title = "Reactivity";
    return p;
}

bool hasCode(const std::vector<Issue>& issues, IssueCode code, std::string_view location = {}) {
    return std::any_of(issues.begin(), issues.end(), [&](const Issue& i) {
        return i.code == code && (location.empty() || i.location == location);
    });
}

} // namespace

TEST_CASE("A plan's routes and sources round-trip byte for byte, and a plan without them keeps its bytes",
          "[directing][reactivity][adr924]") {
    Plan plan = basePlan();
    const json before = plan.toJson();
    CHECK_FALSE(before.contains("routes")); // the control: nothing new is written when there is nothing new
    CHECK_FALSE(before.contains("sources"));

    plan.routes.push_back(kickRoute());
    PlanRoute hue;
    hue.key = "sections.fungi";
    hue.level = ReactiveLevel::Macro;
    hue.route.source = "timeline.glowing-plants-hue-by-section";
    hue.route.target = "nodes/meadow/scatter/fungi/hueOffset";
    hue.route.chain.remapEnabled = true;
    hue.route.chain.remapOutMin = -0.08f;
    plan.routes.push_back(hue);
    plan.sources.push_back(breath());

    const json doc = plan.toJson();
    REQUIRE(doc.contains("routes"));
    REQUIRE(doc["routes"].size() == 2);
    // The route is a project route: every chain field, the delay first-class, the depth written.
    const json& route = doc["routes"][0]["route"];
    CHECK(route == params::routeToJson(kickRoute().route));
    CHECK(route["chain"]["delayMs"] == 35.0);
    CHECK(route["depthSource"] == "section.energy");
    CHECK_FALSE(route.contains("planItem"));
    CHECK(doc["routes"][0]["level"] == "meso");
    CHECK(doc["sources"][0]["parameters"]["beatsPerCycle"] == 8.0);

    const PlanParse parsed = parsePlan(doc);
    for (const Issue& i : parsed.issues) {
        INFO(i.toJson().dump());
    }
    REQUIRE(parsed.plan);
    CHECK(parsed.issues.empty());
    CHECK(*parsed.plan == plan);
    CHECK(parsed.plan->toJson().dump() == doc.dump()); // canonical: the same bytes again
}

TEST_CASE("A route item's shape is checked, and a field a model invents is named rather than ignored",
          "[directing][reactivity][adr924]") {
    const auto parse = [](json routeItem) {
        json doc = basePlan().toJson();
        doc["routes"] = json::array({std::move(routeItem)});
        return parsePlan(doc);
    };
    json good = planRouteToJson(kickRoute());

    SECTION("a missing route, source or target is refused") {
        json item = good;
        item.erase("route");
        CHECK(hasCode(parse(item).issues, IssueCode::SchemaInvalid, "/routes/0/route"));
        CHECK_FALSE(parse(item).plan);
        item = good;
        item["route"].erase("target");
        CHECK(hasCode(parse(item).issues, IssueCode::SchemaInvalid, "/routes/0/route/target"));
    }
    SECTION("a level outside micro, meso, macro is refused with the three") {
        json item = good;
        item["level"] = "mega";
        const PlanParse p = parse(item);
        REQUIRE(hasCode(p.issues, IssueCode::SchemaInvalid, "/routes/0/level"));
        const auto it = std::find_if(p.issues.begin(), p.issues.end(), [](const Issue& i) { return i.location == "/routes/0/level"; });
        CHECK(it->suggestions == std::vector<std::string>{"micro", "meso", "macro"});
    }
    SECTION("\"delay\" for \"delayMs\" is a warning that suggests the real name") {
        json item = good;
        item["route"]["chain"]["delay"] = 90;
        const PlanParse p = parse(item);
        REQUIRE(p.plan); // a warning, not a refusal
        const auto it = std::find_if(p.issues.begin(), p.issues.end(),
                                     [](const Issue& i) { return i.location == "/routes/0/route/chain/delay"; });
        REQUIRE(it != p.issues.end());
        CHECK(it->code == IssueCode::SchemaUnknownField);
        CHECK(std::find(it->suggestions.begin(), it->suggestions.end(), "delayMs") != it->suggestions.end());
    }
    SECTION("planItem is the compiler's: written in a plan it is ignored, with a word") {
        json item = good;
        item["route"]["planItem"] = "someone/else";
        const PlanParse p = parse(item);
        REQUIRE(p.plan);
        CHECK(hasCode(p.issues, IssueCode::SchemaUnknownField, "/routes/0/route/planItem"));
        CHECK(p.plan->routes[0].route.planItem.empty());
    }
    SECTION("keys are unique across every kind of item") {
        json doc = basePlan().toJson();
        doc["routes"] = json::array({good});
        doc["cues"] = json::array({json{{"key", good["key"]}, {"parameter", "test/flash"}, {"at", "0:01"}, {"value", 1.0}}});
        CHECK(hasCode(parsePlan(doc).issues, IssueCode::DuplicateKey));
        json twoSources = basePlan().toJson();
        twoSources["sources"] = json::array({planSourceToJson(breath())});
        json clash = planRouteToJson(kickRoute());
        clash["key"] = breath().key;
        twoSources["routes"] = json::array({clash});
        CHECK(hasCode(parsePlan(twoSources).issues, IssueCode::DuplicateKey));
    }
    SECTION("a source's name cannot carry a separator its signal and parameters are built from") {
        json doc = basePlan().toJson();
        json src = planSourceToJson(breath());
        src["name"] = "two.bar";
        doc["sources"] = json::array({src});
        CHECK(hasCode(parsePlan(doc).issues, IssueCode::SchemaInvalid, "/sources/0/name"));
    }
}

TEST_CASE("The plan contract lists routes and sources, and a route says which plan item made it only when one did",
          "[directing][reactivity][adr924]") {
    const json schema = planSchema();
    REQUIRE(schema["fields"].contains("routes"));
    REQUIRE(schema["fields"].contains("sources"));
    CHECK(schema["fields"]["routes"][0]["route"]["chain"].contains("delayMs"));
    CHECK(schema["fields"]["routes"][0]["route"].contains("depthSource"));

    params::ModRoute plain = kickRoute().route;
    CHECK_FALSE(params::routeToJson(plain).contains("planItem"));
    params::ModRoute made = plain;
    made.planItem = planItemId("reactivity", "kick.elder-gills");
    const json j = params::routeToJson(made);
    CHECK(j["planItem"] == "reactivity/kick.elder-gills");
    auto back = params::routeFromJson(j);
    REQUIRE(back);
    CHECK(back->planItem == made.planItem);
    CHECK(sameRoute(*back, made));
    const auto split = splitPlanItemId(made.planItem);
    REQUIRE(split);
    CHECK(split->first == "reactivity");
    CHECK(split->second == "kick.elder-gills");
    CHECK_FALSE(splitPlanItemId("no-slash"));
}
