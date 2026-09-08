#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/serialization.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace avgen;
using namespace avgen::params;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using nlohmann::json;

namespace {
double d(float v) {
    return static_cast<double>(v);
}

std::filesystem::path tempPath(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("avgen_params_test_") + name);
}

struct Fixture {
    ParameterSet params;
    Modulator modulator;
    Parameter<float>& scale;
    Parameter<int>& count;
    Parameter<bool>& flag;
    Parameter<glm::vec3>& color;
    Parameter<float>& hidden;

    Fixture()
        : scale(params.add(
              ParamDesc<float>{.path = "orb/scale", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 4.0f}))
        , count(
              params.add(ParamDesc<int>{.path = "orb/count", .defaultValue = 3, .hardMin = 0, .hardMax = 10}))
        , flag(params.add(
              ParamDesc<bool>{.path = "orb/flag", .defaultValue = false, .hardMin = false, .hardMax = true}))
        , color(params.add(ParamDesc<glm::vec3>{.path = "orb/color",
                                                .defaultValue = glm::vec3(0.5f),
                                                .hardMin = glm::vec3(0.0f),
                                                .hardMax = glm::vec3(1.0f),
                                                .isColor = true}))
        , hidden(params.add(ParamDesc<float>{.path = "internal/x",
                                             .defaultValue = 0.0f,
                                             .hardMin = 0.0f,
                                             .hardMax = 1.0f,
                                             .flags = ParamFlags{.serialized = false}})) {}
};

ProcessorChain fancyChain() {
    ProcessorChain c;
    c.gain = 1.5f;
    c.offset = -0.1f;
    c.curve = CurveType::SCurve;
    c.curveAmount = 6.0f;
    c.clampEnabled = true;
    c.clampMin = 0.1f;
    c.clampMax = 0.9f;
    c.threshold = ThresholdMode::Subtract;
    c.thresholdLevel = 0.2f;
    c.attackMs = 15.0f;
    c.decayMs = 180.0f;
    c.envelope = EnvelopeMode::PeakHold;
    c.envelopeHoldMs = 30.0f;
    c.envelopeFallPerSecond = 2.5f;
    c.remapEnabled = true;
    c.remapInMin = 0.0f;
    c.remapInMax = 0.8f;
    c.remapOutMin = -1.0f;
    c.remapOutMax = 2.0f;
    return c;
}

void checkChainEqual(const ProcessorChain& a, const ProcessorChain& b) {
    CHECK(a.gain == b.gain);
    CHECK(a.offset == b.offset);
    CHECK(a.curve == b.curve);
    CHECK(a.curveAmount == b.curveAmount);
    CHECK(a.clampEnabled == b.clampEnabled);
    CHECK(a.clampMin == b.clampMin);
    CHECK(a.clampMax == b.clampMax);
    CHECK(a.threshold == b.threshold);
    CHECK(a.thresholdLevel == b.thresholdLevel);
    CHECK(a.attackMs == b.attackMs);
    CHECK(a.decayMs == b.decayMs);
    CHECK(a.envelope == b.envelope);
    CHECK(a.envelopeHoldMs == b.envelopeHoldMs);
    CHECK(a.envelopeFallPerSecond == b.envelopeFallPerSecond);
    CHECK(a.remapEnabled == b.remapEnabled);
    CHECK(a.remapInMin == b.remapInMin);
    CHECK(a.remapInMax == b.remapInMax);
    CHECK(a.remapOutMin == b.remapOutMin);
    CHECK(a.remapOutMax == b.remapOutMax);
}
} // namespace

