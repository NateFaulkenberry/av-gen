// ADR-2111..2113 (Neon Dreams iteration 9): the march's per-frame jitter is a spread at the common frame rates (not a
// ramp), bobbing formations move their lobes (and their bins keep them), and the wisp layer draws thin, tattered
// cloud that a wake parts.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/cloud_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/clouds.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// the wrapped distance between two values on [0, 1)
float circ(float a, float b) {
    const float d = std::fabs(a - b);
    return std::min(d, 1.0f - d);
}

} // namespace

TEST_CASE("the cloud march's frame jitters spread every few frames at 24, 30 and 60 fps",
          "[renderer][clouds][adr2111]") {
    for (const double fps : {24.0, 30.0, 60.0}) {
        for (int component = 0; component < 2; ++component) {
            std::vector<float> v;
            for (int k = 0; k < 240; ++k) {
                FrameTime t{};
                t.renderTime = static_cast<double>(k) / fps;
                const glm::vec2 j = rendering::cloudFrameJitter(t.frameNonce());
                v.push_back(component == 0 ? j.x : j.y);
            }
            // no slow ramp: the mean step between frames is a large fraction of the range (the ADR-2100 light jitter
            // stepped 0.11 a frame at 30 fps and its march jitter 0.056)
            double step = 0.0;
            for (std::size_t k = 1; k < v.size(); ++k) step += circ(v[k], v[k - 1]);
            step /= static_cast<double>(v.size() - 1);
            INFO("fps " << fps << " component " << component << " mean step " << step);
            CHECK(step > 0.2);
            // and any eight frames in a row cover the range with no gap over a third of it
            float worst = 0.0f;
            for (std::size_t k = 0; k + 8 <= v.size(); ++k) {
                std::vector<float> w(v.begin() + static_cast<std::ptrdiff_t>(k),
                                     v.begin() + static_cast<std::ptrdiff_t>(k + 8));
                std::sort(w.begin(), w.end());
                float gap = w.front() + 1.0f - w.back();
                for (std::size_t i = 1; i < w.size(); ++i) gap = std::max(gap, w[i] - w[i - 1]);
                worst = std::max(worst, gap);
            }
            CHECK(worst < 0.34f);
        }
    }
}
