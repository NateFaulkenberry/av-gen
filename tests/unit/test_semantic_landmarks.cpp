// ADR-833 (Phase D §25/§69): props and effects say what they are.
//
// The awareness layer already filtered and weighted percepts by semantic tags, but the words came
// only from entities' `tags` and from the five interest kinds. Glowmere's props -- forty mushroom
// caps, the saucer, its beam -- carried nothing, so "go and look at a mushroom" and "the UFO is a
// world effect" were not things a character could be told. Now a node contributes its authored
// `tags` plus what it demonstrably is (a generated procedural's generator), and the saucer and the
// beam are tagged in the film.

#include "app/engine.hpp"
#include "entity/entity.hpp"
#include "entity/mind.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using namespace avgen;

namespace {
std::filesystem::path multicam() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
}
bool present() {
    return std::filesystem::exists(std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}
} // namespace

TEST_CASE("a node's semantic words are what the author wrote plus what it is", "[entity][phaseD][semantics][adr833]") {
    scene::CompositionNode cap;
    cap.name = "cap";
    cap.kind = scene::NodeKind::Procedural;
    cap.procedural.source.generated.generator = "mushroom";
    CHECK(scene::semanticTagsOf(cap) == std::vector<std::string>{"mushroom"});
    cap.tags = {"glowing", "mushroom"}; // authored first, and not repeated
    CHECK(scene::semanticTagsOf(cap) == std::vector<std::string>{"glowing", "mushroom"});
    scene::CompositionNode rock;
    rock.name = "rock";
    rock.kind = scene::NodeKind::Gltf;
    CHECK(scene::semanticTagsOf(rock).empty()); // nothing is guessed from a name
    rock.tags = {"rock"};
    CHECK(scene::semanticTagsOf(rock) == std::vector<std::string>{"rock"});
}

TEST_CASE("Glowmere's mushrooms, saucer and beam carry their semantic tags", "[entity][phaseD][semantics][glowmere][adr833]") {
    if (!present()) {
        SKIP("Glowmere assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(multicam()).has_value());
    const entity::EntityWorld& world = engine.composition()->entityWorld();
    const entity::SemanticTags& tags = world.semanticTags();
    const std::uint64_t mushroom = tags.bit("mushroom");
    REQUIRE(mushroom != 0);
    int caps = 0;
    for (const entity::InterestPoint& p : world.interestPoints()) {
        if (p.name.size() > 4 && p.name.ends_with("-cap")) {
            INFO(p.name << " carries " << tags.describe(p.tags));
            CHECK((p.tags & mushroom) != 0);
            ++caps;
        }
    }
    CHECK(caps >= 10); // one per species at least: the landmarks and the heroes both name them
    const entity::Entity* saucer = world.find("visitor");
    const entity::Entity* beam = world.find("visitor-beam");
    REQUIRE(saucer != nullptr);
    REQUIRE(beam != nullptr);
    CHECK((saucer->tagMask() & tags.bit("ufo")) != 0);
    CHECK((saucer->tagMask() & tags.bit("vehicle")) != 0);
    CHECK((beam->tagMask() & tags.bit("world_effect")) != 0);
}

TEST_CASE("a node's tags survive a save", "[entity][phaseD][semantics][adr833]") {
    if (!present()) {
        SKIP("Glowmere assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(multicam()).has_value());
    scene::CompositionNode* node = engine.composition()->findNode("rook");
    REQUIRE(node != nullptr);
    node->tags = {"alien", "hero"};
    const nlohmann::json saved = engine.composition()->toJson();
    bool found = false;
    for (const auto& n : saved.at("nodes")) {
        if (n.at("name") == "rook") {
            REQUIRE(n.contains("tags"));
            CHECK(n.at("tags") == nlohmann::json{"alien", "hero"});
            found = true;
        } else if (n.at("name") == "tide") {
            CHECK_FALSE(n.contains("tags")); // additive: an untagged node grows no key
        }
    }
    CHECK(found);
}
