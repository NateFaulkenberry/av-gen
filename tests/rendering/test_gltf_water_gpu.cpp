// ADR-2020: a gltf node's water material on the GPU -- the acceptance test of the request that asked for it
// (docs/requests/2026-11-11-gltf-water-materials.md §7). A generated GLB: a 3 km plane at y = 0 whose material is
// `Lake`, a 1500 m ridge 3 km out with its lit face to the camera, and a red block 200 m out standing in the lake.
// The camera sits 5 m above the water, level with the horizon.
//   * with `waterMaterials.Lake` the ridge is in the lake (the sky mirror: it is past the march's 2000 m) and so is the
//     block (the march: it is too near for the mirror to trust);
//   * without the key, neither is: the lake is the asset's PBR sheet and reflects the environment cube alone;
//   * the live ladder's `nowaterssr` (waterSsrScale 0) takes the block's reflection and leaves the ridge's;
//   * `skyMirror` 0 takes the ridge's and leaves the block's;
//   * a key that names no material in the asset renders byte-identically to no key.
// With AVGEN_WATER_DUMP set, each frame is written there as a PNG, and the scene and its GLB beside them (for
// `avgen --headless --ab nowaterssr`).

#include "support/gltf_fixture.hpp"
#include "support/temp_dir.hpp"
#include "support/water_bench.hpp"

#include "rendering/water_renderer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::testsupport;
namespace fs = std::filesystem;

namespace {

constexpr glm::vec3 kEye{0.0f, 5.0f, 0.0f};
constexpr glm::vec3 kLookAt{0.0f, 5.0f, -3000.0f};
// The ridge: 2000 m wide, 1500 m tall, its near face at z = -3000. The block: 40 x 30 x 20 m, 200 m out, right of
// centre, so its reflection sits inside the ridge's and the two can be told apart.
constexpr glm::vec3 kRidgeLo{-1000.0f, 0.0f, -3200.0f};
constexpr glm::vec3 kRidgeHi{1000.0f, 1500.0f, -3000.0f};
constexpr glm::vec3 kBlockLo{40.0f, 0.0f, -220.0f};
constexpr glm::vec3 kBlockHi{80.0f, 30.0f, -200.0f};

fs::path acceptanceGlb() {
    std::array<GlbMesh, 3> meshes;
    meshes[0].node = "water";
    meshes[0].material = "Lake";
    meshes[0].color = {0.1f, 0.45f, 0.45f}; // teal
    meshes[0].roughness = 0.5f;             // the patched value the request's lake carries today
    appendPlane(meshes[0], {-1500.0f, -2900.0f}, {1500.0f, 100.0f}, 0.0f);
    meshes[1].node = "ridge";
    meshes[1].material = "Rock";
    meshes[1].color = {0.85f, 0.85f, 0.85f};
    meshes[1].roughness = 0.9f;
    appendBox(meshes[1], kRidgeLo, kRidgeHi);
    meshes[2].node = "block";
    meshes[2].material = "Red";
    meshes[2].color = {0.9f, 0.04f, 0.03f};
    meshes[2].roughness = 0.7f;
    appendBox(meshes[2], kBlockLo, kBlockHi);
    return writeMeshesGlb("gltf_water_acceptance", meshes);
}

// The request's own `waterMaterials.Lake`, verbatim (§5).
constexpr const char* kLakeWater = R"({"shallowColor": [0.113, 0.250, 0.251], "deepColor": [0.02, 0.09, 0.11],
    "clarity": 6.0, "maxOpacity": 1.0, "edgeFade": 0.6, "fresnel": 0.04, "reflection": 1.0, "reflectionTint": [1, 1, 1],
    "roughness": 0.04, "ripple": 0.03, "rippleScale": 0.05, "rippleSpeed": 0.0, "foam": 0.0, "refraction": 0.0,
    "ssr": 1.0, "ssrDistance": 2000, "ssrSteps": 128, "ssrGain": 1.0, "skyMirror": 1.0})";

