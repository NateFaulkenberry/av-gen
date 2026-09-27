// The evaluator hook against a REAL Creative Critic (ADR-931's live smoke test). Hidden: it needs a
// running Critic, the GPU and minutes, so it never runs in the suite. Run it by hand against a PRIVATE
// Critic -- never the coordinator's on :8765 --:
//
//   H=<scratch>/critic-home; P=8791; C=~/Documents/GitHub/creative-critic/.venv/bin/critic
//   CRITIC_HOME=$H CRITIC_PORT=$P $C --url http://127.0.0.1:$P start --daemon
//   AVGEN_CRITIC=$C AVGEN_CRITIC_URL=http://127.0.0.1:$P AVGEN_RENDER_PREFIX=<repo>/tools/gpu-lock.sh \
//     build/release/tests/avgen_tests "[.live][evaluate]"
//   CRITIC_HOME=$H $C stop
//
// The test process itself uses no GPU: the render is a child `avgen --render`, behind the prefix.

#include "app/directing_evaluate.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <thread>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

TEST_CASE("live: a candidate set piece rendered on a scratch copy and judged by a running Critic",
          "[.live][evaluate][adr931]") {
    if (std::getenv("AVGEN_CRITIC") == nullptr || std::getenv("AVGEN_CRITIC_URL") == nullptr) {
        SKIP("set AVGEN_CRITIC and AVGEN_CRITIC_URL to a private Critic (see the file's header)");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "tests/data/setpieces/setpiece-lab.json").has_value());
    // One static camera on the field station, and one shot on it: the Critic's scene is its shots.
    scene::CameraDirection direction = engine.composition()->cameraDirection();
    scene::CameraRig rig;
    rig.name = "field wide";
    rig.position = glm::vec3(62.0f, 22.0f, 92.0f);
    rig.target = glm::vec3(62.0f, 16.0f, 22.0f);
    rig.fovDegrees = 50.0f;
    const scene::CameraId id = direction.addCamera(rig);
    scene::CameraShot shot;
    shot.camera = id;
    shot.startSeconds = 50.0;
    shot.endSeconds = 70.0;
    shot.label = "the field abduction";
    direction.shots.push_back(shot);
    REQUIRE(engine.setCameraDirection(direction).has_value());

    const json plan = json::parse(R"({
      "schemaVersion": 1, "id": "ufo", "title": "UFO activity", "tier": "baked",
      "setPieces": [{"key": "field", "template": "abduction", "craft": "saucer", "at": {"seconds": 60},
                     "place": {"point": [62, 22]}, "beamColor": [1.0, 0.25, 0.15],
                     "set": {"animals": 2, "approachSeconds": 6, "hoverHeight": 30}, "framingMetres": 70}]})");
    ai::EvaluationRequest request;
    request.plan = plan;
    request.planId = "ufo";
    request.revision = 1;
    request.candidate = directing::fingerprint(plan);
    request.from = 58.0;
    request.until = 66.0;
    request.mode = "preview";
    request.label = "live smoke";

    app::EvaluatorOptions options = app::evaluatorOptionsFrom(std::nullopt, std::nullopt);
    options.avgen = fs::path(AVGEN_SOURCE_DIR) / "build/release/src/avgen";
    options.keepFiles = std::getenv("AVGEN_EVALUATE_KEEP") != nullptr;
    options.session = "avgen-live-smoke";
    const auto begun = std::chrono::steady_clock::now();
    auto handle = app::makeEvaluationHook(options)(engine, request);
    INFO((handle ? std::string() : handle.error().message));
    REQUIRE(handle.has_value());
    std::string last;
    while (!(*handle)->done()) {
        if (const std::string phase = (*handle)->phase(); phase != last) {
            last = phase;
            fmt::print("  [{:6.1f} s] {}\n",
                       std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count(), phase);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    auto value = (*handle)->take();
    INFO((value ? std::string() : value.error().message));
    REQUIRE(value.has_value());
    (*handle)->settle(engine, *value);
    fmt::print("  [{:6.1f} s] {}\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count(),
               value->value("summary", std::string()));
    fmt::print("{}\n", value->dump(1).substr(0, 4000));
    REQUIRE(engine.directingEvaluations().size() == 1);
    const directing::EvaluationReport& r = engine.directingEvaluations().front();
    CHECK(r.evaluator == "creative-critic");
    CHECK_FALSE(r.jobId.empty());
    CHECK(r.planId == "ufo");
    CHECK(r.from == 58.0);
    CHECK_FALSE(r.headline.empty());
    CHECK_FALSE(r.dimensions.empty());
    // The person's project was not given the candidate.
    CHECK(engine.directingPlans().empty());
}
