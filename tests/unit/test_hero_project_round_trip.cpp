// Starring an object did not survive a save, so no hero ever reached a deliverable (ADR-276).
//
// The shape is ADR-207's and ADR-230's, for a third family. A project whose scene came from a file
// saves that scene **by reference** -- `assets.scene.path` plus a hash of the bytes already on disk
// -- and nothing but a "Save Scene As..." dialog writes a scene file. So an edit made through the
// world editor that lands in the `Composition` rather than in a parameter lives in the window the
// person is looking at and in no document any render reads. World effects were the first
// (ADR-207), atmospheric effects the second (ADR-230), and heroes are the third: `ui::setNodesHero`
// calls `Composition::setHeroes` and stops there.
//
// Measured before the fix: **1 hero in the session, 0 after a save and a reload.** It matters more
// than the other two because an offline render builds its own `Engine` and loads the *project*, so
// a star was invisible to every frame anybody exported -- and because `cameraAimFollow`, which the
// project already saved, names its heroes by name: `Composition::applyDirectedAim` skips an entry
// whose hero is not in `heroes()`, so a saved cut was aimed at nothing.
//
// The arms (ADR-182):
//
//   A  star, save the project, reload          -> the hero is there, with its own numbers
//   B  unstar, save, reload                    -> it is gone, and stays gone (the deletion is an
//                                                 edit like any other; a save that only wrote
//                                                 additions is the same defect pointing the other
//                                                 way)
//   C  never star, save, reload                -> nothing is written at all: a project that was
//                                                 never edited keeps the file it had
//   D  the scene file is byte-for-byte unchanged by any of it -- ADR-271's rule, and the reason
//      this is in the project rather than re-authored into the scene

#include "app/engine.hpp"
#include "support/gltf_fixture.hpp"
#include "ui/world_edit.hpp"
#include "world/hero.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// A one-node composition saved to a real scene file, so the project that follows saves the scene
// **by reference** -- which is the entire mechanism under test. An inlined composition carries its
// own `heroes` block through `Composition::toJson` and was never broken.
struct Fixture {
    fs::path dir;
    fs::path glb;
    fs::path scenePath;
    fs::path projectPath;
    app::Engine engine{app::EngineMode::Offline};

    Fixture() {
        dir = fs::temp_directory_path() /
              ("avgen_hero_round_trip_" + std::to_string(static_cast<long long>(::getpid())));
        fs::remove_all(dir);
        fs::create_directories(dir);
        const auto tmp = testsupport::writeTriangleGlb("hero_round_trip");
        glb = dir / "tri.glb";
        fs::copy_file(tmp, glb, fs::copy_options::overwrite_existing);
        fs::remove(tmp);
        scenePath = dir / "world.scene.json";
        projectPath = dir / "world.json";

        engine.newComposition();
        scene::CompositionNode node;
        node.name = "monument";
        node.kind = scene::NodeKind::Gltf;
        node.asset = glb.generic_string();
        node.transform.position = glm::vec3(4.0f, 0.0f, -7.0f);
        REQUIRE(engine.addNode(std::move(node)).has_value());
        REQUIRE(engine.saveComposition(scenePath).has_value());
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    void star(bool on) {
        const std::vector<std::string> names{"monument"};
        const ui::EditCommand command = ui::setNodesHero(engine, names, on);
        CHECK_FALSE(command.heroes.empty());
    }

    [[nodiscard]] std::size_t heroes() const {
        const auto* comp = const_cast<app::Engine&>(engine).composition();
        return comp == nullptr ? 0 : comp->heroes().size();
    }
};

std::size_t heroesAfterReload(const fs::path& project) {
    app::Engine fresh(app::EngineMode::Offline);
    auto loaded = fresh.loadProject(project);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    REQUIRE(fresh.composition() != nullptr);
    return fresh.composition()->heroes().size();
}

} // namespace

