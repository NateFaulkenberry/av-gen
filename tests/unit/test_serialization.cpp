#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/preset.hpp"
#include "params/serialization.hpp"
#include "signals/source.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
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

// ---- version 2: polarity, sources, presets --------------------------------------------------

TEST_CASE("Route polarity round-trips and defaults to unipolar", "[serialization][v2]") {
    ModRoute route;
    route.source = "lfo.a";
    route.target = "orb/scale";
    route.polarity = Polarity::Bipolar;
    const json j = routeToJson(route);
    CHECK(j["polarity"] == "bipolar");
    auto back = routeFromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->polarity == Polarity::Bipolar);

    auto v1 = routeFromJson(json{{"source", "s"}, {"target", "t"}}); // v1 documents have no key
    REQUIRE(v1.has_value());
    CHECK(v1->polarity == Polarity::Unipolar);
    auto uni = routeFromJson(json{{"source", "s"}, {"target", "t"}, {"polarity", "unipolar"}});
    REQUIRE(uni.has_value());
    CHECK(uni->polarity == Polarity::Unipolar);
    CHECK_FALSE(routeFromJson(json{{"source", "s"}, {"target", "t"}, {"polarity", "Bipolar"}}).has_value());
    CHECK_FALSE(routeFromJson(json{{"source", "s"}, {"target", "t"}, {"polarity", 1}}).has_value());
}

namespace {
struct V2Fixture : Fixture {
    signals::SignalBus bus;
    signals::SourceRack rack;
    PresetBank bank;

    V2Fixture() {
        rack.attach(bus, params);
        rack.add(std::make_unique<signals::LfoSource>("wobble", signals::LfoShape::Triangle));
        auto timeline = std::make_unique<signals::TimelineSource>("intro");
        timeline->addKey(signals::Keyframe{0.0, 0.0f, signals::KeyInterpolation::Smooth});
        timeline->addKey(signals::Keyframe{2.0, 1.0f, signals::KeyInterpolation::Step});
        rack.add(std::move(timeline));
        auto macro = std::make_unique<signals::MacroSource>();
        macro->addKnob("energy", 0.7f);
        rack.add(std::move(macro));
        scale.setBase(2.0f);
        bank.add(capturePreset(params, "calm"));
        scale.setBase(3.5f);
        bank.add(capturePreset(params, "wild"));
    }
};
} // namespace

TEST_CASE("Version 2 project round-trips sources, presets and source parameters", "[serialization][v2]") {
    V2Fixture f;
    f.params.findAs<float>("sources/wobble/rate")->setBase(4.0f);
    f.params.findAs<float>("macros/energy")->setBase(0.2f);
    ModRoute r;
    r.source = "lfo.wobble.bipolar";
    r.target = "sources/intro/scale";
    r.polarity = Polarity::Bipolar;
    f.modulator.addRoute(r);

    const json doc = saveProject(f.params, f.modulator, &f.rack, &f.bank);
    CHECK(doc["version"] == kProjectFormatVersion);
    REQUIRE(doc["sources"].is_array());
    CHECK(doc["sources"].size() == 3);
    REQUIRE(doc["presets"].is_array());
    CHECK(doc["presets"].size() == 2);
    CHECK(doc["parameters"]["sources/wobble/rate"] == 4.0);
    CHECK_THAT(doc["parameters"]["macros/energy"].get<double>(), WithinAbs(0.2, 1e-6));
    CHECK(doc["routes"][0]["polarity"] == "bipolar");
    // Omitted pointers omit the sections.
    const json bare = saveProject(f.params, f.modulator);
    CHECK_FALSE(bare.contains("sources"));
    CHECK_FALSE(bare.contains("presets"));

    Fixture g;
    signals::SignalBus bus;
    signals::SourceRack rack;
    PresetBank bank;
    rack.attach(bus, g.params);
    rack.add(std::make_unique<signals::NoiseSource>("stale"));
    bank.add(Preset{.name = "stale", .values = {}});
    REQUIRE(loadProject(doc, g.params, g.modulator, &rack, &bank).has_value());

    // Rack replaced and attached, so the source parameters existed when values were applied.
    CHECK(rack.sources().size() == 3);
    CHECK(rack.find("noise", "stale") == nullptr);
    CHECK(g.params.find("sources/stale/rate") == nullptr);
    REQUIRE(g.params.findAs<float>("sources/wobble/rate") != nullptr);
    CHECK_THAT(d(g.params.findAs<float>("sources/wobble/rate")->base()), WithinAbs(4.0, 1e-6));
    CHECK_THAT(d(g.params.findAs<float>("macros/energy")->base()), WithinAbs(0.2, 1e-6));
    CHECK(dynamic_cast<signals::LfoSource*>(rack.find("lfo", "wobble"))->shape() ==
          signals::LfoShape::Triangle);
    CHECK(dynamic_cast<signals::TimelineSource*>(rack.find("timeline", "intro"))->keys().size() == 2);
    CHECK(rack.toJson() == doc["sources"]);
    CHECK_THAT(d(g.scale.base()), WithinAbs(3.5, 1e-6));
    REQUIRE(g.modulator.routes().size() == 1);
    CHECK(g.modulator.routes()[0].polarity == Polarity::Bipolar);
    CHECK(bank.presets().size() == 2);
    CHECK(bank.find("stale") == nullptr);
    REQUIRE(bank.find("calm") != nullptr);
    CHECK(bank.find("calm")->values.at("orb/scale")[0] == 2.0f);
    CHECK(bank.toJson() == doc["presets"]);
    // Presets apply to the rebuilt parameter set.
    CHECK(applyPreset(g.params, *bank.find("calm")) > 0);
    CHECK_THAT(d(g.scale.base()), WithinAbs(2.0, 1e-6));
}

