// Every public header must compile on its own (include hygiene).
#include "analysis/analysis_runner.hpp"
#include "analysis/analysis_track.hpp"
#include "analysis/analyzer.hpp"
#include "analysis/fft.hpp"
#include "analysis/window.hpp"
#include "audio/analysis_stream.hpp"
#include "audio/audio_file.hpp"
#include "audio/audio_player.hpp"
#include "core/error.hpp"
#include "core/log.hpp"
#include "core/ring_buffer.hpp"
#include "core/rng.hpp"
#include "core/time.hpp"
#include "core/triple_buffer.hpp"
#include "params/modulation.hpp"
#include "params/parameter.hpp"
#include "params/parameter_set.hpp"
#include "params/preset.hpp"
#include "params/processor.hpp"
#include "params/serialization.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/orb_scene.hpp"
#include "scene/scene.hpp"
#include "shaders/shader_format.hpp"
#include "signals/audio_signals.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Parameter<T> template instantiates for every supported type", "[params][headers]") {
    using namespace avgen::params;
    Parameter<float> f(ParamDesc<float>{.path = "a/b", .defaultValue = 0.5f, .hardMin = 0.0f, .hardMax = 1.0f});
    Parameter<int> i(ParamDesc<int>{.path = "a/i", .defaultValue = 2, .hardMin = 0, .hardMax = 10});
    Parameter<bool> b(ParamDesc<bool>{.path = "a/f", .defaultValue = true, .hardMin = false, .hardMax = true});
    Parameter<glm::vec3> v(ParamDesc<glm::vec3>{.path = "a/v", .defaultValue = glm::vec3(1.0f), .hardMin = glm::vec3(0.0f), .hardMax = glm::vec3(2.0f)});
    CHECK(f.label() == "b");
    CHECK(f.group() == "a");
    CHECK(f.value() == 0.5f);
    CHECK(i.componentCount() == 1);
    CHECK(b.baseComponent(0) == 1.0f);
    CHECK(v.componentCount() == 3);
    CHECK(v.kind() == ParamKind::Vec3);
    CHECK(f.softMin(0) == 0.0f);
    CHECK(f.softMax(0) == 1.0f);
    f.setBaseComponent(0, 5.0f);
    CHECK(f.base() == 1.0f);
}
