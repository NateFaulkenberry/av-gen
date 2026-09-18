// Where does an edit made through the particle panel end up, and is it still there tomorrow?
//
// Reported as: "I did change the beam but I did it through the UI - so if it got changed in the
// wrong spot then the UI is changing it the wrong spot so fix that."
//
// The owner set the tractor beam's radius with the emitter controls in `WorldEditPanel::
// drawParticleSettings`. What persisted was `particles/visitor-beam/extent: 0.0538` in the project
// -- a bare multiplier over a 7.8 that lives in the scene file. Twice an agent read it as ADR-264
// session residue and reverted it, and from inside the project file it is indistinguishable from
// one: it is not a radius, it is not in metres, and nothing in the document says what it multiplies.
//
// The boundary this file guards is the one that settles it, and it is three claims at once:
//
//   1. The edit **survives a save and a reload.** `Engine::saveProject` writes the scene by
//      reference -- `assets.scene.path` plus a hash of the bytes on disk -- and nothing but the
//      "Save Scene As..." dialog ever writes a scene file. An edit re-authored into
//      `CompositionNode::particles` would be discarded by the next Cmd-S, which is ADR-225's defect
//      with the sign flipped. The project's `parameters` block is where a live adjustment keeps.
//   2. The project does **not contradict the scene in a language the scene cannot answer.** The
//      value in the project is the number on the slider, in the slider's unit, so a person reading
//      `0.42` beside the scene's `7.8` can see one authored radius replacing another instead of a
//      ratio they have to reconstruct.
//   3. The scene file is **not written.** It is shared -- `glowmere-stylized.scene.json` is the
//      scene of two projects -- and an editor that re-authored it would change a project the user
//      did not open, and would need re-fingerprinting on every drag.
//
// Every case has a control arm that passes before and after the fix, because a probe that cannot
// see the value that IS there proves nothing about one that is not (ADR-182).
//
// The panel itself is ImGui and is not testable. What is driven here is what the panel calls, with
// nothing in between: `scene::particleExtentFromRadius` turns the metres under the mouse into an
// extent, `ui::EditHistory::beginDrag` / `ui::setBaseComponents` / `commitDrag` are the three calls
// the drag makes, and they are called in that order.

