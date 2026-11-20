// ADR-2116: a crowd of skinned characters. A node's opening `phase`, a pose shared by rigs of one
// asset playing one clip at one clip second, and the palette ceiling a crowd used to hit at 64.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>

using namespace avgen;

namespace {

std::filesystem::path charactersDir() { return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "characters"; }

nlohmann::json alienNode(const std::string& name, float x, const std::string& state, float speed, float phase) {
    nlohmann::json anim = {{"state", state}, {"speed", speed}, {"cullDistance", 0.0}};
    if (phase != 0.0f) {
        anim["phase"] = phase;
    }
    return {{"name", name},
            {"kind", "gltf"},
            {"asset", "../../assets/imported/alien.gltf"},
            {"position", {x, 0.0, 0.0}},
            {"scale", {0.01, 0.01, 0.01}},
            {"animation", anim}};
}

struct Crowd {
    params::ParameterSet parameters;
    params::Modulator modulator;
    std::unique_ptr<scene::Composition> composition;
};

std::unique_ptr<Crowd> crowd(const nlohmann::json& nodes) {
    const nlohmann::json doc = {{"format", "avgen-scene"},
                                {"version", 1},
                                {"name", "crowd"},
                                {"camera", {{"mode", 1}, {"position", {0.0, 1.0, 6.0}}, {"target", {0.0, 0.6, 0.0}}}},
                                {"nodes", nodes}};
    assets::AssetRegistry registry{charactersDir()};
    auto loaded = scene::Composition::fromJson(doc, registry);
    REQUIRE(loaded.has_value());
    auto out = std::make_unique<Crowd>();
    out->composition = std::move(*loaded);
    out->composition->attach(out->parameters, out->modulator);
    out->composition->setViewport(640, 360);
    return out;
}

const scene::SkinnedRig& rigOf(const scene::Composition& c, const std::string& node) {
    const scene::CompositionNode* n = c.findNode(node);
    REQUIRE(n != nullptr);
    REQUIRE_FALSE(n->rigs.empty());
    return c.scene().rigs[n->rigs.front()];
}

} // namespace

TEST_CASE("a crowd in step shares one pose, and a phase puts a body out of step", "[scene][animation][adr2116]") {
    if (!std::filesystem::is_regular_file(charactersDir() / "../../assets/imported/alien.gltf")) {
        SKIP("the imported alien is not present");
    }
    auto c = crowd(nlohmann::json::array({alienNode("a", -1.0f, "Walk", 1.0f, 0.0f),
                                          alienNode("b", 0.0f, "Walk", 1.0f, 0.0f),
                                          alienNode("c", 1.0f, "Walk", 1.0f, 0.25f),
                                          alienNode("d", 2.0f, "Walk", 1.0f, 0.0f)}));
    FrameTime time;
    time.renderTime = 0.5;
    c->composition->update(time);
    const scene::RigStats& stats = c->composition->rigStats();
    CHECK(stats.posed == 4);
    CHECK(stats.shared == 2); // b and d adopt a's pose; c is a quarter second ahead
    CHECK(rigOf(*c->composition, "a").palette == rigOf(*c->composition, "b").palette);
    CHECK(rigOf(*c->composition, "c").palette != rigOf(*c->composition, "a").palette);
    CHECK(rigOf(*c->composition, "b").sharedFrom >= 0);
    CHECK(rigOf(*c->composition, "c").sharedFrom == -1);

    // The phase is clip seconds at second 0: c at 0.5 s is where a is at 0.75 s.
    auto ref = crowd(nlohmann::json::array({alienNode("a", -1.0f, "Walk", 1.0f, 0.0f)}));
    FrameTime later;
    later.renderTime = 0.75;
    ref->composition->update(later);
    CHECK(rigOf(*ref->composition, "a").palette == rigOf(*c->composition, "c").palette);
}