// `waterMaterials` is the whole key and its value, or empty for none.
fs::path acceptanceScene(const std::string& name, const fs::path& glb, const std::string& waterMaterials) {
    const std::string text = fmt::format(
        R"({{"format": "avgen-scene", "version": 1, "name": "gltf-water-acceptance",
  "camera": {{"mode": 1, "position": [{}, {}, {}], "target": [{}, {}, {}], "fov": 60, "orbitSpeed": 0}},
  "environment": {{"intensity": 1.0, "volumeDensity": 0.0, "sky": {{"enabled": true, "background": true}}}},
  "lights": [{{"name": "sun", "type": "directional", "direction": [0.3, -0.45, -0.84], "color": [1.0, 0.95, 0.88],
              "intensity": 3.0, "castsShadow": true}}],
  "nodes": [{{"name": "environment", "kind": "gltf", "asset": "{}"{}}}]}})",
        kEye.x, kEye.y, kEye.z, kLookAt.x, kLookAt.y, kLookAt.z, glb.filename().string(),
        waterMaterials.empty() ? std::string() : ", " + waterMaterials);
    const fs::path path = glb.parent_path() / ("avgen_gltf_water_" + name + ".scene.json");
    std::ofstream(path) << text;
    if (const char* dir = std::getenv("AVGEN_WATER_DUMP"); dir != nullptr && dir[0] != '\0') {
        std::error_code ec;
        fs::copy_file(glb, fs::path(dir) / glb.filename(), fs::copy_options::overwrite_existing, ec);
        fs::copy_file(path, fs::path(dir) / path.filename(), fs::copy_options::overwrite_existing, ec);
    }
    return path;
}

struct Rgb {
    double r = 0.0, g = 0.0, b = 0.0;
    [[nodiscard]] double sum() const { return r + g + b; }
    [[nodiscard]] double redness() const { return sum() > 0.0 ? (r - 0.5 * (g + b)) / sum() : 0.0; }
};

double distance(const Rgb& a, const Rgb& b) {
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}

// A screen rectangle, in pixels, rows from the top.
struct Box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    [[nodiscard]] bool empty() const { return x1 <= x0 || y1 <= y0; }
};

Rgb mean(const gpu::ImageF& img, const Box& box) {
    Rgb out;
    std::size_t n = 0;
    for (int y = std::max(box.y0, 0); y < std::min(box.y1, static_cast<int>(img.height)); ++y) {
        for (int x = std::max(box.x0, 0); x < std::min(box.x1, static_cast<int>(img.width)); ++x) {
            const float* p = img.pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            out.r += p[0];
            out.g += p[1];
            out.b += p[2];
            ++n;
        }
    }
    if (n > 0) {
        out.r /= static_cast<double>(n);
        out.g /= static_cast<double>(n);
        out.b /= static_cast<double>(n);
    }
    return out;
}

// Where a world point lands, in pixels.
glm::vec2 project(const scene::Scene& s, glm::vec3 p, std::uint32_t w, std::uint32_t h) {
    const glm::vec4 clip = s.camera.projection(static_cast<float>(w) / static_cast<float>(h)) * s.camera.view() *
                           glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(w), (0.5f - ndc.y * 0.5f) * static_cast<float>(h)};
}

// Where a point's flat-mirror image in the plane y = 0 lands.
glm::vec2 projectMirrored(const scene::Scene& s, glm::vec3 p, std::uint32_t w, std::uint32_t h) {
    return project(s, {p.x, -p.y, p.z}, w, h);
}

bool allFinite(const gpu::ImageF& img) {
    return std::all_of(img.rgba.begin(), img.rgba.end(), [](float v) { return std::isfinite(v); });
}

} // namespace

