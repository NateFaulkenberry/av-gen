// world_preview: renders a WorldMap to a top-down PNG so geography can be judged before any of it
// is turned into triangles. Hypsometric tint plus hillshade plus 10 m contours plus water, which is
// the same set of cues a topographic map uses, for the same reason: it makes shape legible.
//
//   avgen_world_preview [--biomes] [world.json] [out.png] [pixels] [probeX probeZ]...
//   avgen_world_preview --terrain <style> [--seed N] [--set key=value]... [out.png] [pixels]
//
// The second form generates a map from `terrain_gen`'s artistic parameters instead of loading one,
// which is the only way to look at a style before it has been committed to a recipe. It installs the
// composer's five biomes so `--biomes` means the same thing in both forms, and it prints the water
// courses the generator produced -- the half of the water seam that a picture cannot show.

#include "assets/image.hpp"
#include "world/terrain.hpp"
#include "world/terrain_gen.hpp"
#include "world/terrain_water.hpp"
#include "world/world_composer.hpp"
#include "world/world_recipe.hpp"
#include "world/world_map.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <string>
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

// Distinct, evenly spread inks so a five-biome blend is legible; they are labels, not art.
glm::vec3 biomeInk(int index) {
    const glm::vec3 inks[world::kMaxBiomes] = {{0.16f, 0.42f, 0.62f}, {0.42f, 0.68f, 0.28f}, {0.12f, 0.40f, 0.20f},
                                               {0.62f, 0.56f, 0.44f}, {0.88f, 0.88f, 0.92f}, {0.72f, 0.34f, 0.62f},
                                               {0.90f, 0.62f, 0.24f}, {0.36f, 0.30f, 0.66f}};
    return inks[std::clamp(index, 0, world::kMaxBiomes - 1)];
}

} // namespace

