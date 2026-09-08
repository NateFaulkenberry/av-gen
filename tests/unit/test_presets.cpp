#include "params/parameter_set.hpp"
#include "params/preset.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

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

struct Fixture {
    ParameterSet params;
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
} // namespace

TEST_CASE("capturePreset snapshots serialised base values", "[presets]") {
    Fixture f;
    f.scale.setBase(2.5f);
    f.count.setBase(7);
    f.flag.setBase(true);
    f.color.setBase(glm::vec3(0.1f, 0.2f, 0.3f));
    f.scale.setFinalComponent(0, 3.9f); // finals are not captured
    f.hidden.setBase(0.7f);

    const Preset preset = capturePreset(f.params, "bright");
    CHECK(preset.name == "bright");
    CHECK(preset.values.size() == 4);
    CHECK_FALSE(preset.values.contains("internal/x"));
    REQUIRE(preset.values.at("orb/scale").size() == 1);
    CHECK(preset.values.at("orb/scale")[0] == 2.5f);
    CHECK(preset.values.at("orb/count")[0] == 7.0f);
    CHECK(preset.values.at("orb/flag")[0] == 1.0f);
    REQUIRE(preset.values.at("orb/color").size() == 3);
    CHECK(preset.values.at("orb/color")[2] == 0.3f);
}

TEST_CASE("applyPreset restores values, clamps and ignores unknown paths", "[presets]") {
    Fixture f;
    f.scale.setBase(2.5f);
    f.color.setBase(glm::vec3(0.1f, 0.2f, 0.3f));
    Preset preset = capturePreset(f.params, "p");
    f.params.resetAllToDefault();
    CHECK(f.scale.base() == 1.0f);

    CHECK(applyPreset(f.params, preset) == 4);
    CHECK(f.scale.base() == 2.5f);
    CHECK_THAT(d(f.color.base().y), WithinAbs(0.2, 1e-6));

    preset.values["orb/scale"] = {99.0f};
    preset.values["orb/count"] = {-4.0f};
    preset.values["does/not/exist"] = {1.0f};
    preset.values["orb/color"] = {0.9f}; // short vectors apply the components they have
    CHECK(applyPreset(f.params, preset) == 4);
    CHECK(f.scale.base() == 4.0f);
    CHECK(f.count.base() == 0);
    CHECK_THAT(d(f.color.base().x), WithinAbs(0.9, 1e-6));
    CHECK_THAT(d(f.color.base().y), WithinAbs(0.2, 1e-6));
}

TEST_CASE("applyPresetBlend interpolates and picks one-sided paths by weight", "[presets]") {
    Fixture f;
    Preset a;
    a.name = "a";
    a.values["orb/scale"] = {1.0f};
    a.values["orb/color"] = {0.0f, 0.0f, 0.0f};
    a.values["orb/count"] = {2.0f}; // only in a
    Preset b;
    b.name = "b";
    b.values["orb/scale"] = {3.0f};
    b.values["orb/color"] = {1.0f, 1.0f, 1.0f};
    b.values["orb/flag"] = {1.0f}; // only in b
    b.values["ghost/x"] = {1.0f};  // unknown path

    CHECK(applyPresetBlend(f.params, a, b, 0.25f) == 3); // scale, color, count
    CHECK_THAT(d(f.scale.base()), WithinAbs(1.5, 1e-6));
    CHECK_THAT(d(f.color.base().x), WithinAbs(0.25, 1e-6));
    CHECK(f.count.base() == 2);
    CHECK_FALSE(f.flag.base());

    CHECK(applyPresetBlend(f.params, a, b, 0.75f) == 3); // scale, color, flag
    CHECK_THAT(d(f.scale.base()), WithinAbs(2.5, 1e-6));
    CHECK_THAT(d(f.color.base().z), WithinAbs(0.75, 1e-6));
    CHECK(f.count.base() == 2); // untouched
    CHECK(f.flag.base());

    applyPresetBlend(f.params, a, b, 0.0f);
    CHECK(f.scale.base() == 1.0f);
    applyPresetBlend(f.params, a, b, 1.0f);
    CHECK(f.scale.base() == 3.0f);

    // Out-of-range interpolants are clamped, and results clamp to the hard range.
    b.values["orb/scale"] = {10.0f};
    applyPresetBlend(f.params, a, b, 2.0f);
    CHECK(f.scale.base() == 4.0f);
    applyPresetBlend(f.params, a, b, 0.5f);
    CHECK(f.scale.base() == 4.0f); // 5.5 clamped
}