#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "scene/particles.hpp"
#include "ui/edit_history.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() /
                         ("avgen_particle_authoring_" + std::to_string(getpid()) + "_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// The shipped tractor beam, cut down to the fields this is about: a 7.8 m disc firing a near-column
// straight down. The numbers are `glowmere-valley-2-multicam.scene.json`'s so that the arithmetic in
// the comments below is the arithmetic of the report.
scene::CompositionNode beamNode() {
    scene::CompositionNode node;
    node.kind = scene::NodeKind::Particles;
    node.name = "visitor-beam";
    node.particles.name = "visitor-beam";
    node.particles.shape = scene::EmitterShape::Disc;
    node.particles.extent = glm::vec3(7.8f);
    node.particles.spread = 0.042f;
    node.particles.spawnRate = 4000.0f;
    node.particles.capacity = 4096;
    node.particles.lifetimeMin = 3.6f;
    node.particles.lifetimeMax = 5.0f;
    node.particles.speedMin = 8.5f;
    node.particles.speedMax = 12.5f;
    node.particles.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    return node;
}

// A box emitter, because `extent` is one field with three meanings and the panel offers one number
// for it. 30 x 18 x 4 is `hyperspace.scene.json`'s streak volume.
scene::CompositionNode boxNode() {
    scene::CompositionNode node;
    node.kind = scene::NodeKind::Particles;
    node.name = "streaks";
    node.particles.name = "streaks";
    node.particles.shape = scene::EmitterShape::Box;
    node.particles.extent = glm::vec3(30.0f, 18.0f, 4.0f);
    node.particles.spawnRate = 400.0f;
    node.particles.capacity = 1024;
    return node;
}

// The emitter radius a freshly loaded project actually runs, through the same two calls the engine
// makes: the parameter's base is what the document set, and `applyParticleParameters` is what turns
// it into the system the renderer draws.
float runtimeRadius(app::Engine& engine, const char* node) {
    engine.update(FrameTime{});
    const scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    for (const scene::ParticleSystem& ps : comp->scene().particles) {
        if (ps.name == node) {
            return ps.extent.x;
        }
    }
    FAIL("no particle system called '" << node << "' in the flattened scene");
    return 0.0f;
}

float baseRadius(app::Engine& engine, const char* node) {
    const params::IParameter* p = engine.params().find(std::string("particles/") + node + "/extent");
    REQUIRE(p != nullptr);
    return p->baseComponent(0);
}

// One drag of the panel's radius control, start to finish: the three calls
// `WorldEditPanel::drawParticleSettings` makes around `ImGui::DragFloat`, with the ImGui taken out.
bool dragRadiusTo(app::Engine& engine, ui::EditHistory& history, const char* node, float metres) {
    const std::string path = std::string("particles/") + node + "/extent";
    const scene::CompositionNode* object = engine.composition()->findNode(node);
    REQUIRE(object != nullptr);
    history.beginDrag(engine, "radius " + std::string(node), {path}, {std::string(node)});
    const glm::vec3 want = scene::particleExtentFromRadius(object->particles.extent, metres);
    const bool written = ui::setBaseComponents(engine, path, {want.x, want.y, want.z});
    history.commitDrag(engine);
    return written;
}

std::string bytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("An emitter radius set through the panel survives the save the render loads",
          "[integration][project][particles][ui]") {
    const fs::path dir = scratch("radius");
    const fs::path scene = dir / "scene.json";
    const fs::path project = dir / "project.json";

    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.addNode(beamNode()).has_value());
    REQUIRE(session.saveComposition(scene).has_value());
    const std::string sceneBefore = bytes(scene);

    // The control. Nothing has been edited, so the project's reference to the scene is enough and a
    // reloaded engine must find the 7.8 m the scene authors. This arm passed before the fix and
    // passes after it; without it, an arm that finds 0.42 proves nothing about where it came from.
    REQUIRE(session.saveProject(project).has_value());
    {
        app::Engine render(app::EngineMode::Offline);
        REQUIRE(render.loadProject(project).has_value());
        CHECK_THAT(runtimeRadius(render, "visitor-beam"),
                   Catch::Matchers::WithinAbs(7.8, 0.001));
        CHECK_THAT(baseRadius(render, "visitor-beam"), Catch::Matchers::WithinAbs(7.8, 0.001));
    }

    // The arm: the owner's drag, to the value they set.
    ui::EditHistory history;
    REQUIRE(dragRadiusTo(session, history, "visitor-beam", 0.42f));
    CHECK_THAT(runtimeRadius(session, "visitor-beam"), Catch::Matchers::WithinAbs(0.42, 0.001));
    REQUIRE(session.saveProject(project).has_value());

    // ...is still 0.42 m in the engine an offline render builds from that file, which is a different
    // process reading a document and not this session's memory.
    {
        app::Engine render(app::EngineMode::Offline);
        REQUIRE(render.loadProject(project).has_value());
        CHECK_THAT(runtimeRadius(render, "visitor-beam"), Catch::Matchers::WithinAbs(0.42, 0.001));
    }

    // ...and it is *written as 0.42*, which is the half of this that is not about persistence.
    // Before the fix the same drag left `0.053846150636672974` here: correct, reloadable, and
    // unreadable without the scene file open beside it. The assertion is on the number in the
    // document rather than on what reloading it does, because the defect was that a person and a
    // guard could not tell what the number meant.
    {
        std::ifstream in(project);
        nlohmann::json doc;
        in >> doc;
        REQUIRE(doc["parameters"].contains("particles/visitor-beam/extent"));
        const nlohmann::json& saved = doc["parameters"]["particles/visitor-beam/extent"];
        REQUIRE(saved.is_array());
        REQUIRE(saved.size() == 3);
        INFO("the project holds " << saved.dump() << " for a beam the panel showed as 0.42 m");
        CHECK(std::fabs(saved[0].get<double>() - 0.42) <= 0.001);
    }

    // ...and the scene file was not touched. Byte for byte: it is shared between projects and it is
    // fingerprinted by every project that references it, so "the editor rewrote it" is not a small
    // thing to get wrong.
    CHECK(bytes(scene) == sceneBefore);

    // ...and the drag is one undo. This was the blocker the panel's own comment named -- "the only
    // form the undo system supports, since EditCommand carries parameter changes and whole nodes
    // but has no entry for 'a node's payload changed'" -- and it is an argument *for* the boundary
    // rather than a cost of it: an absolute parameter is a `ParamChange` like any other.
    REQUIRE(history.undoSize() == 1);
    const ui::EditApply undone = history.undo(session);
    CHECK(undone.ok());
    CHECK_THAT(runtimeRadius(session, "visitor-beam"), Catch::Matchers::WithinAbs(7.8, 0.001));
}