TEST_CASE("Parameter values round-trip through JSON", "[serialization]") {
    Fixture f;
    f.scale.setBase(2.5f);
    f.count.setBase(7);
    f.flag.setBase(true);
    f.color.setBase(glm::vec3(0.1f, 0.2f, 0.3f));

    const json js = parameterToJson(f.scale);
    const json jc = parameterToJson(f.count);
    const json jf = parameterToJson(f.flag);
    const json jv = parameterToJson(f.color);
    CHECK(js.is_number_float());
    CHECK(jc.is_number_integer());
    CHECK(jc.get<int>() == 7);
    CHECK(jf.is_boolean());
    CHECK(jf.get<bool>());
    REQUIRE(jv.is_array());
    CHECK(jv.size() == 3);

    f.params.resetAllToDefault();
    REQUIRE(parameterFromJson(f.scale, js).has_value());
    REQUIRE(parameterFromJson(f.count, jc).has_value());
    REQUIRE(parameterFromJson(f.flag, jf).has_value());
    REQUIRE(parameterFromJson(f.color, jv).has_value());
    CHECK_THAT(d(f.scale.base()), WithinAbs(2.5, 1e-6));
    CHECK(f.count.base() == 7);
    CHECK(f.flag.base());
    CHECK_THAT(d(f.color.base().y), WithinAbs(0.2, 1e-6));

    // Type mismatches are errors.
    CHECK_FALSE(parameterFromJson(f.scale, json("text")).has_value());
    CHECK_FALSE(parameterFromJson(f.flag, json(1)).has_value());
    CHECK_FALSE(parameterFromJson(f.color, json::array({0.1, 0.2})).has_value());
    CHECK_FALSE(parameterFromJson(f.color, json::array({0.1, "a", 0.3})).has_value());
    CHECK_FALSE(parameterFromJson(f.count, json(true)).has_value());
    // Out-of-range values are clamped.
    REQUIRE(parameterFromJson(f.scale, json(99.0)).has_value());
    CHECK(f.scale.base() == 4.0f);
}

TEST_CASE("Processor chain round-trips with lower-case enum strings", "[serialization]") {
    const ProcessorChain chain = fancyChain();
    const json j = chainToJson(chain);
    CHECK(j["curve"] == "scurve");
    CHECK(j["threshold"] == "subtract");
    CHECK(j["envelope"] == "peakhold");
    CHECK(j["clampEnabled"] == true);
    auto back = chainFromJson(j);
    REQUIRE(back.has_value());
    checkChainEqual(*back, chain);

    // All enum spellings.
    for (const char* name : {"linear", "power", "log", "exp", "scurve"}) {
        CHECK(chainFromJson(json{{"curve", name}}).has_value());
    }
    for (const char* name : {"none", "gate", "binary", "subtract"}) {
        CHECK(chainFromJson(json{{"threshold", name}}).has_value());
    }
    for (const char* name : {"none", "peakhold", "linearfall"}) {
        CHECK(chainFromJson(json{{"envelope", name}}).has_value());
    }
}

TEST_CASE("Processor chain tolerates missing keys and rejects bad values", "[serialization]") {
    auto empty = chainFromJson(json::object());
    REQUIRE(empty.has_value());
    checkChainEqual(*empty, ProcessorChain{});

    auto partial = chainFromJson(json{{"attackMs", 20.0}, {"curve", "power"}});
    REQUIRE(partial.has_value());
    CHECK(partial->attackMs == 20.0f);
    CHECK(partial->curve == CurveType::Power);
    CHECK(partial->decayMs == 0.0f);

    auto badEnum = chainFromJson(json{{"curve", "wavy"}});
    REQUIRE_FALSE(badEnum.has_value());
    CHECK_THAT(badEnum.error().message, ContainsSubstring("wavy"));
    CHECK_FALSE(chainFromJson(json{{"envelope", "PeakHold"}}).has_value()); // case-sensitive
    CHECK_FALSE(chainFromJson(json{{"gain", "loud"}}).has_value());
    CHECK_FALSE(chainFromJson(json{{"clampEnabled", 1}}).has_value());
    CHECK_FALSE(chainFromJson(json(3)).has_value());
}