TEST_CASE("Preset JSON round-trip and validation", "[presets][json]") {
    Fixture f;
    f.scale.setBase(2.0f);
    f.color.setBase(glm::vec3(0.1f, 0.2f, 0.3f));
    const Preset preset = capturePreset(f.params, "warm");
    const json j = presetToJson(preset);
    CHECK(j["name"] == "warm");
    REQUIRE(j["values"].is_object());
    CHECK(j["values"]["orb/scale"] == json::array({2.0}));
    CHECK(j["values"]["orb/color"].size() == 3);

    auto back = presetFromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->name == "warm");
    CHECK(back->values == preset.values);

    // Scalars and booleans are accepted as single components.
    auto lenient = presetFromJson(json{{"name", "n"}, {"values", {{"a", 1.5}, {"b", true}}}});
    REQUIRE(lenient.has_value());
    CHECK(lenient->values.at("a") == std::vector<float>{1.5f});
    CHECK(lenient->values.at("b") == std::vector<float>{1.0f});
    auto empty = presetFromJson(json{{"name", "n"}});
    REQUIRE(empty.has_value());
    CHECK(empty->values.empty());

    CHECK_FALSE(presetFromJson(json(1)).has_value());
    CHECK_FALSE(presetFromJson(json{{"values", json::object()}}).has_value());
    CHECK_FALSE(presetFromJson(json{{"name", 4}}).has_value());
    CHECK_FALSE(presetFromJson(json{{"name", "n"}, {"values", 3}}).has_value());
    CHECK_FALSE(presetFromJson(json{{"name", "n"}, {"values", {{"a", "x"}}}}).has_value());
    auto badArray = presetFromJson(json{{"name", "n"}, {"values", {{"a", json::array({1.0, "x"})}}}});
    REQUIRE_FALSE(badArray.has_value());
    CHECK_THAT(badArray.error().message, ContainsSubstring("'a'"));
}

TEST_CASE("PresetBank add, replace, remove, find", "[presets][bank]") {
    PresetBank bank;
    CHECK(bank.find("a") == nullptr);
    Preset a;
    a.name = "a";
    a.values["x"] = {1.0f};
    Preset b;
    b.name = "b";
    bank.add(a);
    bank.add(b);
    CHECK(bank.presets().size() == 2);
    REQUIRE(bank.find("a") != nullptr);
    CHECK(bank.find("a")->values.at("x")[0] == 1.0f);

    a.values["x"] = {2.0f};
    Preset& replaced = bank.add(a);
    CHECK(bank.presets().size() == 2);
    CHECK(&bank.presets()[0] == &replaced); // keeps its slot
    CHECK(bank.find("a")->values.at("x")[0] == 2.0f);

    const PresetBank& constBank = bank;
    CHECK(constBank.find("b") != nullptr);
    CHECK(bank.remove("a"));
    CHECK_FALSE(bank.remove("a"));
    CHECK(bank.presets().size() == 1);
    bank.clear();
    CHECK(bank.presets().empty());
}

TEST_CASE("PresetBank JSON round-trip leaves the bank unchanged on malformed input",
          "[presets][bank][json]") {
    Fixture f;
    PresetBank bank;
    f.scale.setBase(1.5f);
    bank.add(capturePreset(f.params, "one"));
    f.scale.setBase(3.5f);
    bank.add(capturePreset(f.params, "two"));
    const json j = bank.toJson();
    REQUIRE(j.is_array());
    CHECK(j.size() == 2);
    CHECK(j[1]["name"] == "two");

    PresetBank back;
    REQUIRE(back.fromJson(j).has_value());
    CHECK(back.toJson() == j);
    REQUIRE(back.find("two") != nullptr);
    CHECK(back.find("two")->values.at("orb/scale")[0] == 3.5f);

    // Duplicate names in the document: the later one wins.
    REQUIRE(back.fromJson(json::array({json{{"name", "d"}, {"values", {{"a", 1.0}}}},
                                       json{{"name", "d"}, {"values", {{"a", 2.0}}}}}))
                .has_value());
    CHECK(back.presets().size() == 1);
    CHECK(back.find("d")->values.at("a")[0] == 2.0f);

    const json before = back.toJson();
    CHECK_FALSE(back.fromJson(json(5)).has_value());
    CHECK_FALSE(back.fromJson(json::array({json(5)})).has_value());
    auto bad = back.fromJson(json::array({json{{"name", "ok"}}, json{{"name", "x"}, {"values", 1}}}));
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("presets[1]"));
    CHECK(back.toJson() == before);
}