TEST_CASE("The radius control resizes a box emitter without squaring it",
          "[integration][project][particles][ui]") {
    const fs::path dir = scratch("box");
    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.addNode(boxNode()).has_value());
    REQUIRE(session.saveComposition(dir / "scene.json").has_value());

    // The control: as authored, 30 x 18 x 4.
    const scene::Composition* comp = session.composition();
    REQUIRE(comp != nullptr);
    session.update(FrameTime{});
    const scene::ParticleSystem* live = nullptr;
    for (const scene::ParticleSystem& ps : comp->scene().particles) {
        if (ps.name == "streaks") {
            live = &ps;
        }
    }
    REQUIRE(live != nullptr);
    CHECK_THAT(live->extent.x, Catch::Matchers::WithinAbs(30.0, 0.001));
    CHECK_THAT(live->extent.y, Catch::Matchers::WithinAbs(18.0, 0.001));
    CHECK_THAT(live->extent.z, Catch::Matchers::WithinAbs(4.0, 0.001));

    // Halved. The proportions are the scene's authorship and the size is the slider's, so 15 m of x
    // must come with 9 and 2 and not with a cube. An absolute parameter written straight into all
    // three components is the obvious implementation of "stop multiplying" and it is wrong: it turns
    // a volume of streaks into a box the first time anybody touches the control.
    ui::EditHistory history;
    REQUIRE(dragRadiusTo(session, history, "streaks", 15.0f));
    session.update(FrameTime{});
    CHECK_THAT(live->extent.x, Catch::Matchers::WithinAbs(15.0, 0.001));
    CHECK_THAT(live->extent.y, Catch::Matchers::WithinAbs(9.0, 0.001));
    CHECK_THAT(live->extent.z, Catch::Matchers::WithinAbs(2.0, 0.001));
}

// The other half of the pair, and the reason the two controls sat next to each other lying in
// different directions for so long: `spread` was already absolute and already right, and nobody
// comparing them noticed because both *display* an absolute quantity.
TEST_CASE("The emitter's cone angle round-trips the same way its radius does",
          "[integration][project][particles][ui]") {
    const fs::path dir = scratch("spread");
    const fs::path project = dir / "project.json";
    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.addNode(beamNode()).has_value());
    REQUIRE(session.saveComposition(dir / "scene.json").has_value());

    REQUIRE(session.saveProject(project).has_value());
    {
        app::Engine render(app::EngineMode::Offline);
        REQUIRE(render.loadProject(project).has_value());
        const params::IParameter* p = render.params().find("particles/visitor-beam/spread");
        REQUIRE(p != nullptr);
        CHECK_THAT(p->baseComponent(0), Catch::Matchers::WithinAbs(0.042, 0.0001));
    }

    ui::EditHistory history;
    history.beginDrag(session, "spread visitor-beam", {"particles/visitor-beam/spread"},
                      {std::string("visitor-beam")});
    REQUIRE(ui::setBaseComponents(session, "particles/visitor-beam/spread", {0.128f}));
    history.commitDrag(session);
    REQUIRE(session.saveProject(project).has_value());
    {
        app::Engine render(app::EngineMode::Offline);
        REQUIRE(render.loadProject(project).has_value());
        const params::IParameter* p = render.params().find("particles/visitor-beam/spread");
        REQUIRE(p != nullptr);
        CHECK_THAT(p->baseComponent(0), Catch::Matchers::WithinAbs(0.128, 0.0001));
        render.update(FrameTime{});
        for (const scene::ParticleSystem& ps : render.composition()->scene().particles) {
            if (ps.name == "visitor-beam") {
                CHECK_THAT(ps.spread, Catch::Matchers::WithinAbs(0.128, 0.0001));
            }
        }
    }
}