TEST_CASE("an imported lake with a water material mirrors the far ridge and the near block",
          "[gpu][renderer][water][gltf][adr2020]") {
    const fs::path glb = acceptanceGlb();
    const fs::path withKey = acceptanceScene("lake", glb, std::string(R"("waterMaterials": {"Lake": )") + kLakeWater + "}");
    const fs::path without = acceptanceScene("plain", glb, "");
    const fs::path misnamed = acceptanceScene("misnamed", glb, R"("waterMaterials": {"Lakee": {"skyMirror": 1}})");

    constexpr std::uint32_t kW = 480;
    constexpr std::uint32_t kH = 270;
    WaterBench bench = WaterBench::make(kW, kH, withKey);
    WaterBench::Arm live = bench.arm();
    bench.aim(kEye, kLookAt);
    bench.hold(4.0);
    REQUIRE(bench.scene().waters.size() == 1);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    REQUIRE(w.cinematic());

    // The regions, from the geometry: the horizon row, the ridge's columns, the block's mirror image.
    const scene::Scene& s = bench.scene();
    const float horizon = project(s, {0.0f, kEye.y, -1e5f}, kW, kH).y;
    const glm::vec2 ridgeLeft = project(s, {kRidgeLo.x, 0.0f, kRidgeHi.z}, kW, kH);
    const glm::vec2 ridgeRight = project(s, {kRidgeHi.x, 0.0f, kRidgeHi.z}, kW, kH);
    const glm::vec2 blockA = projectMirrored(s, {kBlockLo.x, kBlockLo.y, kBlockHi.z}, kW, kH);
    const glm::vec2 blockB = projectMirrored(s, {kBlockHi.x, kBlockHi.y, kBlockHi.z}, kW, kH);
    INFO("horizon row " << horizon << ", ridge columns " << ridgeLeft.x << ".." << ridgeRight.x
                        << ", the block's mirror image " << blockA.x << ".." << blockB.x << " x " << blockA.y << ".."
                        << blockB.y);
    const int h0 = static_cast<int>(std::ceil(horizon)) + 4; // below the far shore
    // the ridge's reflection: inside its columns, clear of the block's, from just under the shore down
    const int blockX0 = static_cast<int>(blockA.x) - 6;
    const int blockX1 = static_cast<int>(blockB.x) + 6;
    const Box ridgeMirror{static_cast<int>(ridgeLeft.x) + 8, h0, std::min(blockX0, static_cast<int>(ridgeRight.x) - 8),
                          static_cast<int>(kH) - 8};
    // the same rows outside the ridge: the sky's reflection
    const Box skyMirror{4, h0, static_cast<int>(ridgeLeft.x) - 8, static_cast<int>(kH) - 8};
    // the block's mirror image, its middle (the ripples move its edge)
    const Box blockMirror{static_cast<int>(blockA.x) + 3, static_cast<int>(blockA.y) + 2, static_cast<int>(blockB.x) - 3,
                          static_cast<int>(blockB.y) - 2};
    REQUIRE_FALSE(ridgeMirror.empty());
    REQUIRE_FALSE(skyMirror.empty());
    REQUIRE_FALSE(blockMirror.empty());

    const auto ssrScale = [&](float scale) {
        rendering::QualitySettings q = live.renderer->qualitySettings();
        q.waterSsrScale = scale;
        live.renderer->setQualitySettings(q);
    };
    // the ridge in the lake: its reflection against the sky's, the same rows
    const auto ridgeContrast = [&](const gpu::ImageF& img) { return distance(mean(img, ridgeMirror), mean(img, skyMirror)); };

    const gpu::ImageF on = bench.render(live);
    ssrScale(0.0f);
    const gpu::ImageF noSsr = bench.render(live);
    ssrScale(1.0f);
    w.skyMirror = 0.0f;
    const gpu::ImageF noMirror = bench.render(live);
    w.skyMirror = 1.0f;
    dumpWater(on, "gltf-lake-key");
    dumpWater(noSsr, "gltf-lake-nowaterssr");
    dumpWater(noMirror, "gltf-lake-skymirror0");
    CHECK(allFinite(on));

    REQUIRE(bench.engine->loadComposition(without).has_value());
    bench.aim(kEye, kLookAt);
    bench.hold(4.0);
    REQUIRE(bench.scene().waters.empty());
    const gpu::ImageF plain = bench.render(live);
    dumpWater(plain, "gltf-lake-nokey");

    REQUIRE(bench.engine->loadComposition(misnamed).has_value());
    bench.aim(kEye, kLookAt);
    bench.hold(4.0);
    REQUIRE(bench.scene().waters.empty());
    const gpu::ImageF misnamedFrame = bench.render(live);

    const Rgb ridgeDirect = mean(on, Box{ridgeMirror.x0, static_cast<int>(horizon) - 30, ridgeMirror.x1,
                                         static_cast<int>(horizon) - 4});
    INFO("the ridge itself " << ridgeDirect.r << ", " << ridgeDirect.g << ", " << ridgeDirect.b);
    INFO("ridge-vs-sky contrast in the lake: key " << ridgeContrast(on) << ", nowaterssr " << ridgeContrast(noSsr)
                                                   << ", skyMirror 0 " << ridgeContrast(noMirror) << ", no key "
                                                   << ridgeContrast(plain));
    INFO("redness of the block's mirror image: key " << mean(on, blockMirror).redness() << ", nowaterssr "
                                                     << mean(noSsr, blockMirror).redness() << ", skyMirror 0 "
                                                     << mean(noMirror, blockMirror).redness() << ", no key "
                                                     << mean(plain, blockMirror).redness());

    // with the key: the ridge and the block are both in the lake
    CHECK(ridgeContrast(on) > 4.0 * std::max(ridgeContrast(plain), 1e-3));
    CHECK(mean(on, blockMirror).redness() > 0.25);
    // without it: neither
    CHECK(mean(plain, blockMirror).redness() < 0.1);
    // nowaterssr: the ridge stays (the sky mirror), the block goes (it was the march's)
    CHECK(ridgeContrast(noSsr) > 0.7 * ridgeContrast(on));
    CHECK(mean(noSsr, blockMirror).redness() < 0.1);
    // skyMirror 0: the ridge goes (past the march's reach), the block stays
    CHECK(ridgeContrast(noMirror) < 0.25 * ridgeContrast(on));
    CHECK(mean(noMirror, blockMirror).redness() > 0.25);
    // a key that matched nothing is no key at all
    CHECK(gpu::hashImage(misnamedFrame) == gpu::hashImage(plain));
    CHECK(bench.ctx->errorCount() == 0);
}

