#include "params/parameter.hpp"
#include "params/parameter_set.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using namespace avgen::params;

TEST_CASE("Parameter starts at its default and clamps base and final", "[params]") {
    Parameter<float> p(
        ParamDesc<float>{.path = "orb/scale", .defaultValue = 1.0f, .hardMin = 0.05f, .hardMax = 8.0f});
    CHECK(p.value() == 1.0f);
    CHECK(p.base() == 1.0f);
    CHECK(p.kind() == ParamKind::Float);
    CHECK(p.componentCount() == 1);
    CHECK(p.defaultComponent(0) == 1.0f);

    p.setBase(20.0f);
    CHECK(p.base() == 8.0f);
    p.setBase(-3.0f);
    CHECK(p.base() == 0.05f);
    p.setBaseComponent(0, 2.5f);
    CHECK(p.base() == 2.5f);
    CHECK(p.value() == 1.0f); // final untouched until reset or modulation

    p.setFinalComponent(0, 100.0f);
    CHECK(p.value() == 8.0f);
    p.setFinalComponent(0, -1.0f);
    CHECK(p.value() == 0.05f);
    p.resetFinal();
    CHECK(p.value() == 2.5f);
    p.resetToDefault();
    CHECK(p.base() == 1.0f);
    CHECK(p.value() == 1.0f);
}

TEST_CASE("Parameter default outside the hard range is clamped", "[params]") {
    Parameter<float> p(ParamDesc<float>{.path = "x", .defaultValue = 5.0f, .hardMin = 0.0f, .hardMax = 1.0f});
    CHECK(p.base() == 1.0f);
    CHECK(p.value() == 1.0f);
}

TEST_CASE("Parameter soft range defaults to the hard range", "[params]") {
    Parameter<float> a(
        ParamDesc<float>{.path = "a", .defaultValue = 0.0f, .hardMin = -2.0f, .hardMax = 2.0f});
    CHECK(a.softMin(0) == -2.0f);
    CHECK(a.softMax(0) == 2.0f);
    Parameter<float> b(ParamDesc<float>{.path = "b",
                                        .defaultValue = 0.0f,
                                        .hardMin = -2.0f,
                                        .hardMax = 2.0f,
                                        .softMin = -1.0f,
                                        .softMax = 1.0f});
    CHECK(b.softMin(0) == -1.0f);
    CHECK(b.softMax(0) == 1.0f);
    CHECK(b.hardMin(0) == -2.0f);
    CHECK(b.hardMax(0) == 2.0f);
}

TEST_CASE("Parameter derives label and group from the path", "[params]") {
    Parameter<float> p(
        ParamDesc<float>{.path = "scene/orb/scale", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 1.0f});
    CHECK(p.label() == "scale");
    CHECK(p.group() == "scene");
    Parameter<float> flat(
        ParamDesc<float>{.path = "gain", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 1.0f});
    CHECK(flat.label() == "gain");
    CHECK(flat.group().empty());
    Parameter<float> custom(ParamDesc<float>{.path = "a/b",
                                             .defaultValue = 0.0f,
                                             .hardMin = 0.0f,
                                             .hardMax = 1.0f,
                                             .label = "Nice",
                                             .group = "Grp"});
    CHECK(custom.label() == "Nice");
    CHECK(custom.group() == "Grp");
}

TEST_CASE("Vec3 parameter exposes and clamps components", "[params]") {
    Parameter<glm::vec3> c(ParamDesc<glm::vec3>{.path = "orb/baseColor",
                                                .defaultValue = glm::vec3(0.75f, 0.2f, 0.9f),
                                                .hardMin = glm::vec3(0.0f),
                                                .hardMax = glm::vec3(1.0f),
                                                .isColor = true});
    CHECK(c.kind() == ParamKind::Color);
    CHECK(c.componentCount() == 3);
    CHECK(c.baseComponent(0) == 0.75f);
    CHECK(c.baseComponent(1) == 0.2f);
    CHECK(c.baseComponent(2) == 0.9f);
    c.setBaseComponent(1, 4.0f);
    CHECK(c.base().y == 1.0f);
    c.setFinalComponent(2, -1.0f);
    CHECK(c.value().z == 0.0f);
    c.setBase(glm::vec3(0.1f, 0.2f, 0.3f));
    CHECK(c.base() == glm::vec3(0.1f, 0.2f, 0.3f));
    c.resetFinal();
    CHECK(c.value() == glm::vec3(0.1f, 0.2f, 0.3f));

    Parameter<glm::vec3> plain(ParamDesc<glm::vec3>{.path = "v",
                                                    .defaultValue = glm::vec3(0.0f),
                                                    .hardMin = glm::vec3(-1.0f),
                                                    .hardMax = glm::vec3(1.0f)});
    CHECK(plain.kind() == ParamKind::Vec3);
}