TEST_CASE("Version 1 documents still load and clear the optional sections", "[serialization][v2]") {
    V2Fixture f;
    json doc = saveProject(f.params, f.modulator);
    doc["version"] = 1;
    doc["parameters"]["orb/scale"] = 1.25;
    doc["routes"] = json::array({json{{"source", "audio.bass"}, {"target", "orb/scale"}}});
    REQUIRE(loadProject(doc, f.params, f.modulator, &f.rack, &f.bank).has_value());
    CHECK_THAT(d(f.scale.base()), WithinAbs(1.25, 1e-6));
    REQUIRE(f.modulator.routes().size() == 1);
    CHECK(f.modulator.routes()[0].polarity == Polarity::Unipolar);
    CHECK(f.rack.sources().empty()); // a project without sources has none
    CHECK(f.params.find("sources/wobble/rate") == nullptr);
    CHECK(f.bank.presets().empty());

    // Null pointers leave the rack and bank alone.
    V2Fixture h;
    REQUIRE(loadProject(doc, h.params, h.modulator).has_value());
    CHECK(h.rack.sources().size() == 3);
    CHECK(h.bank.presets().size() == 2);
}

TEST_CASE("Documents newer than the supported version are rejected", "[serialization][v2]") {
    V2Fixture f;
    json doc = saveProject(f.params, f.modulator, &f.rack, &f.bank);
    doc["version"] = kProjectFormatVersion + 1;
    auto result = loadProject(doc, f.params, f.modulator, &f.rack, &f.bank);
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message,
               ContainsSubstring("version " + std::to_string(kProjectFormatVersion + 1)));
    doc["version"] = 0;
    CHECK_FALSE(loadProject(doc, f.params, f.modulator, &f.rack, &f.bank).has_value());
    CHECK(f.rack.sources().size() == 3);
}

