// world_preview: renders a WorldMap to a top-down PNG so geography can be judged before any of it
// is turned into triangles. Hypsometric tint plus hillshade plus 10 m contours plus water, which is
// the same set of cues a topographic map uses, for the same reason: it makes shape legible.
//
//   avgen_world_preview [world.json] [out.png] [pixels]

#include "assets/image.hpp"
#include "world/world_map.hpp"
#include "world/terrain.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace avgen;

namespace {

glm::vec3 hypsometric(float t) {
    // A ramp that separates by elevation band rather than looking pretty: deep green low ground,
    // olive mid slopes, tan shoulders, pale rock on top.
    const glm::vec3 stops[5] = {{0.06f, 0.22f, 0.14f}, {0.16f, 0.34f, 0.16f}, {0.42f, 0.42f, 0.20f},
                                {0.60f, 0.50f, 0.34f}, {0.86f, 0.86f, 0.82f}};
    const float u = std::clamp(t, 0.0f, 1.0f) * 4.0f;
    const auto i = static_cast<int>(u);
    const int j = std::min(i + 1, 4);
    return glm::mix(stops[i], stops[j], u - static_cast<float>(i));
}

} // namespace

int main(int argc, char** argv) {
    const std::string worldPath = argc > 1 ? argv[1] : "";
    const std::string outPath = argc > 2 ? argv[2] : "world_preview.png";
    const int size = argc > 3 ? std::atoi(argv[3]) : 768;

    world::WorldMap map = world::defaultWorld();
    if (!worldPath.empty()) {
        std::ifstream in(worldPath);
        if (!in) {
            std::fprintf(stderr, "cannot open %s\n", worldPath.c_str());
            return 1;
        }
        nlohmann::json j;
        in >> j;
        auto parsed = world::worldMapFromJson(j.contains("world") ? j.at("world") : j);
        if (!parsed) {
            std::fprintf(stderr, "%s\n", parsed.error().message.c_str());
            return 1;
        }
        map = *parsed;
    }

    std::vector<float> heights(static_cast<std::size_t>(size) * size);
    float lo = 1e30f;
    float hi = -1e30f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const glm::vec2 p = map.min() + map.size * glm::vec2((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                                                 (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
            const float h = map.height(p);
            heights[static_cast<std::size_t>(y) * size + x] = h;
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    }
    std::printf("world '%s': %.0f x %.0f m, height %.1f .. %.1f m (%.1f m of relief)\n", map.name.c_str(),
                map.size.x, map.size.y, lo, hi, hi - lo);

    const float metresPerPixel = map.size.x / static_cast<float>(size);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4, 255);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * size + x;
            const float h = heights[i];
            const float t = (h - lo) / std::max(hi - lo, 1e-3f);
            glm::vec3 c = hypsometric(t);
            // Hillshade from the north-west, the cartographic convention, by finite differences on
            // the rendered grid rather than the analytic normal -- what is shaded is what is shown.
            const float hx = heights[static_cast<std::size_t>(y) * size + std::min(x + 1, size - 1)] -
                             heights[static_cast<std::size_t>(y) * size + std::max(x - 1, 0)];
            const float hz = heights[static_cast<std::size_t>(std::min(y + 1, size - 1)) * size + x] -
                             heights[static_cast<std::size_t>(std::max(y - 1, 0)) * size + x];
            const glm::vec3 n = glm::normalize(glm::vec3(-hx, 4.0f * metresPerPixel, -hz));
            const float shade = std::clamp(glm::dot(n, glm::normalize(glm::vec3(-0.5f, 0.7f, -0.5f))), 0.0f, 1.0f);
            c *= 0.35f + 0.85f * shade;
            // 10 m contours: a band wherever the height crosses a multiple of 10.
            const float band = std::fabs(std::fmod(h, 10.0f));
            if (std::min(band, 10.0f - band) < std::max(std::fabs(hx), std::fabs(hz)) * 0.5f) {
                c *= 0.72f;
            }
            const glm::vec2 p = map.min() + map.size * glm::vec2((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                                                 (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
            const float water = map.waterSurface(p);
            if (h < water) {
                const float depth = std::clamp((water - h) / 3.0f, 0.0f, 1.0f);
                c = glm::mix(glm::vec3(0.25f, 0.55f, 0.62f), glm::vec3(0.04f, 0.14f, 0.26f), depth);
            }
            for (int k = 0; k < 3; ++k) {
                rgba[i * 4 + k] = static_cast<std::uint8_t>(std::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }
    // Probes: the heights a scene author actually needs -- where to stand a camera, where the ground
    // is under a focal point -- printed rather than guessed at from a colour ramp.
    for (int i = 4; i + 1 < argc; i += 2) {
        const glm::vec2 q(static_cast<float>(std::atof(argv[i])), static_cast<float>(std::atof(argv[i + 1])));
        const world::Sample sm = map.sample(q, 0.6f);
        std::printf("  probe (%.1f, %.1f): height %.2f  slope %.2f  %s\n", q.x, q.y, sm.height, sm.slope,
                    sm.submerged ? "under water" : "dry");
    }
    if (auto r = assets::writePng(outPath, static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), rgba); !r) {
        std::fprintf(stderr, "%s\n", r.error().message.c_str());
        return 1;
    }
    std::printf("wrote %s\n", outPath.c_str());
    return 0;
}
