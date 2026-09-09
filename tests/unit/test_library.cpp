// The shipped preset library (brief §75): every generator, field and deformer body in
// examples/library/library.json must parse into the real structs, so the library cannot drift
// from the code.

#include "scene/procedural.hpp"
#include "spatial/field.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace avgen;
namespace fs = std::filesystem;

TEST_CASE("The preset library parses into scene data", "[library]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const fs::path file = fs::path(AVGEN_SOURCE_DIR) / "examples" / "library" / "library.json";
    if (!fs::exists(file)) {
        SKIP("library not present");
    }
    std::ifstream in(file);
    REQUIRE(in.good());
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    REQUIRE_FALSE(j.is_discarded());
    CHECK(j.at("format") == "avgen-library");

    REQUIRE(j.contains("generators"));
    CHECK(j.at("generators").size() >= 8);
    for (const auto& [name, entry] : j.at("generators").items()) {
        INFO("generator " << name);
        REQUIRE(entry.contains("procedural"));
        auto pg = scene::ProceduralGeometry::fromJson(entry.at("procedural"));
        REQUIRE(pg.has_value());
        CHECK(pg->validate().has_value());
        pg->name = name;
        CHECK(pg->rebuild());
        CHECK(pg->instances.size() > 0);
        CHECK_FALSE(entry.at("description").get<std::string>().empty());
    }

    REQUIRE(j.contains("fields"));
    CHECK(j.at("fields").size() >= 7);
    for (const auto& [name, entry] : j.at("fields").items()) {
        INFO("field " << name);
        REQUIRE(entry.contains("field"));
        auto f = spatial::FieldSpec::fromJson(entry.at("field"));
        REQUIRE(f.has_value());
        CHECK(f->validate().has_value());
        // Every library field must actually do something at a point inside its falloff.
        const float scalar = spatial::sampleScalar(*f, glm::vec3(3.0f, 2.0f, 1.0f), 0.25);
        const glm::vec3 vector = spatial::sampleVector(*f, glm::vec3(3.0f, 2.0f, 1.0f), 0.25);
        CHECK((std::abs(scalar) > 1e-6f || glm::length(vector) > 1e-6f));
    }

    REQUIRE(j.contains("deformers"));
    CHECK(j.at("deformers").size() >= 7);
    for (const auto& [name, entry] : j.at("deformers").items()) {
        INFO("deformer " << name);
        REQUIRE(entry.contains("deformer"));
        // Deformers are parsed through an object that carries them.
        nlohmann::json body = {{"source", {{"kind", "box"}}},
                               {"distribution", {{"kind", "single"}}},
                               {"deformers", nlohmann::json::array({entry.at("deformer")})}};
        auto pg = scene::ProceduralGeometry::fromJson(body);
        REQUIRE(pg.has_value());
        REQUIRE(pg->deformers.size() == 1);
        CHECK(pg->validate().has_value());
    }

    REQUIRE(j.contains("worlds"));
    for (const auto& [name, path] : j.at("worlds").items()) {
        INFO("world " << name);
        CHECK(fs::exists(fs::path(AVGEN_SOURCE_DIR) / path.get<std::string>()));
    }
#endif
}
