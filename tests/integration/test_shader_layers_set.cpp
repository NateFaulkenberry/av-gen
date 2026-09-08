// GPU-free: ShaderLayerSet registers parameters, packs inputs, reloads and serialises.
#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "shaders/shader_layers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace avgen;

namespace {
const char* kShader = R"(/*{
  "INPUTS": [
    {"NAME": "speed", "TYPE": "float", "DEFAULT": 2.0, "MIN": 0.0, "MAX": 10.0},
    {"NAME": "tint", "TYPE": "color", "DEFAULT": [0.1, 0.2, 0.3, 1.0]},
    {"NAME": "on", "TYPE": "bool", "DEFAULT": true},
    {"NAME": "steps", "TYPE": "long", "DEFAULT": 3, "MIN": 1, "MAX": 8}
  ]
}*/
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> { return inputs.tint * inputs.speed; }
)";

std::filesystem::path writeShader(const char* name, const char* source) {
    const auto path = std::filesystem::temp_directory_path() / (std::string("avgen_set_") + name + ".wgsl");
    std::ofstream(path) << source;
    return path;
}
} // namespace

TEST_CASE("ShaderLayerSet registers inputs as parameters and packs their finals", "[shaders][layers]") {
    params::ParameterSet params;
    shaders::ShaderLayerSet set(params, 0.0);
    const auto path = writeShader("pack", kShader);
    auto id = set.add(path, shaders::LayerStage::Background);
    REQUIRE(id.has_value());
    auto* layer = set.find(*id);
    REQUIRE(layer != nullptr);
    CHECK(layer->name == "avgen_set_pack");
    REQUIRE(layer->inputParams.size() == 4);
    CHECK(params.findAs<float>("shader/avgen_set_pack/speed")->value() == 2.0f);
    CHECK(params.findAs<glm::vec4>("shader/avgen_set_pack/tint")->kind() == params::ParamKind::Color);
    CHECK(params.findAs<bool>("shader/avgen_set_pack/on")->value());
    CHECK(params.findAs<int>("shader/avgen_set_pack/steps")->value() == 3);

    FrameTime time{};
    time.renderTime = 1.5;
    shaders::StdUniforms base;
    base.audio[1] = 0.7f;
    set.update(time, base);
    // Layout: speed f32 @0, tint vec4 @16, on u32 @32, steps i32 @36 -> size 48.
    REQUIRE(layer->packedInputs.size() == 48);
    float speed = 0.0f;
    std::memcpy(&speed, layer->packedInputs.data(), 4);
    CHECK(speed == 2.0f);
    float tintB = 0.0f;
    std::memcpy(&tintB, layer->packedInputs.data() + 16 + 8, 4);
    CHECK_THAT(static_cast<double>(tintB), Catch::Matchers::WithinAbs(0.3, 1e-6));
    std::uint32_t on = 0;
    std::memcpy(&on, layer->packedInputs.data() + 32, 4);
    CHECK(on == 1u);
    std::int32_t steps = 0;
    std::memcpy(&steps, layer->packedInputs.data() + 36, 4);
    CHECK(steps == 3);
    CHECK(layer->std.time == 1.5f);
    CHECK(layer->std.audio[1] == 0.7f);

    // Reload keeps values for inputs that still exist.
    params.findAs<float>("shader/avgen_set_pack/speed")->setBase(7.0f);
    REQUIRE(set.reload(*id).has_value());
    CHECK(layer->version == 2);
    CHECK(params.findAs<float>("shader/avgen_set_pack/speed")->base() == 7.0f);

    // Removing unregisters.
    CHECK(set.remove(*id));
    CHECK(params.find("shader/avgen_set_pack/speed") == nullptr);
    CHECK_FALSE(set.add("/nope/missing.wgsl", shaders::LayerStage::Post).has_value());
    std::filesystem::remove(path);
}

TEST_CASE("Shader layers round-trip through the engine's project file", "[integration][shaders]") {
    const auto shader = writeShader("project", kShader);
    const auto project = std::filesystem::temp_directory_path() / "avgen_shader_project.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        auto id = engine.addShaderLayer(shader, shaders::LayerStage::Post);
        REQUIRE(id.has_value());
        engine.params().findAs<float>("shader/avgen_set_project/speed")->setBase(4.5f);
        params::ModRoute route{.source = "audio.bass", .target = "shader/avgen_set_project/speed", .amount = 1.0f};
        engine.modulator().addRoute(route);
        engine.rebind();
        CHECK(engine.modulator().bound());
        REQUIRE(engine.saveProject(project).has_value());
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    REQUIRE(engine.shaderLayers().size() == 1);
    const auto& layer = *engine.shaderLayers().layers().front();
    CHECK(layer.stage == shaders::LayerStage::Post);
    CHECK(layer.path == shader);
    REQUIRE(engine.params().find("shader/avgen_set_project/speed") != nullptr);
    CHECK_THAT(static_cast<double>(engine.params().find("shader/avgen_set_project/speed")->baseComponent(0)),
               Catch::Matchers::WithinAbs(4.5, 1e-6));
    bool routed = false;
    for (const auto& r : engine.modulator().routes()) {
        routed = routed || (r.target == "shader/avgen_set_project/speed" && r.enabled);
    }
    CHECK(routed);
    // Scene swap keeps the layer and re-registers its parameters.
    engine.loadOrbScene();
    CHECK(engine.params().find("shader/avgen_set_project/speed") != nullptr);
    CHECK(engine.shaderLayers().size() == 1);
    std::filesystem::remove(project);
    std::filesystem::remove(shader);
}