TEST_CASE("Int parameter rounds component writes", "[params]") {
    Parameter<int> p(ParamDesc<int>{.path = "count", .defaultValue = 2, .hardMin = 0, .hardMax = 10});
    CHECK(p.kind() == ParamKind::Int);
    p.setBaseComponent(0, 3.4f);
    CHECK(p.base() == 3);
    p.setBaseComponent(0, 3.6f);
    CHECK(p.base() == 4);
    p.setBaseComponent(0, 42.0f);
    CHECK(p.base() == 10);
    p.setFinalComponent(0, -7.0f);
    CHECK(p.value() == 0);
    CHECK(p.baseComponent(0) == 10.0f);
}

TEST_CASE("Bool parameter thresholds at 0.5", "[params]") {
    Parameter<bool> p(
        ParamDesc<bool>{.path = "on", .defaultValue = false, .hardMin = false, .hardMax = true});
    CHECK(p.kind() == ParamKind::Bool);
    CHECK_FALSE(p.value());
    p.setBaseComponent(0, 0.49f);
    CHECK_FALSE(p.base());
    p.setBaseComponent(0, 0.5f);
    CHECK(p.base());
    CHECK(p.baseComponent(0) == 1.0f);
    p.setFinalComponent(0, 1.0f);
    CHECK(p.value());
}

TEST_CASE("ParameterSet registers, finds and iterates in insertion order", "[params][set]") {
    ParameterSet set;
    CHECK(set.size() == 0);
    CHECK(set.find("orb/scale") == nullptr);

    auto& scale = set.add(
        ParamDesc<float>{.path = "orb/scale", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 8.0f});
    auto& color = set.add(ParamDesc<glm::vec3>{.path = "orb/color",
                                               .defaultValue = glm::vec3(1.0f),
                                               .hardMin = glm::vec3(0.0f),
                                               .hardMax = glm::vec3(1.0f)});
    auto& count = set.add(ParamDesc<int>{.path = "orb/count", .defaultValue = 3, .hardMin = 0, .hardMax = 9});
    CHECK(set.size() == 3);

    REQUIRE(set.find("orb/scale") != nullptr);
    CHECK(set.find("orb/scale") == &scale);
    CHECK(set.findAs<float>("orb/scale") == &scale);
    CHECK(set.findAs<glm::vec3>("orb/scale") == nullptr);
    CHECK(set.findAs<glm::vec3>("orb/color") == &color);
    CHECK(set.findAs<int>("orb/count") == &count);
    CHECK(set.find("missing") == nullptr);

    const ParameterSet& constSet = set;
    CHECK(constSet.find("orb/color") == &color);

    REQUIRE(set.ordered().size() == 3);
    CHECK(set.ordered()[0]->path() == "orb/scale");
    CHECK(set.ordered()[1]->path() == "orb/color");
    CHECK(set.ordered()[2]->path() == "orb/count");
}

TEST_CASE("ParameterSet duplicate paths", "[params][set]") {
    ParameterSet set;
    auto& first =
        set.add(ParamDesc<float>{.path = "a/x", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 2.0f});

    SECTION("same type returns the existing parameter") {
        auto& again =
            set.add(ParamDesc<float>{.path = "a/x", .defaultValue = 0.5f, .hardMin = 0.0f, .hardMax = 1.0f});
        CHECK(&again == &first);
        CHECK(again.base() == 1.0f); // original description kept
        CHECK(set.size() == 1);
    }
    SECTION("different type is a programming error and registers nothing") {
        CHECK_THROWS_AS(set.add(ParamDesc<int>{.path = "a/x", .defaultValue = 1, .hardMin = 0, .hardMax = 2}),
                        std::logic_error);
        CHECK(set.size() == 1);
        CHECK(set.findAs<float>("a/x") == &first);
        CHECK(set.findAs<int>("a/x") == nullptr);
        CHECK(set.ordered().size() == 1);
    }
}

TEST_CASE("ParameterSet resetFinals and resetAllToDefault", "[params][set]") {
    ParameterSet set;
    auto& a = set.add(ParamDesc<float>{.path = "a", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    auto& b = set.add(ParamDesc<float>{.path = "b", .defaultValue = 2.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    a.setBase(3.0f);
    b.setBase(4.0f);
    a.setFinalComponent(0, 9.0f);
    b.setFinalComponent(0, 9.0f);
    set.resetFinals();
    CHECK(a.value() == 3.0f);
    CHECK(b.value() == 4.0f);
    set.resetAllToDefault();
    CHECK(a.base() == 1.0f);
    CHECK(b.base() == 2.0f);
    CHECK(a.value() == 1.0f);
    CHECK(b.value() == 2.0f);
}