TEST_CASE("A failed version 2 load leaves parameters, routes, sources and presets unchanged",
          "[serialization][v2]") {
    V2Fixture f;
    f.modulator.addRoute(ModRoute{.source = "audio.bass", .target = "orb/scale"});
    const json paramsBefore = saveProject(f.params, f.modulator)["parameters"];
    const json sourcesBefore = f.rack.toJson();
    const json presetsBefore = f.bank.toJson();

    json good = saveProject(f.params, f.modulator, &f.rack, &f.bank);
    good["parameters"]["orb/scale"] = 0.5;
    good["sources"] = json::array({json{{"kind", "noise"}, {"name", "n"}}});
    good["presets"] = json::array({json{{"name", "p"}}});
    good["routes"] = json::array();

    auto check = [&](json doc, const char* what) {
        INFO(what);
        auto result = loadProject(doc, f.params, f.modulator, &f.rack, &f.bank);
        CHECK_FALSE(result.has_value());
        CHECK(saveProject(f.params, f.modulator)["parameters"] == paramsBefore);
        CHECK(f.modulator.routes().size() == 1);
        CHECK(f.rack.toJson() == sourcesBefore);
        CHECK(f.params.find("sources/wobble/rate") != nullptr);
        CHECK(f.params.find("sources/n/rate") == nullptr);
        CHECK(f.bank.toJson() == presetsBefore);
    };
    json bad = good;
    bad["sources"] = 7;
    check(bad, "sources not an array");
    bad = good;
    bad["sources"].push_back(json{{"kind", "lfo"}, {"name", "x"}, {"settings", json{{"shape", "blob"}}}});
    check(bad, "malformed source settings");
    bad = good;
    bad["presets"] = json::object();
    check(bad, "presets not an array");
    bad = good;
    bad["presets"].push_back(json{{"values", json::object()}});
    check(bad, "preset without a name");
    bad = good;
    bad["routes"].push_back(json{{"source", "s"}, {"target", "t"}, {"polarity", "sideways"}});
    check(bad, "bad polarity");
    bad = good;
    bad["parameters"]["orb/flag"] = "yes";
    check(bad, "parameter type mismatch");
    // Malformed optional sections are rejected even when the caller does not load them.
    bad = good;
    bad["sources"] = 7;
    CHECK_FALSE(loadProject(bad, f.params, f.modulator).has_value());

    // The good document then loads.
    REQUIRE(loadProject(good, f.params, f.modulator, &f.rack, &f.bank).has_value());
    CHECK_THAT(d(f.scale.base()), WithinAbs(0.5, 1e-6));
    CHECK(f.modulator.routes().empty());
    CHECK(f.rack.sources().size() == 1);
    CHECK(f.params.find("sources/n/rate") != nullptr);
    CHECK(f.params.find("sources/wobble/rate") == nullptr);
    CHECK(f.bank.presets().size() == 1);
}

TEST_CASE("Version 2 project files round-trip on disk", "[serialization][v2][file]") {
    const auto path = tempPath("project_v2.json");
    {
        V2Fixture f;
        REQUIRE(saveProjectFile(path, f.params, f.modulator, &f.rack, &f.bank).has_value());
    }
    {
        Fixture g;
        signals::SignalBus bus;
        signals::SourceRack rack;
        PresetBank bank;
        rack.attach(bus, g.params);
        REQUIRE(loadProjectFile(path, g.params, g.modulator, &rack, &bank).has_value());
        CHECK(rack.sources().size() == 3);
        CHECK(bank.presets().size() == 2);
        CHECK_THAT(d(g.scale.base()), WithinAbs(3.5, 1e-6));
    }
    std::filesystem::remove(path);
}

// ---- version 4: explicit migration ------------------------------------------------------------

namespace {
json v1Document() {
    return json{
        {"format", kProjectFormatName},
        {"version", 1},
        {"parameters", json{{"orb/scale", 1.25}, {"orb/count", 4}}},
        {"routes", json::array({json{{"source", "audio.bass"}, {"target", "orb/scale"}},
                                json{{"source", "audio.mid"}, {"target", "orb/count"}, {"amount", 0.5}}})},
        {"customTool", json{{"note", "kept"}}}};
}
} // namespace

