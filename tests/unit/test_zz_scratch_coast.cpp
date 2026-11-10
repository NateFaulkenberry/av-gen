// SCRATCH (not for commit): coastal steps and auto levels on art's land3 tiles.
#include "scene/tile_terrain.hpp"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
using namespace avgen;
TEST_CASE("scratch coast", "[.scratchcoast]") {
    std::ifstream in("/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/c334afa4-33e7-45d3-b0f3-889614b6118b/scratchpad/r2/land3-tiles.json");
    auto spec = scene::tileTerrainFromJson(nlohmann::json::parse(in));
    REQUIRE(spec.has_value());
    const auto& s = *spec;
    auto feature = [&](std::size_t i, float pad, glm::vec2& lo, glm::vec2& hi) {};
    (void)feature;
    std::vector<std::pair<std::string, glm::vec4>> boxes;
    for (const auto& r : s.ridges) { glm::vec2 lo(1e9f), hi(-1e9f); for (auto p : r.points) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
        boxes.push_back({"ridge " + r.name, {lo.x - r.width, lo.y - r.width, hi.x + r.width, hi.y + r.width}}); }
    for (const auto& r : s.rises) { const float e = r.radius + r.edge + 20.0f;
        boxes.push_back({"rise " + r.name, {r.centre.x - e, r.centre.y - e, r.centre.x + e, r.centre.y + e}}); }
    float worstAll = 0.0f;
    for (const auto& [name, b] : boxes) {
        float coastStep = 0.0f, anyStep = 0.0f;
        for (float z = b.y; z <= b.w; z += 4.0f) for (float x = b.x; x <= b.z; x += 4.0f) {
            const auto a = scene::tileSample(s, x + 2.0f, z + 2.0f);
            for (glm::vec2 o : {glm::vec2(4, 0), glm::vec2(0, 4)}) {
                const auto n = scene::tileSample(s, x + 2.0f + o.x, z + 2.0f + o.y);
                const float st = std::abs(a.height - n.height);
                anyStep = std::max(anyStep, st);
                if ((a.water < 0.5f) != (n.water < 0.5f) || std::min(a.water, n.water) < 0.5f && std::max(a.water, n.water) > 0.01f) coastStep = std::max(coastStep, st);
            }
        }
        worstAll = std::max(worstAll, coastStep);
        std::printf("STEP %-24s coast %6.2f m  any %6.2f m\n", name.c_str(), coastStep, anyStep);
    }
    std::printf("STEP worst coastal step %.2f m\n", worstAll);
    for (std::size_t i = 0; i < s.rivers.size(); ++i) std::printf("LEVEL river %s source %.3f\n", s.rivers[i].name.c_str(), scene::tileRiverSourceLevel(s, i));
    for (std::size_t i = 0; i < s.ponds.size(); ++i) std::printf("LEVEL pond %s %.3f\n", s.ponds[i].name.c_str(), scene::tilePondLevel(s, i));
}