int main(int argc, char** argv) {
    // A leading --biomes swaps the hypsometric tint for the biome blend. The relief is drawn the
    // same way in both, so the two images are the same landscape read two ways: the question a
    // biome map has to answer is whether its regions sit where the geography put them.
    int arg = 1;
    bool biomeView = false;
    bool generate = false;
    std::string recipePath;
    world::TerrainParams params;
    while (arg < argc && std::string(argv[arg]).rfind("--", 0) == 0) {
        const std::string flag = argv[arg];
        if (flag == "--biomes") {
            biomeView = true;
            ++arg;
        } else if (flag == "--terrain" && arg + 1 < argc) {
            const auto style = world::terrainStyleFromName(argv[arg + 1]);
            if (!style) {
                std::fprintf(stderr, "unknown terrain style '%s'\n", argv[arg + 1]);
                return 1;
            }
            params = world::terrainPreset(*style);
            generate = true;
            arg += 2;
        } else if (flag == "--recipe" && arg + 1 < argc) {
            recipePath = argv[arg + 1];
            generate = true;
            arg += 2;
        } else if (flag == "--set" && arg + 1 < argc) {
            const std::string kv = argv[arg + 1];
            const auto eq = kv.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "--set wants key=value, got '%s'\n", kv.c_str());
                return 1;
            }
            nlohmann::json patch = params.toJson();
            const std::string key = kv.substr(0, eq);
            const std::string value = kv.substr(eq + 1);
            if (key == "style") {
                patch[key] = value;
            } else if (key == "seed") {
                patch[key] = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
            } else {
                patch[key] = std::atof(value.c_str());
            }
            auto parsed = world::TerrainParams::fromJson(patch);
            if (!parsed) {
                std::fprintf(stderr, "%s\n", parsed.error().message.c_str());
                return 1;
            }
            params = *parsed;
            arg += 2;
        } else {
            std::fprintf(stderr, "unknown flag '%s'\n", flag.c_str());
            return 1;
        }
    }
    const std::string worldPath = generate ? "" : (argc > arg ? argv[arg] : "");
    const int outArg = generate ? arg : arg + 1;
    const std::string outPath = argc > outArg ? argv[outArg] : "world_preview.png";
    const int size = argc > outArg + 1 ? std::atoi(argv[outArg + 1]) : 768;
    const int firstProbe = outArg + 2;

    world::WorldMap map = world::defaultWorld();
    if (generate) {
        if (!recipePath.empty()) {
            // The composer's own path, not a reconstruction of it: whatever `terrainFor` returns is
            // what `--generate` will put in the scene, biomes and all.
            auto recipe = world::WorldRecipe::loadFile(recipePath);
            if (!recipe) {
                std::fprintf(stderr, "%s\n", recipe.error().message.c_str());
                return 1;
            }
            params = recipe->terrain;
            params.name = recipe->world;
            params.seed = recipe->seed;
            params.extent = recipe->extent;
        }
        params.name = recipePath.empty() ? std::string("preview-") + world::terrainStyleName(params.style)
                                         : params.name;
        map = world::generateTerrain(params);
        map.biomes = world::composerBiomes();
        map.prepare();
        std::printf("%s\n", params.toJson().dump().c_str());
        if (auto v = map.validate(); !v) {
            std::fprintf(stderr, "generated map is invalid: %s\n", v.error().message.c_str());
            return 1;
        }
        // A cross-section through each river, because a picture from above cannot show whether the
        // water is in a channel or lying on top of one.
        for (const world::WaterCourse& c : world::waterCourses(map)) {
            if (c.kind != world::WaterKind::River || c.centreline.size() < 5) {
                continue;
            }
            const std::size_t mid = c.centreline.size() / 2;
            const glm::vec2 at(c.centreline[mid].x, c.centreline[mid].z);
            const glm::vec2 flow = c.flowAt(at);
            const glm::vec2 across(-flow.y, flow.x);
            std::printf("  section %-10s surface %.2f  bed %.2f |", c.name.c_str(), c.surfaceAt(at),
                        map.height(at));
            for (float m = -4.0f; m <= 4.01f; m += 1.0f) {
                std::printf(" %+.1f", map.height(at + across * (c.halfWidth * m)) - c.surfaceAt(at));
            }
            std::printf("  (metres above the water line at -4..+4 half-widths)\n");
        }
        for (const world::WaterCourse& c : world::waterCourses(map)) {
            std::printf("  water: %-10s %-5s %2zu nodes, half-width %.1f m, depth %.1f m, "
                        "length %.0f m, descent %.1f m, flow %.2f m/s\n",
                        c.name.c_str(), world::waterKindName(c.kind), c.centreline.size(), c.halfWidth,
                        c.depth, c.length, c.descent, c.flowSpeed());
            const glm::vec3& head = c.centreline.front();
            const glm::vec3& mouth = c.centreline.back();
            std::printf("             head (%.0f, %.0f) at %.1f m  ->  mouth (%.0f, %.0f) at %.1f m\n",
                        head.x, head.z, head.y, mouth.x, mouth.z, mouth.y);
        }
    } else if (!worldPath.empty()) {
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

    // What the rules have to be written against. A biome band placed without knowing the spread of
    // the value it bands is a guess: scree asked for slope above 0.34 on terrain whose slope reaches
    // 0.4 in a handful of places, and duly owned one per cent of the world.
    {
        std::vector<float> slopes;
        std::vector<float> moistures;
        constexpr int kStep = 4;
        for (int y = 0; y < size; y += kStep) {
            for (int x = 0; x < size; x += kStep) {
                const glm::vec2 q =
                    map.min() + map.size * glm::vec2((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                                     (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
                const world::Sample sm = map.sample(q, 0.6f);
                slopes.push_back(sm.slope);
                moistures.push_back(sm.moisture);
            }
        }
        const auto pct = [](std::vector<float>& v, double q) {
            std::sort(v.begin(), v.end());
            return v.empty() ? 0.0f : v[std::min(v.size() - 1, static_cast<std::size_t>(q * (v.size() - 1)))];
        };
        std::printf("  slope    p10 %.3f p50 %.3f p90 %.3f p99 %.3f\n", pct(slopes, 0.1), pct(slopes, 0.5),
                    pct(slopes, 0.9), pct(slopes, 0.99));
        std::printf("  moisture p10 %.2f p50 %.2f p90 %.2f\n", pct(moistures, 0.1), pct(moistures, 0.5),
                    pct(moistures, 0.9));
    }

    // How much of the map each biome actually owns. "Meadow looks dominant" is an impression; a
    // biome set is balanced or not, and that is a number.
    if (!map.biomes.empty()) {
        std::vector<int> owned(map.biomes.biomes.size(), 0);
        int total = 0;
        constexpr int kStep = 4;
        for (int y = 0; y < size; y += kStep) {
            for (int x = 0; x < size; x += kStep) {
                const glm::vec2 q =
                    map.min() + map.size * glm::vec2((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                                     (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
                const world::Sample sm = map.sample(q, 0.6f);
                ++owned[static_cast<std::size_t>(map.biomes.at(sm.altitude, sm.slope, sm.moisture, q).dominant())];
                ++total;
            }
        }
        std::printf("  biomes:");
        for (std::size_t b = 0; b < owned.size(); ++b) {
            std::printf(" %s %.0f%%", map.biomes.biomes[b].name.c_str(),
                        100.0 * owned[b] / std::max(total, 1));
        }
        std::printf("\n");
    }

    const float metresPerPixel = map.size.x / static_cast<float>(size);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4, 255);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * size + x;
            const float h = heights[i];
            const float t = (h - lo) / std::max(hi - lo, 1e-3f);
            const glm::vec2 q = map.min() + map.size * glm::vec2((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                                                 (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
            glm::vec3 c = hypsometric(t);
            if (biomeView && !map.biomes.empty()) {
                // The blend itself, not the dominant biome: a map of hard cells would hide exactly
                // what needs checking, which is how wide the transitions are.
                const world::Sample sm = map.sample(q, 0.6f);
                const world::BiomeWeights bw = map.biomes.at(sm.altitude, sm.slope, sm.moisture, q);
                c = glm::vec3(0.0f);
                for (int b = 0; b < bw.count; ++b) {
                    c += biomeInk(b) * bw.weights[static_cast<std::size_t>(b)];
                }
            }
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
            const float water = map.waterSurface(q);
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
    for (int i = firstProbe; i + 1 < argc; i += 2) {
        const glm::vec2 q(static_cast<float>(std::atof(argv[i])), static_cast<float>(std::atof(argv[i + 1])));
        const world::Sample sm = map.sample(q, 0.6f);
        const world::BiomeWeights bw = map.biomes.at(sm.altitude, sm.slope, sm.moisture, q);
        const char* biome = map.biomes.empty() ? "-" : map.biomes.biomes[static_cast<std::size_t>(bw.dominant())].name.c_str();
        std::printf("  probe (%.1f, %.1f): height %.2f  slope %.2f  moisture %.2f  %s  biome %s (axis %.2f)\n",
                    q.x, q.y, sm.height, sm.slope, sm.moisture, sm.submerged ? "wet" : "dry", biome, bw.axis());
    }
    if (auto r = assets::writePng(outPath, static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), rgba); !r) {
        std::fprintf(stderr, "%s\n", r.error().message.c_str());
        return 1;
    }
    std::printf("wrote %s\n", outPath.c_str());
    return 0;
}