TEST_CASE("A version 1 document migrates to the current version in three steps", "[serialization][v4]") {
    json doc = v1Document();
    auto report = migrateProject(doc);
    REQUIRE(report.has_value());
    CHECK(report->fromVersion == 1);
    CHECK(report->toVersion == kProjectFormatVersion);
    REQUIRE(report->steps.size() == 3);
    CHECK_THAT(report->steps[0], ContainsSubstring("1 -> 2"));
    CHECK_THAT(report->steps[1], ContainsSubstring("2 -> 3"));
    CHECK_THAT(report->steps[2], ContainsSubstring("3 -> 4"));

    CHECK(doc["version"] == kProjectFormatVersion);
    CHECK(doc["format"] == kProjectFormatName);
    REQUIRE(doc["routes"].size() == 2);
    CHECK(doc["routes"][0]["polarity"] == "unipolar");
    CHECK(doc["routes"][1]["polarity"] == "unipolar");
    CHECK(doc["routes"][1]["amount"] == 0.5);
    CHECK(doc["sources"] == json::array());
    CHECK(doc["presets"] == json::array());
    CHECK(doc["shaders"] == json::array());
    CHECK_FALSE(doc.contains("timeline"));
    CHECK(doc["assets"] == json::object());
    CHECK(doc["app"]["name"] == "avgen");
    CHECK(doc["app"]["version"] == "unknown");
    CHECK(doc["customTool"]["note"] == "kept"); // unknown keys survive
    CHECK(doc["parameters"]["orb/scale"] == 1.25);

    // The migrated document loads with the polarity the migration wrote.
    Fixture f;
    REQUIRE(loadProject(doc, f.params, f.modulator).has_value());
    CHECK_THAT(d(f.scale.base()), WithinAbs(1.25, 1e-6));
    REQUIRE(f.modulator.routes().size() == 2);
    CHECK(f.modulator.routes()[0].polarity == Polarity::Unipolar);
    CHECK(f.modulator.routes()[1].polarity == Polarity::Unipolar);
}

TEST_CASE("A version 2 document gains shaders, assets and app", "[serialization][v4]") {
    json doc =
        json{{"format", kProjectFormatName},
             {"version", 2},
             {"parameters", json::object()},
             {"routes", json::array({json{{"source", "s"}, {"target", "t"}, {"polarity", "bipolar"}}})},
             {"sources", json::array({json{{"kind", "noise"}, {"name", "n"}}})},
             {"presets", json::array()}};
    auto report = migrateProject(doc);
    REQUIRE(report.has_value());
    CHECK(report->fromVersion == 2);
    CHECK(report->steps.size() == 2);
    CHECK(doc["version"] == kProjectFormatVersion);
    CHECK(doc["shaders"] == json::array());
    CHECK(doc["assets"] == json::object());
    CHECK(doc["app"]["name"] == "avgen");
    CHECK(doc["routes"][0]["polarity"] == "bipolar"); // existing values are not overwritten
    CHECK(doc["sources"].size() == 1);

    // Existing keys are kept on every hop.
    json custom = json{{"format", kProjectFormatName},
                       {"version", 3},
                       {"shaders", json::array({json{{"name", "x"}}})},
                       {"assets", json{{"audio", "a.wav"}}},
                       {"app", json{{"name", "other"}, {"version", "9"}}}};
    REQUIRE(migrateProject(custom).has_value());
    CHECK(custom["shaders"].size() == 1);
    CHECK(custom["assets"]["audio"] == "a.wav");
    CHECK(custom["app"]["name"] == "other");
}

TEST_CASE("A current document reports no migration steps", "[serialization][v4]") {
    Fixture f;
    json doc = saveProject(f.params, f.modulator);
    REQUIRE(doc["version"] == kProjectFormatVersion);
    const json before = doc;
    auto report = migrateProject(doc);
    REQUIRE(report.has_value());
    CHECK(report->fromVersion == kProjectFormatVersion);
    CHECK(report->toVersion == kProjectFormatVersion);
    CHECK(report->steps.empty());
    CHECK(doc == before);
}

TEST_CASE("migrateProject rejects newer versions, bad envelopes and non-objects", "[serialization][v4]") {
    json newer = json{{"format", kProjectFormatName}, {"version", 5}};
    auto result = migrateProject(newer);
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("version 5"));
    CHECK(newer["version"] == 5); // untouched on failure

    json array = json::array();
    CHECK_FALSE(migrateProject(array).has_value());
    json number = json(3);
    CHECK_FALSE(migrateProject(number).has_value());
    json wrongFormat = json{{"format", "avgen-scene"}, {"version", 1}};
    auto wrong = migrateProject(wrongFormat);
    REQUIRE_FALSE(wrong.has_value());
    CHECK_THAT(wrong.error().message, ContainsSubstring("format"));
    json noVersion = json{{"format", kProjectFormatName}};
    CHECK_FALSE(migrateProject(noVersion).has_value());
    json stringVersion = json{{"format", kProjectFormatName}, {"version", "1"}};
    CHECK_FALSE(migrateProject(stringVersion).has_value());
    json zero = json{{"format", kProjectFormatName}, {"version", 0}};
    CHECK_FALSE(migrateProject(zero).has_value());
}