// Hidden ([.]): the cinematic lake's cost at 1080p on the acceptance scene (honest timestamps, ADR-1600): the scene
// label's median over interleaved reps, with the water's SSR on and with the live ladder's `nowaterssr`, and the
// same scene without the key (the asset's plain PBR lake).
TEST_CASE("gltf water material: cost (measure)", "[.][gpu][water][gltf][measure][adr2020]") {
    const fs::path glb = acceptanceGlb();
    const fs::path withKey = acceptanceScene("lake", glb, std::string(R"("waterMaterials": {"Lake": )") + kLakeWater + "}");
    const fs::path without = acceptanceScene("plain", glb, "");
    WaterBench lake = WaterBench::make(1920, 1080, withKey);
    WaterBench::Arm live = lake.arm();
    if (!live.renderer->timeline().available()) {
        SKIP("timestamp queries unavailable");
    }
    lake.aim(kEye, kLookAt);
    lake.hold(4.0);
    const auto measure = [&](WaterBench& bench, float ssrScale) {
        rendering::QualitySettings q = live.renderer->qualitySettings();
        q.waterSsrScale = ssrScale;
        live.renderer->setQualitySettings(q);
        std::vector<double> ms;
        for (int i = 0; i < 80; ++i) {
            bench.time.renderTime = 4.0 + i / 60.0;
            REQUIRE(live.renderer->renderFrame(bench.scene(), bench.time, 1920, 1080).has_value());
            if (i >= 20) {
                ms.push_back(live.renderer->timeline().msFor("scene"));
            }
        }
        std::sort(ms.begin(), ms.end());
        return ms[ms.size() / 2];
    };
    WaterBench plainBench = WaterBench::make(1920, 1080, without);
    plainBench.aim(kEye, kLookAt);
    plainBench.hold(4.0);
    std::array<std::vector<double>, 3> ms;
    for (int rep = 0; rep < 7; ++rep) {
        const double a = measure(lake, 1.0f);
        const double b = measure(lake, 0.0f);
        const double c = measure(plainBench, 1.0f);
        if (rep == 0) {
            continue; // warm-up
        }
        ms[0].push_back(a);
        ms[1].push_back(b);
        ms[2].push_back(c);
    }
    const auto list = [](const std::vector<double>& v) {
        std::string out;
        for (const double x : v) {
            out += fmt::format(" {:.3f}", x);
        }
        return out;
    };
    WARN("1080p acceptance lake (level camera, ~half the frame water), the scene label's median (ms), 6 interleaved "
         "reps: cinematic lake, SSR 128 steps"
         << list(ms[0]) << " | nowaterssr" << list(ms[1]) << " | no key (PBR lake)" << list(ms[2]));
    CHECK(ms[0][0] > 0.0);
}