TEST_CASE("a shared pose is the pose the rig would have computed, by seek and by play", "[scene][animation][adr2116]") {
    if (!std::filesystem::is_regular_file(charactersDir() / "../../assets/imported/alien.gltf")) {
        SKIP("the imported alien is not present");
    }
    const auto nodes = nlohmann::json::array({alienNode("a", -1.0f, "Walk", 1.1f, 0.3f),
                                              alienNode("b", 1.0f, "Walk", 1.1f, 0.3f)});
    auto played = crowd(nodes);
    FrameTime time;
    for (int f = 0; f <= 45; ++f) { // play 1.5 s at 30 fps
        time.renderTime = f / 30.0;
        played->composition->update(time);
    }
    CHECK(played->composition->rigStats().shared == 1);
    // A lone rig, sought straight to the same second, computes the same palette itself.
    auto alone = crowd(nlohmann::json::array({alienNode("b", 1.0f, "Walk", 1.1f, 0.3f)}));
    alone->composition->update(time);
    CHECK(alone->composition->rigStats().shared == 0);
    CHECK(rigOf(*alone->composition, "b").palette == rigOf(*played->composition, "b").palette);
}

TEST_CASE("the opening phase survives a save and reload", "[scene][composition][adr2116]") {
    if (!std::filesystem::is_regular_file(charactersDir() / "../../assets/imported/alien.gltf")) {
        SKIP("the imported alien is not present");
    }
    auto c = crowd(nlohmann::json::array({alienNode("a", 0.0f, "Walk", 1.0f, 0.4f)}));
    const nlohmann::json written = c->composition->toJson();
    assets::AssetRegistry registry{charactersDir()};
    auto again = scene::Composition::fromJson(written, registry);
    REQUIRE(again.has_value());
    CHECK((*again)->findNode("a")->animation.phase == 0.4f);
}

namespace {

nlohmann::json listener(const std::string& name, float x) {
    nlohmann::json n = alienNode(name, x, "Walk", 1.0f, 0.0f);
    n["animation"]["crowd"] = "town";
    n["animation"]["layers"] = nlohmann::json::array(
        {{{"name", "crowd-look"}, {"kind", "aim"}, {"drive", "manual"},
          {"joints", {"mixamorig:Head", "mixamorig:HeadTop_End"}}, {"pivot", "mixamorig:Head"}, {"maxYaw", 60.0}},
         {{"name", "crowd-sway"}, {"kind", "secondary"}, {"drive", "manual"},
          {"joints", {"mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2"}}, {"degrees", 4.0}, {"period", 1.32}}});
    return n;
}

} // namespace

TEST_CASE("a crowd turns its heads, sways and runs behind the timeline by its parameters", "[scene][animation][adr2117]") {
    if (!std::filesystem::is_regular_file(charactersDir() / "../../assets/imported/alien.gltf")) {
        SKIP("the imported alien is not present");
    }
    auto c = crowd(nlohmann::json::array({listener("a", -1.0f), listener("b", 1.0f)}));
    auto ref = crowd(nlohmann::json::array({alienNode("a", -1.0f, "Walk", 1.0f, 0.0f)}));
    FrameTime time;
    time.renderTime = 0.5;
    // Idle parameters: the layers do nothing, so the two share a pose and match a plain walker.
    c->composition->update(time);
    ref->composition->update(time);
    CHECK(c->composition->rigStats().shared == 1);
    CHECK(rigOf(*c->composition, "a").palette == rigOf(*ref->composition, "a").palette);

    // The crowd looks at a point well off to one side: the heads move, the poses are their own.
    auto* look = c->parameters.findAs<float>("crowds/town/look");
    auto* target = c->parameters.findAs<glm::vec3>("crowds/town/lookTarget");
    auto* lag = c->parameters.findAs<float>("crowds/town/lag");
    REQUIRE(look != nullptr);
    REQUIRE(target != nullptr);
    REQUIRE(lag != nullptr);
    look->setBase(1.0f);
    target->setBase({-20.0f, 1.0f, 3.0f});
    c->parameters.resetFinals();
    time.renderTime = 0.55;
    c->composition->update(time);
    CHECK(c->composition->rigStats().shared == 0);
    const auto layered = rigOf(*c->composition, "a").palette;
    look->setBase(0.0f);
    c->parameters.resetFinals();
    time.renderTime = 0.6;
    c->composition->update(time);
    time.renderTime = 0.55;
    c->composition->update(time);
    CHECK(rigOf(*c->composition, "a").palette != layered);

    // A lag of 0.3 s: the clips are where an unlagged body was 0.3 s earlier.
    lag->setBase(0.3f);
    c->parameters.resetFinals();
    time.renderTime = 1.0;
    c->composition->update(time);
    FrameTime earlier;
    earlier.renderTime = 0.7;
    ref->composition->update(earlier);
    CHECK(rigOf(*c->composition, "a").palette == rigOf(*ref->composition, "a").palette);
}