TEST_CASE("a starred hero survives the project save an offline render reloads", "[heroes][project]") {
    Fixture f;
    const std::string sceneBefore = bytesOf(f.scenePath);

    // ---- arm C, first, because it is the control and it has to hold before and after ----
    // A project that was never edited writes no `heroes` key at all. Without this the arm below
    // would pass on a save that wrote an empty array into every project in the repository.
    REQUIRE(f.engine.saveProject(f.projectPath).has_value());
    {
        std::ifstream in(f.projectPath);
        const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
        REQUIRE_FALSE(doc.is_discarded());
        CHECK_FALSE(doc.contains("heroes"));
    }
    CHECK(heroesAfterReload(f.projectPath) == 0);

    // ---- arm A ----
    f.star(true);
    REQUIRE(f.heroes() == 1);
    const world::HeroPoint declared = f.engine.composition()->heroes().front();
    REQUIRE(f.engine.saveProject(f.projectPath).has_value());

    {
        std::ifstream in(f.projectPath);
        const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
        REQUIRE_FALSE(doc.is_discarded());
        REQUIRE(doc.contains("heroes"));
        REQUIRE(doc["heroes"].is_array());
        CHECK(doc["heroes"].size() == 1);
    }

    // The measurement that names the defect: one in the session, one reloaded. It was one and zero.
    app::Engine fresh(app::EngineMode::Offline);
    REQUIRE(fresh.loadProject(f.projectPath).has_value());
    REQUIRE(fresh.composition() != nullptr);
    const auto& back = fresh.composition()->heroes();
    INFO("session " << f.heroes() << " hero(es), reloaded " << back.size());
    REQUIRE(back.size() == 1);

    // Not merely present: the same hero. A round trip that kept the name and lost the numbers is
    // the shape ADR-230 found -- 94 parameter values kept and the aurora lost.
    CHECK(back.front().name == declared.name);
    CHECK_THAT(back.front().position.x, Catch::Matchers::WithinAbs(declared.position.x, 1e-4));
    CHECK_THAT(back.front().position.y, Catch::Matchers::WithinAbs(declared.position.y, 1e-4));
    CHECK_THAT(back.front().position.z, Catch::Matchers::WithinAbs(declared.position.z, 1e-4));
    CHECK_THAT(back.front().radius, Catch::Matchers::WithinAbs(declared.radius, 1e-4));
    CHECK_THAT(back.front().height, Catch::Matchers::WithinAbs(declared.height, 1e-4));
    CHECK_THAT(back.front().importance, Catch::Matchers::WithinAbs(declared.importance, 1e-4));
    CHECK_THAT(back.front().preferredCameraDistance,
               Catch::Matchers::WithinAbs(declared.preferredCameraDistance, 1e-4));

    // ---- arm D: ADR-271's rule ----
    // The scene file is not touched. This is the whole argument for putting the star in the project:
    // a scene is shared between projects, and an editor that wrote scenes would change a project the
    // user did not open. Byte-for-byte, because a re-serialisation that happened to be equivalent
    // would still re-fingerprint the file.
    CHECK(bytesOf(f.scenePath) == sceneBefore);
}

TEST_CASE("unstarring survives the same save", "[heroes][project]") {
    // Arm B. A save that wrote only additions would leave an unstarred object starred for ever in
    // every render, and would look exactly like a working fix from the arm above.
    Fixture f;
    f.star(true);
    REQUIRE(f.heroes() == 1);
    REQUIRE(f.engine.saveProject(f.projectPath).has_value());
    REQUIRE(heroesAfterReload(f.projectPath) == 1);

    f.star(false);
    CHECK(f.heroes() == 0);
    REQUIRE(f.engine.saveProject(f.projectPath).has_value());

    // Written as an empty array rather than omitted: an omitted key means "the scene's own list
    // stands", and the scene's own list here is empty too -- so this case is indistinguishable
    // either way, and the *rule* is what is asserted. The reload is the thing that matters.
    CHECK(heroesAfterReload(f.projectPath) == 0);
}

TEST_CASE("a hero the project cannot read is refused by name, not dropped", "[heroes][project]") {
    // The failure mode ADR-067 and ADR-070 were both about: a hero that quietly failed to load
    // looks exactly like a hero nobody declared. `HeroPoint::fromJson` validates, and an
    // activation radius inside the stand-off is one of the things it refuses.
    Fixture f;
    f.star(true);
    REQUIRE(f.engine.saveProject(f.projectPath).has_value());

    nlohmann::json doc;
    {
        std::ifstream in(f.projectPath);
        doc = nlohmann::json::parse(in, nullptr, false);
        REQUIRE_FALSE(doc.is_discarded());
    }
    REQUIRE(doc.contains("heroes"));
    doc["heroes"][0]["activationRadius"] = 1.0;
    doc["heroes"][0]["preferredCameraDistance"] = 40.0;
    {
        std::ofstream out(f.projectPath);
        out << doc.dump(1);
    }

    app::Engine fresh(app::EngineMode::Offline);
    auto loaded = fresh.loadProject(f.projectPath);
    // The project still loads -- a bad hero is not a reason to refuse somebody's whole piece -- and
    // it says so. The control is the arm above, where the same file loads with no warning at all.
    REQUIRE(loaded.has_value());
    bool complained = false;
    for (const std::string& w : fresh.projectWarnings()) {
        if (w.find("heroes") != std::string::npos) {
            complained = true;
            INFO("warning: " << w);
        }
    }
    CHECK(complained);
    CHECK(fresh.composition()->heroes().empty());
}