TEST_CASE("Routes round-trip through JSON", "[serialization]") {
    ModRoute route;
    route.source = "audio.bass";
    route.target = "orb/color";
    route.component = 2;
    route.amount = -0.75f;
    route.op = ModOp::Multiply;
    route.enabled = false;
    route.chain = fancyChain();

    const json j = routeToJson(route);
    CHECK(j["source"] == "audio.bass");
    CHECK(j["target"] == "orb/color");
    CHECK(j["component"] == 2);
    CHECK(j["op"] == "multiply");
    CHECK(j["enabled"] == false);
    CHECK(j.contains("chain"));

    auto back = routeFromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->source == "audio.bass");
    CHECK(back->target == "orb/color");
    CHECK(back->component == 2);
    CHECK(back->amount == -0.75f);
    CHECK(back->op == ModOp::Multiply);
    CHECK_FALSE(back->enabled);
    checkChainEqual(back->chain, route.chain);
    CHECK(back->targetParam == nullptr);

    for (const char* name : {"add", "multiply", "replace", "min", "max"}) {
        auto r = routeFromJson(json{{"source", "s"}, {"target", "t"}, {"op", name}});
        INFO(name);
        CHECK(r.has_value());
    }
    // Defaults for missing optional keys.
    auto minimal = routeFromJson(json{{"source", "audio.mid"}, {"target", "orb/scale"}});
    REQUIRE(minimal.has_value());
    CHECK(minimal->component == -1);
    CHECK(minimal->amount == 1.0f);
    CHECK(minimal->op == ModOp::Add);
    CHECK(minimal->enabled);
    // Missing required keys and bad enums.
    CHECK_FALSE(routeFromJson(json{{"target", "orb/scale"}}).has_value());
    CHECK_FALSE(routeFromJson(json{{"source", "audio.mid"}}).has_value());
    CHECK_FALSE(routeFromJson(json{{"source", "s"}, {"target", "t"}, {"op", "divide"}}).has_value());
    CHECK_FALSE(
        routeFromJson(json{{"source", "s"}, {"target", "t"}, {"chain", {{"curve", "nope"}}}}).has_value());
}

TEST_CASE("Project round-trip restores base values and routes", "[serialization]") {
    Fixture f;
    f.scale.setBase(3.0f);
    f.count.setBase(8);
    f.flag.setBase(true);
    f.color.setBase(glm::vec3(0.9f, 0.8f, 0.7f));
    f.hidden.setBase(0.5f);
    ModRoute r1;
    r1.source = "audio.bass";
    r1.target = "orb/scale";
    r1.amount = 1.2f;
    r1.chain = fancyChain();
    ModRoute r2;
    r2.source = "audio.onset";
    r2.target = "orb/color";
    r2.component = 1;
    r2.op = ModOp::Max;
    f.modulator.addRoute(r1);
    f.modulator.addRoute(r2);

    const json doc = saveProject(f.params, f.modulator);
    CHECK(doc["format"] == kProjectFormatName);
    CHECK(doc["version"] == kProjectFormatVersion);
    CHECK(doc["parameters"].contains("orb/scale"));
    CHECK_FALSE(doc["parameters"].contains("internal/x")); // flags.serialized == false
    REQUIRE(doc["routes"].is_array());
    CHECK(doc["routes"].size() == 2);

    Fixture g;
    g.modulator.addRoute(r2); // replaced by load
    REQUIRE(loadProject(doc, g.params, g.modulator).has_value());
    CHECK_THAT(d(g.scale.base()), WithinAbs(3.0, 1e-6));
    CHECK(g.count.base() == 8);
    CHECK(g.flag.base());
    CHECK_THAT(d(g.color.base().z), WithinAbs(0.7, 1e-6));
    CHECK(g.hidden.base() == 0.0f);
    REQUIRE(g.modulator.routes().size() == 2);
    CHECK(g.modulator.routes()[0].source == "audio.bass");
    CHECK(g.modulator.routes()[0].amount == 1.2f);
    checkChainEqual(g.modulator.routes()[0].chain, fancyChain());
    CHECK(g.modulator.routes()[1].op == ModOp::Max);
    CHECK(g.modulator.routes()[1].component == 1);
    CHECK_FALSE(g.modulator.bound());
}