TEST_CASE("loadProject migrates a copy and leaves the caller's document unchanged", "[serialization][v4]") {
    const json doc = v1Document();
    const json before = doc;
    Fixture f;
    REQUIRE(loadProject(doc, f.params, f.modulator).has_value());
    CHECK(doc == before);
    CHECK(doc["version"] == 1);
    CHECK_FALSE(doc.contains("shaders"));
    CHECK_FALSE(doc["routes"][0].contains("polarity"));
    CHECK_THAT(d(f.scale.base()), WithinAbs(1.25, 1e-6));
    CHECK(f.count.base() == 4);
    REQUIRE(f.modulator.routes().size() == 2);
    CHECK(f.modulator.routes()[0].polarity == Polarity::Unipolar);

    // A version 5 document is rejected with the same message as before.
    json newer = saveProject(f.params, f.modulator);
    newer["version"] = 5;
    auto rejected = loadProject(newer, f.params, f.modulator);
    REQUIRE_FALSE(rejected.has_value());
    CHECK_THAT(rejected.error().message, ContainsSubstring("version 5"));
}

TEST_CASE("loadProject keeps the routes a subsystem installed and drops the rest", "[serialization][macro]") {
    // ADR-522. `loadProject` replaces the AUTHORED routes and keeps the ones a subsystem owns,
    // because the owner puts them back and a saved copy would duplicate on every save. Two owners
    // were flagged -- a procedural graph (ADR-028) and an entity's reactions (ADR-088) -- and the
    // third, a world macro, was not: its routes were installed by `Engine::applyWorldMacros`,
    // bound, counted, and then erased sixty lines later by the second parameter pass.
    //
    // examples/machine/machine.json loaded with exactly its twelve authored LFO and audio routes
    // and none of its three macros' eight. The knobs existed, appeared in the panel and took part
    // in cue presets, and moved nothing at all.
    Fixture f;
    const json doc = saveProject(f.params, f.modulator); // no routes of its own

    Fixture g;
    ModRoute authored;
    authored.source = "audio.bass";
    authored.target = "orb/scale";
    ModRoute graph = authored;
    graph.fromGraph = true;
    ModRoute entity = authored;
    entity.fromEntity = true;
    ModRoute macro = authored;
    macro.source = "macro.season";
    macro.fromMacro = true;
    macro.chain.remapEnabled = true;
    g.modulator.addRoute(authored);
    g.modulator.addRoute(graph);
    g.modulator.addRoute(entity);
    g.modulator.addRoute(macro);
    // The premise: all four are there before the load, so "three survived" is a survival and not
    // an initial condition (ADR-182).
    REQUIRE(g.modulator.routes().size() == 4);

    REQUIRE(loadProject(doc, g.params, g.modulator).has_value());

    std::size_t graphs = 0;
    std::size_t entities = 0;
    std::size_t macros = 0;
    std::size_t authoredLeft = 0;
    for (const ModRoute& r : g.modulator.routes()) {
        graphs += r.fromGraph ? 1 : 0;
        entities += r.fromEntity ? 1 : 0;
        macros += r.fromMacro ? 1 : 0;
        authoredLeft += (!r.fromGraph && !r.fromEntity && !r.fromMacro) ? 1 : 0;
    }
    CHECK(graphs == 1);
    CHECK(entities == 1);
    CHECK(macros == 1);   // the one this ADR is about
    CHECK(authoredLeft == 0); // and the authored one really was replaced
}

TEST_CASE("saveProject does not write the routes a subsystem owns", "[serialization][macro]") {
    // The other half: an owner rebuilds its routes on every load, so writing them would give a
    // project a duplicate of each one every time it was saved -- and a route the author cannot
    // delete, because the owner puts it straight back.
    Fixture f;
    ModRoute authored;
    authored.source = "audio.bass";
    authored.target = "orb/scale";
    ModRoute macro = authored;
    macro.source = "macro.season";
    macro.fromMacro = true;
    f.modulator.addRoute(authored);
    f.modulator.addRoute(macro);
    REQUIRE(f.modulator.routes().size() == 2);

    const json doc = saveProject(f.params, f.modulator);
    REQUIRE(doc["routes"].is_array());
    CHECK(doc["routes"].size() == 1);
    CHECK(doc["routes"][0]["source"] == "audio.bass");
}