TEST_CASE("loadProject rejects wrong format and newer versions", "[serialization]") {
    Fixture f;
    json doc = saveProject(f.params, f.modulator);
    doc["format"] = "something-else";
    CHECK_FALSE(loadProject(doc, f.params, f.modulator).has_value());
    doc = saveProject(f.params, f.modulator);
    doc["version"] = kProjectFormatVersion + 1;
    auto newer = loadProject(doc, f.params, f.modulator);
    REQUIRE_FALSE(newer.has_value());
    CHECK_THAT(newer.error().message, ContainsSubstring("version"));
    doc.erase("version");
    CHECK_FALSE(loadProject(doc, f.params, f.modulator).has_value());
    CHECK_FALSE(loadProject(json::array(), f.params, f.modulator).has_value());
    CHECK_FALSE(loadProject(json{{"format", kProjectFormatName}, {"version", 1}, {"parameters", 3}}, f.params,
                            f.modulator)
                    .has_value());
    CHECK_FALSE(loadProject(json{{"format", kProjectFormatName}, {"version", 1}, {"routes", 3}}, f.params,
                            f.modulator)
                    .has_value());
}

TEST_CASE("loadProject ignores unknown paths and reports type mismatches", "[serialization]") {
    Fixture f;
    json doc = saveProject(f.params, f.modulator);
    doc["parameters"]["does/not/exist"] = 5.0;
    doc["parameters"]["orb/scale"] = 2.0;
    REQUIRE(loadProject(doc, f.params, f.modulator).has_value()); // unknown path -> warning only
    CHECK_THAT(d(f.scale.base()), WithinAbs(2.0, 1e-6));

    doc["parameters"]["orb/scale"] = 3.0;
    doc["parameters"]["orb/flag"] = "yes";
    auto mismatch = loadProject(doc, f.params, f.modulator);
    REQUIRE_FALSE(mismatch.has_value());
    CHECK_THAT(mismatch.error().message, ContainsSubstring("orb/flag"));
    CHECK_THAT(d(f.scale.base()), WithinAbs(2.0, 1e-6)); // nothing applied on failure
}

TEST_CASE("Project files round-trip on disk and malformed files are errors", "[serialization][file]") {
    const auto path = tempPath("project.json");
    {
        Fixture f;
        f.scale.setBase(1.75f);
        ModRoute r;
        r.source = "audio.rms";
        r.target = "orb/scale";
        r.op = ModOp::Min;
        f.modulator.addRoute(r);
        REQUIRE(saveProjectFile(path, f.params, f.modulator).has_value());
    }
    {
        Fixture g;
        REQUIRE(loadProjectFile(path, g.params, g.modulator).has_value());
        CHECK_THAT(d(g.scale.base()), WithinAbs(1.75, 1e-6));
        REQUIRE(g.modulator.routes().size() == 1);
        CHECK(g.modulator.routes()[0].op == ModOp::Min);
    }
    std::filesystem::remove(path);

    const auto bad = tempPath("broken.json");
    {
        std::ofstream out(bad);
        out << "{ \"format\": \"avgen-project\", \"version\": 1, ";
    }
    Fixture h;
    auto broken = loadProjectFile(bad, h.params, h.modulator);
    REQUIRE_FALSE(broken.has_value());
    CHECK_THAT(broken.error().message, ContainsSubstring("JSON"));
    std::filesystem::remove(bad);

    CHECK_FALSE(
        loadProjectFile(tempPath("missing-dir") / "x" / "nope.json", h.params, h.modulator).has_value());
    CHECK_FALSE(
        saveProjectFile(tempPath("missing-dir") / "x" / "nope.json", h.params, h.modulator).has_value());
}
