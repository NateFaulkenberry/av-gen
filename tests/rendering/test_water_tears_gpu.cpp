// Deliberate water tears (ADR-916), measured from outside.
//
// A tear is a thin, stepped seam in which the ripples are compressed into dense parallel stripes: the
// look of the accident ADR-914 removed, placed and bounded on purpose. These tests hold it to the
// properties the owner's brief and the silent-no-op rule ask for:
//   * at tears 0 the surface is byte-identical to the shader without the tear code at all, whatever
//     the other tear settings say (the no-op the rest of the world relies on);
//   * the seams exist, are thin, carry stripes along them, and turn when their direction does;
//   * outside its seams the water is the water with no tears (the brief's "not uniformly busy");
//   * every tear setting reaches the picture, and each reaches it through its control's parameter;
//   * the wind reaches them;
//   * a seek lands on the frame a play reached;
//   * a preview and a final at twice its resolution fade them the same.

#include "support/water_bench.hpp"
#include "support/water_shader_variant.hpp"

#include "rendering/water_renderer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::testsupport;
namespace fs = std::filesystem;

namespace {

constexpr const char* kTearsBegin = "---- tears (ADR-916) begin ----";
constexpr const char* kTearsEnd = "---- tears (ADR-916) end ----";

// ---- a synthetic sheet of water, looked straight down on -------------------------------------------
//
// One flat water quad over a dark bed, its flow lanes set by hand (+X at 0.4 of the fastest body's
// speed, three metres deep, midstream), a moon low enough to put glints on a slope, and the procedural
// sky for the reflection. Straight down so a direction in the world is a direction on the screen:
// the camera's up falls back to +Z, and the image is the world's XZ plane turned half a turn.
//
// From 20 m at 384 px a metre is about 20 px, so the base ripple (1 cycle/m) spans twenty pixels, a
// 1.2 m band about twenty-four, and the five stripes 5 m of shear packs into it (a compression of 4.2)
// about five each: all of it resolved, which is what a test of their shape needs.

scene::MeshData sheet(float half, float y, bool water) {
    scene::MeshData mesh;
    const glm::vec3 lane = water ? glm::vec3(1.0f, 0.4f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec2 uv = water ? glm::vec2(3.0f, 1.0f) : glm::vec2(0.0f);
    mesh.vertices.push_back({{-half, y, -half}, lane, uv});
    mesh.vertices.push_back({{half, y, -half}, lane, uv});
    mesh.vertices.push_back({{half, y, half}, lane, uv});
    mesh.vertices.push_back({{-half, y, half}, lane, uv});
    mesh.indices = {0, 2, 1, 0, 3, 2};
    return mesh;
}

scene::WaterSettings tearSettings() {
    scene::WaterSettings w;
    w.shallowColor = {0.03f, 0.10f, 0.12f};
    w.deepColor = {0.006f, 0.035f, 0.075f};
    w.clarity = 1.25f;
    w.maxOpacity = 0.93f;
    w.fresnel = 0.2f;
    w.reflection = 1.6f;
    w.specular = 1.2f;
    w.roughness = 0.12f;
    w.ripple = 0.08f;
    w.rippleScale = 1.0f;
    w.foam = 0.0f;
    w.glow = 0.0f;
    w.sparkle = 0.0f;
    w.refraction = 0.0f;
    // The seams, dense enough that several cross a 50 m view.
    w.tears = 0.6f;
    w.tearShear = 5.0f;
    w.tearCoverage = 0.9f;
    w.tearCell = 1.2f;
    w.tearSpacing = 5.0f;
    w.tearStretch = 3.0f;
    w.tearFollowsWind = false;
    w.tearAngle = 0.35f;
    w.tearDrift = 0.0f;
    w.tearWind = 0.0f;
    return w;
}

scene::Scene tearScene(const scene::WaterSettings& settings) {
    scene::Scene s;
    s.environment.backgroundColor = {0.01f, 0.015f, 0.03f};
    s.camera.position = {0.0f, 20.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.farPlane = 400.0f;
    scene::PunctualLight moon;
    moon.direction = glm::normalize(glm::vec3(-0.35f, -0.8f, -0.25f));
    moon.intensity = 2.0f;
    s.addLight(moon);
    scene::Entity& bed = s.addEntity("bed", s.addMesh(sheet(160.0f, -3.0f, false)));
    bed.material.baseColor = {0.02f, 0.03f, 0.03f};
    bed.material.roughness = 1.0f;
    scene::Entity& water = s.addEntity("sea", s.addMesh(sheet(120.0f, 0.0f, true)));
    water.style = scene::MeshStyle::Water;
    water.material.program = "sea";
    water.material.doubleSided = true;
    s.waters.push_back({"sea", settings, 0.5f});
    return s;
}

struct Synthetic {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    std::uint32_t size = 384;

    // `dirs` are searched before the live shader tree, so a variant of water.wgsl written there wins.
    static Synthetic make(std::uint32_t size = 384, std::vector<fs::path> dirs = {}) {
        Synthetic s;
        s.size = size;
        s.ctx = makeWaterContext();
        dirs.emplace_back(AVGEN_SHADER_SOURCE_DIR);
        s.shaders = std::make_unique<gpu::ShaderLibrary>(*s.ctx, dirs);
        s.renderer = std::make_unique<rendering::SceneRenderer>(*s.ctx, *s.shaders);
        REQUIRE(s.renderer->init().has_value());
        s.renderer->setPassToggles(waterQuantitativeToggles());
        return s;
    }

    gpu::ImageF render(const scene::WaterSettings& settings, double seconds = 6.0,
                       const std::function<void(scene::Scene&)>& edit = {}) {
        scene::Scene s = tearScene(settings);
        if (edit) {
            edit(s);
        }
        FrameTime time{};
        time.renderTime = seconds;
        time.deltaTime = 1.0 / 60.0;
        renderer->resetTemporalHistory();
        auto image = renderer->renderToImageFloat(s, time, size, size);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
};

// ---- image measurements --------------------------------------------------------------------------

std::vector<char> differs(const std::vector<float>& a, const std::vector<float>& b, float epsilon) {
    std::vector<char> out(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i] = std::fabs(a[i] - b[i]) > epsilon ? 1 : 0;
    }
    return out;
}

struct Component {
    std::size_t pixels = 0;
    std::size_t perimeter = 0; // pixels with a 4-neighbour outside the component
    double length = 0.0;       // extent along the major axis (four standard deviations)
    double orientation = 0.0;  // radians, of the major axis, in [0, pi)
    // How thin: its length over its mean width, the width being twice its area over its perimeter
    // (exact for a long band, and blind to whether the band is straight or stepped, which a principal-
    // axis aspect is not: a Z of thin band has the aspect of a fat blob).
    [[nodiscard]] double slenderness() const {
        const double width = perimeter > 0 ? 2.0 * static_cast<double>(pixels) / static_cast<double>(perimeter) : 1.0;
        return length / std::max(width, 1.0);
    }
};

// 8-connected components of a mask, each with the principal axes of its pixels.
std::vector<Component> components(const std::vector<char>& mask, std::uint32_t w, std::uint32_t h) {
    std::vector<int> label(mask.size(), -1);
    std::vector<Component> out;
    std::vector<std::size_t> stack;
    for (std::size_t seed = 0; seed < mask.size(); ++seed) {
        if (mask[seed] == 0 || label[seed] >= 0) {
            continue;
        }
        const int id = static_cast<int>(out.size());
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        std::size_t n = 0;
        std::size_t perimeter = 0;
        stack.assign(1, seed);
        label[seed] = id;
        while (!stack.empty()) {
            const std::size_t p = stack.back();
            stack.pop_back();
            {
                const long px = static_cast<long>(p % w);
                const long py = static_cast<long>(p / w);
                const auto outside = [&](long x, long y) {
                    return x < 0 || y < 0 || x >= static_cast<long>(w) || y >= static_cast<long>(h) ||
                           mask[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] == 0;
                };
                perimeter += (outside(px - 1, py) || outside(px + 1, py) || outside(px, py - 1) || outside(px, py + 1)) ? 1 : 0;
            }
            const double x = static_cast<double>(p % w);
            const double y = static_cast<double>(p / w);
            sx += x;
            sy += y;
            sxx += x * x;
            syy += y * y;
            sxy += x * y;
            ++n;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const long nx = static_cast<long>(p % w) + dx;
                    const long ny = static_cast<long>(p / w) + dy;
                    if (nx < 0 || ny < 0 || nx >= static_cast<long>(w) || ny >= static_cast<long>(h)) {
                        continue;
                    }
                    const std::size_t q = static_cast<std::size_t>(ny) * w + static_cast<std::size_t>(nx);
                    if (mask[q] != 0 && label[q] < 0) {
                        label[q] = id;
                        stack.push_back(q);
                    }
                }
            }
        }
        const double mx = sx / n, my = sy / n;
        const double cxx = sxx / n - mx * mx, cyy = syy / n - my * my, cxy = sxy / n - mx * my;
        const double tr = cxx + cyy;
        const double disc = std::sqrt(std::max((cxx - cyy) * (cxx - cyy) * 0.25 + cxy * cxy, 0.0));
        const double l1 = tr * 0.5 + disc;
        const double l2 = std::max(tr * 0.5 - disc, 0.25); // a one-pixel-wide line has a width
        Component c;
        c.pixels = n;
        c.perimeter = perimeter;
        c.length = 4.0 * std::sqrt(l1);
        double o = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);
        if (o < 0.0) {
            o += std::numbers::pi;
        }
        c.orientation = o;
        out.push_back(c);
    }
    return out;
}

// The size-weighted mean axis of a set of components, by doubled angles (an axis has no sign).
double meanOrientation(const std::vector<Component>& cs, std::size_t minPixels) {
    double c = 0.0, s = 0.0;
    for (const Component& k : cs) {
        if (k.pixels >= minPixels) {
            c += static_cast<double>(k.pixels) * std::cos(2.0 * k.orientation);
            s += static_cast<double>(k.pixels) * std::sin(2.0 * k.orientation);
        }
    }
    double o = 0.5 * std::atan2(s, c);
    return o < 0.0 ? o + std::numbers::pi : o;
}

// The smallest angle between two axes, in degrees (0..90).
double axisDifferenceDegrees(double a, double b) {
    double d = std::fabs(a - b);
    while (d > std::numbers::pi) {
        d -= std::numbers::pi;
    }
    d = std::min(d, std::numbers::pi - d);
    return d * 180.0 / std::numbers::pi;
}

// Coherence of the local gradient structure, ((l1 - l2) / (l1 + l2)), from the structure tensor of a
// luminance field summed over a (2r+1)^2 window: 1 where every gradient in the window is parallel (stripes), near 0
// where they point every way (isotropic ripples).
std::vector<float> coherence(const std::vector<float>& lum, std::uint32_t w, std::uint32_t h, int r = 2) {
    std::vector<float> jxx(lum.size(), 0.0f), jyy(lum.size(), 0.0f), jxy(lum.size(), 0.0f);
    for (std::uint32_t y = 1; y + 1 < h; ++y) {
        for (std::uint32_t x = 1; x + 1 < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const float gx = 0.5f * (lum[i + 1] - lum[i - 1]);
            const float gy = 0.5f * (lum[i + w] - lum[i - w]);
            jxx[i] = gx * gx;
            jyy[i] = gy * gy;
            jxy[i] = gx * gy;
        }
    }
    std::vector<float> out(lum.size(), 0.0f);
    const auto edge = static_cast<std::uint32_t>(r + 1);
    for (std::uint32_t y = edge; y + edge < h; ++y) {
        for (std::uint32_t x = edge; x + edge < w; ++x) {
            double a = 0, b = 0, c = 0;
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    const std::size_t q = static_cast<std::size_t>(static_cast<int>(y) + dy) * w +
                                          static_cast<std::size_t>(static_cast<int>(x) + dx);
                    a += jxx[q];
                    b += jyy[q];
                    c += jxy[q];
                }
            }
            const double tr = a + b;
            const double disc = std::sqrt(std::max((a - b) * (a - b) + 4.0 * c * c, 0.0));
            out[static_cast<std::size_t>(y) * w + x] = tr > 1e-12 ? static_cast<float>(disc / tr) : 0.0f;
        }
    }
    return out;
}

// The dominant gradient direction of a field at each pixel (radians, 0..pi) and how strongly it
// dominates, from its structure tensor summed over a (2r+1)^2 window.
struct Orientation {
    std::vector<float> angle;
    std::vector<float> strength; // coherence times energy: 0 where the field is flat or isotropic
};

Orientation orientation(const std::vector<float>& field, std::uint32_t w, std::uint32_t h, int r) {
    std::vector<float> jxx(field.size(), 0.0f), jyy(field.size(), 0.0f), jxy(field.size(), 0.0f);
    for (std::uint32_t y = 1; y + 1 < h; ++y) {
        for (std::uint32_t x = 1; x + 1 < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const float gx = 0.5f * (field[i + 1] - field[i - 1]);
            const float gy = 0.5f * (field[i + w] - field[i - w]);
            jxx[i] = gx * gx;
            jyy[i] = gy * gy;
            jxy[i] = gx * gy;
        }
    }
    Orientation out;
    out.angle.assign(field.size(), 0.0f);
    out.strength.assign(field.size(), 0.0f);
    for (int y = r + 1; y + r + 1 < static_cast<int>(h); ++y) {
        for (int x = r + 1; x + r + 1 < static_cast<int>(w); ++x) {
            double a = 0, b = 0, c = 0;
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    const std::size_t q = static_cast<std::size_t>(y + dy) * w + static_cast<std::size_t>(x + dx);
                    a += jxx[q];
                    b += jyy[q];
                    c += jxy[q];
                }
            }
            const std::size_t i = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);
            out.angle[i] = static_cast<float>(0.5 * std::atan2(2.0 * c, a - b));
            out.strength[i] = static_cast<float>(std::sqrt(std::max((a - b) * (a - b) + 4.0 * c * c, 0.0)));
        }
    }
    return out;
}

// How parallel the stripes inside the bands are to the bands themselves: the strength-weighted mean of
// cos(2 * angle) between the image's dominant gradient and the band mask's own (both point across the
// band when the stripes run along it). 1 is parallel everywhere, 0 is no relation, -1 is crosswise.
double stripeAlignment(const std::vector<float>& lum, const std::vector<char>& band, std::uint32_t w, std::uint32_t h) {
    std::vector<float> mask(band.size());
    for (std::size_t i = 0; i < band.size(); ++i) {
        mask[i] = band[i] != 0 ? 1.0f : 0.0f;
    }
    const Orientation stripes = orientation(lum, w, h, 2);
    const Orientation bands = orientation(mask, w, h, 5);
    double sum = 0.0, weight = 0.0;
    for (std::size_t i = 0; i < band.size(); ++i) {
        if (band[i] == 0 || bands.strength[i] <= 0.0f) {
            continue;
        }
        sum += stripes.strength[i] * std::cos(2.0 * (static_cast<double>(stripes.angle[i]) - bands.angle[i]));
        weight += stripes.strength[i];
    }
    return weight > 0.0 ? sum / weight : 0.0;
}

double meanOver(const std::vector<float>& v, const std::vector<char>& mask, bool inside) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if ((mask[i] != 0) == inside) {
            sum += v[i];
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

// ---- the QA scene with seams in view ---------------------------------------------------------------
//
// The QA scene's water block with the tears' static settings written into it -- dense, well-covered
// seams -- so a view of its river is guaranteed to hold some. `tears` itself is left at the value given,
// because the routable three are what the parameter tests move.
nlohmann::json qaSceneWithSeams(float tears) {
    std::ifstream in(qaWaterScene());
    REQUIRE(in.good());
    nlohmann::json doc = nlohmann::json::parse(in);
    for (nlohmann::json& node : doc.at("nodes")) {
        if (node.value("kind", std::string{}) == "terrain") {
            nlohmann::json& water = node["terrain"]["water"];
            water["tears"] = tears;
            water["tearShear"] = 2.5;
            water["tearCoverage"] = 0.9;
            water["tearCell"] = 1.25;
            water["tearSpacing"] = 10.0;
            water["tearStretch"] = 3.0;
            water["tearDirection"] = 0.4;
            water["tearDrift"] = 0.15;
            water["tearWind"] = 0.6;
        }
    }
    return doc;
}

void loadSeamScene(WaterBench& bench, float tears) {
    REQUIRE(bench.engine->setCompositionJson(qaSceneWithSeams(tears)).has_value());
    bench.time = bench.engine->tick(bench.clock);
    bench.engine->setViewport(bench.width, bench.height);
    bench.engine->update(bench.time);
}

void setParam(WaterBench& bench, const std::string& path, float value) {
    auto* p = bench.engine->params().findAs<float>(path);
    INFO(path);
    REQUIRE(p != nullptr);
    p->setBase(value);
    bench.engine->params().resetFinals();
}

} // namespace

// ---- identity at 0 ------------------------------------------------------------------------------
//
// Everything the tears add sits between markers in water.wgsl, and a material whose `tears` is 0 is drawn
// by a pipeline with those blocks compiled out (WaterRenderer). This strips every marked block from the
// live file with its own code -- the struct fields, the tear function, and the lines that read it -- and
// requires the live renderer at tears 0, every other tear setting pushed off its default, to draw the QA
// scene byte-identically to a renderer given only the stripped file. When ADR-916 was written the
// stripped file was textually the shader ADR-914 and ADR-915 left, so this is "B at 0 is A".
//
// One shader with the tear code gated on `tears > 0` failed this: with the tear written in `fs_water`
// and read in `rippleGradient`, the compiler built the shared ripple path differently, and ~130 glint
// channels of this scene moved, by up to 0.125. The control arm is the same settings with the tears on.
TEST_CASE("at tears 0 the water is byte-identical to the shader with no tear code",
          "[gpu][renderer][water][tears][identity]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    int blocks = 0;
    const std::string stripped = stripMarkedBlocks(readWaterShaderSource(), kTearsBegin, kTearsEnd, &blocks);
    INFO(blocks << " marked tear blocks removed");
    REQUIRE(blocks >= 4);
    // Nothing of the tears' code is left behind (comments may still mention them).
    for (const char* name : {"waterTear", "tearAt", "water.tears", "tearShape", "tearFrame"}) {
        INFO(name);
        REQUIRE(stripped.find(name) == std::string::npos);
    }
    const fs::path strippedDir = writeWaterShaderVariant("no-tears", stripped);

    WaterBench bench = WaterBench::make(256, 176);
    WaterBench::Arm live = bench.arm();
    WaterBench::Arm none = bench.arm({strippedDir});

    struct Pose {
        glm::vec3 eye, target;
    };
    const std::array<Pose, 2> poses{{{{40.0f, 6.0f, -86.0f}, {12.0f, -1.0f, -108.0f}},
                                     {{20.0f, 34.0f, -96.0f}, {14.0f, 0.0f, -110.0f}}}};
    const auto offDefault = [](scene::WaterSettings& w, float tears) {
        w.tears = tears;
        w.tearShear = 5.0f;
        w.tearCoverage = 0.9f;
        w.tearCell = 2.0f;
        w.tearSpacing = 17.0f;
        w.tearStretch = 1.7f;
        w.tearFollowsWind = false;
        w.tearAngle = 1.1f;
        w.tearDrift = -0.3f;
        w.tearWind = 0.9f;
    };
    std::size_t identical = 0;
    std::size_t tornDiffers = 0;
    std::size_t cases = 0;
    for (const Pose& pose : poses) {
        bench.aim(pose.eye, pose.target);
        for (const double seconds : {3.0, 47.5, 181.0}) {
            bench.hold(seconds);
            offDefault(bench.scene().waters.at(0).settings, 0.0f);
            const gpu::ImageF withCode = bench.render(live);
            const gpu::ImageF withoutCode = bench.render(none);
            const std::size_t water = countMask(waterMask(withCode, bench.renderDry(live)));
            const std::size_t waterNone = countMask(waterMask(withoutCode, bench.renderDry(none)));
            INFO(fmt::format("pose ({:.0f},{:.0f},{:.0f}) at {} s: {} and {} water pixels", pose.eye.x,
                             pose.eye.y, pose.eye.z, seconds, water, waterNone));
            REQUIRE(water > 500);
            REQUIRE(waterNone > 500);
            const bool same = gpu::hashImage(withCode) == gpu::hashImage(withoutCode);
            identical += same ? 1 : 0;
            CHECK(same);
            // THE CONTROL: the same settings with the tears on do reach the live shader, so the
            // equality above is not two renders that could never have differed.
            offDefault(bench.scene().waters.at(0).settings, 0.5f);
            const bool torn = gpu::hashImage(bench.render(live)) != gpu::hashImage(withoutCode);
            tornDiffers += torn ? 1 : 0;
            ++cases;
        }
    }
    INFO(identical << " of " << cases << " identical at 0; " << tornDiffers << " differ with the tears on");
    CHECK(tornDiffers == cases);
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- they exist, are thin, carry stripes, and turn with their direction ---------------------------
TEST_CASE("tears are thin seams of parallel stripes that turn with their direction",
          "[gpu][renderer][water][tears]") {
    Synthetic bench = Synthetic::make(384);
    const std::uint32_t n = bench.size;

    struct Seams {
        std::vector<char> band;
        std::vector<Component> parts;
        std::vector<float> lum;
    };
    // The seams' own pixels: where changing only the band's slope changes the picture. Both amounts
    // are past the first 0.25 over which the shear comes in, so the shift is the same in both renders
    // and everything outside the bands subtracts out exactly.
    const auto seamsAt = [&](float angle) {
        scene::WaterSettings strong = tearSettings();
        strong.tearAngle = angle;
        scene::WaterSettings faint = strong;
        faint.tears = 0.3f;
        Seams s;
        const gpu::ImageF strongImage = bench.render(strong);
        const gpu::ImageF faintImage = bench.render(faint);
        dumpWater(strongImage, fmt::format("seams-{:.2f}-strong", angle), 3.0f);
        dumpWater(faintImage, fmt::format("seams-{:.2f}-faint", angle), 3.0f);
        s.lum = luminance(strongImage);
        s.band = differs(s.lum, luminance(faintImage), 1e-4f);
        s.parts = components(s.band, n, n);
        return s;
    };

    const Seams a = seamsAt(0.35f);
    const double share = static_cast<double>(countMask(a.band)) / static_cast<double>(n * n);
    std::size_t bigPixels = 0;
    double slenderWeighted = 0.0;
    for (const Component& c : a.parts) {
        if (c.pixels >= 60) {
            bigPixels += c.pixels;
            slenderWeighted += c.slenderness() * static_cast<double>(c.pixels);
        }
    }
    const double meanAspect = bigPixels > 0 ? slenderWeighted / static_cast<double>(bigPixels) : 0.0;
    // The same pixels with no tears at all: the control for every claim about what is inside the bands.
    scene::WaterSettings untorn = tearSettings();
    untorn.tears = 0.0f;
    const std::vector<float> plain = luminance(bench.render(untorn));
    const double aligned = stripeAlignment(a.lum, a.band, n, n);
    const double unrelated = stripeAlignment(plain, a.band, n, n);
    const double cohBand = meanOver(coherence(a.lum, n, n, 2), a.band, true);
    const double cohPlain = meanOver(coherence(plain, n, n, 2), a.band, true);
    INFO(fmt::format("seams cover {:.1f}% of the water in {} components ({} px in parts of 60+), "
                     "size-weighted length over width {:.1f}; stripes' alignment with their band {:.2f} (the same "
                     "pixels untorn {:.2f}); 5x5 coherence in the bands {:.2f} (untorn {:.2f})",
                     share * 100.0, a.parts.size(), bigPixels, meanAspect, aligned, unrelated, cohBand, cohPlain));
    // Present, and not the whole surface: the brief's "do not make the entire water uniformly busy".
    CHECK(share > 0.02);
    CHECK(share < 0.25);
    // Thin: long pieces, not blobs -- the audit's "aspect above 6", as length over mean width.
    REQUIRE(bigPixels > 0);
    CHECK(meanAspect > 6.0);
    // Stripes that run along the band: the image's dominant gradient inside the bands lines up with the
    // band's own (it points across the band), where the same pixels untorn have little to do with it
    // (0.60 against 0.15 when this was written).
    CHECK(aligned > unrelated + 0.2);
    // The audit also proposed "the structure tensor is coherent (above 0.6)" in the bands. That is not a
    // property of these seams and is not asserted: the compressed ripples are thin streaks with ends and
    // junctions, less coherent in a 5x5 window than the smooth ripples they came from (0.58 against 0.67
    // when this was written, 0.45 against 0.54 in 9x9) while strongly aligned with the band. Gradient
    // energy across the band over along it said little more (x1.77 against x1.24 untorn): in HDR
    // luminance it is carried by the moon's glints, which are isotropic.

    // THE CONTROL, and the alignment claim: turned 60 degrees, the seams' axis turns with them.
    const Seams b = seamsAt(0.35f + static_cast<float>(std::numbers::pi / 3.0));
    const double turned = axisDifferenceDegrees(meanOrientation(a.parts, 60), meanOrientation(b.parts, 60));
    INFO(fmt::format("mean seam axis turned by {:.1f} degrees for a 60 degree turn", turned));
    CHECK(turned > 48.0);
    CHECK(turned < 72.0);
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- nowhere past the seams ---------------------------------------------------------------------------
//
// The brief: "do not make the entire water surface uniformly busy". A seam moves the ripples only inside
// its band: the shift goes out and comes back across the band (a tent in S), so it is exactly zero on
// both sides, and outside every band the surface is the surface with no tears. Measured as the share of
// the water outside the bands (dilated three pixels past their soft edges) that differs from the untorn
// render by more than 1% of its mean. THE CONTROL is the same shader with the tent replaced by a
// one-sided step -- the shift left in place past the band -- under which the water on the far side of
// every seam moves too. (A step relaxed back to zero over the surrounding metres was the design before
// the tent; value noise sits near zero, so its relaxation reached most of the surface.)
TEST_CASE("outside its seams the water is the water with no tears", "[gpu][renderer][water][tears]") {
    const scene::WaterSettings strong = tearSettings();
    scene::WaterSettings faint = strong;
    faint.tears = 0.3f;
    scene::WaterSettings untorn = strong;
    untorn.tears = 0.0f;

    Synthetic live = Synthetic::make(256);
    const std::uint32_t n = live.size;
    const std::vector<float> plain = luminance(live.render(untorn));
    const std::vector<float> torn = luminance(live.render(strong));
    const std::vector<char> seams = differs(torn, luminance(live.render(faint)), 1e-4f);
    std::vector<char> near(seams.size(), 0);
    for (int y = 0; y < static_cast<int>(n); ++y) {
        for (int x = 0; x < static_cast<int>(n); ++x) {
            if (seams[static_cast<std::size_t>(y) * n + static_cast<std::size_t>(x)] == 0) {
                continue;
            }
            for (int dy = -3; dy <= 3; ++dy) {
                for (int dx = -3; dx <= 3; ++dx) {
                    const int u = x + dx, v = y + dy;
                    if (u >= 0 && v >= 0 && u < static_cast<int>(n) && v < static_cast<int>(n)) {
                        near[static_cast<std::size_t>(v) * n + static_cast<std::size_t>(u)] = 1;
                    }
                }
            }
        }
    }
    double meanPlain = 0.0;
    std::size_t outside = 0;
    for (std::size_t i = 0; i < plain.size(); ++i) {
        if (near[i] == 0) {
            meanPlain += plain[i];
            ++outside;
        }
    }
    REQUIRE(outside > plain.size() / 2);
    meanPlain /= static_cast<double>(outside);
    // The share of the water outside the seams that differs from the untorn render by more than
    // `fraction` of its mean luminance.
    const auto changedOutside = [&](const std::vector<float>& lum, double fraction) {
        std::size_t changed = 0;
        for (std::size_t i = 0; i < lum.size(); ++i) {
            if (near[i] == 0 && std::fabs(lum[i] - plain[i]) > fraction * meanPlain) {
                ++changed;
            }
        }
        return static_cast<double>(changed) / static_cast<double>(outside);
    };

    const fs::path stepDir = writeWaterShaderVariant(
        "tears-one-sided", replaceOnce(readWaterShaderSource(), "(1.0 - abs(2.0 * s - 1.0))", "s"));
    Synthetic stepped = Synthetic::make(256, {stepDir});
    const std::vector<float> step = luminance(stepped.render(strong));
    INFO(fmt::format("outside the seams ({:.1f}% of the view), the share of the water that differs from the untorn "
                     "render by more than 0.01% / 0.1% / 1% of its mean: {:.2f}% / {:.2f}% / {:.2f}%; with a one-sided "
                     "step {:.1f}% / {:.1f}% / {:.1f}%",
                     100.0 * static_cast<double>(outside) / static_cast<double>(plain.size()),
                     100.0 * changedOutside(torn, 1e-4), 100.0 * changedOutside(torn, 1e-3),
                     100.0 * changedOutside(torn, 1e-2), 100.0 * changedOutside(step, 1e-4),
                     100.0 * changedOutside(step, 1e-3), 100.0 * changedOutside(step, 1e-2)));
    // A shift of the ripples moves the reflection only slightly where it is of a smooth stretch of sky,
    // so the check counts any change above a thousandth of the mean: everything a shift touches, and
    // none of what a single pixel's rounding does.
    CHECK(changedOutside(torn, 1e-3) < 0.01);
    CHECK(changedOutside(step, 1e-3) > 0.10); // THE CONTROL
    CHECK(live.ctx->errorCount() == 0);
    CHECK(stepped.ctx->errorCount() == 0);
}

// ---- every setting reaches the picture, and none of them does at 0 ---------------------------------
//
// ADR-916 adds nine settings, and this repository's recurring defect is a setting that binds and does
// nothing. Each one, changed from a working baseline, must change the image; and the same change with
// the tears at 0 must not, which is the gate every scene without tears relies on.
TEST_CASE("every tear setting reaches the picture, and none of them does at tears 0",
          "[gpu][renderer][water][tears][parameters]") {
    Synthetic bench = Synthetic::make(256);
    const auto windOn = [](scene::Scene& s) {
        s.environment.wind.enabled = true;
        s.environment.wind.speed = 0.8f;
        s.environment.wind.direction = 2.0f;
        s.environment.wind.gustAmount = 1.0f;
        s.environment.wind.gustScale = 30.0f;
        s.environment.wind.gustSpeed = 6.0f;
    };
    struct Change {
        const char* what;
        std::function<void(scene::WaterSettings&)> apply;
        bool wind = false;
    };
    const std::vector<Change> changes{
        {"tears", [](scene::WaterSettings& w) { w.tears = 1.0f; }},
        {"tearShear", [](scene::WaterSettings& w) { w.tearShear = 3.5f; }},
        {"tearCoverage", [](scene::WaterSettings& w) { w.tearCoverage = 0.5f; }},
        {"tearCell", [](scene::WaterSettings& w) { w.tearCell = 2.0f; }},
        {"tearSpacing", [](scene::WaterSettings& w) { w.tearSpacing = 14.0f; }},
        {"tearStretch", [](scene::WaterSettings& w) { w.tearStretch = 1.5f; }},
        {"tearDirection (angle)", [](scene::WaterSettings& w) { w.tearAngle = 1.2f; }},
        {"tearDirection (wind)", [](scene::WaterSettings& w) { w.tearFollowsWind = true; }, true},
        {"tearDrift", [](scene::WaterSettings& w) { w.tearDrift = 0.5f; }},
        {"tearWind", [](scene::WaterSettings& w) { w.tearWind = 1.0f; }, true},
    };
    for (const Change& change : changes) {
        INFO(change.what);
        const std::function<void(scene::Scene&)> edit =
            change.wind ? std::function<void(scene::Scene&)>(windOn) : std::function<void(scene::Scene&)>();
        scene::WaterSettings base = tearSettings();
        scene::WaterSettings moved = base;
        change.apply(moved);
        const bool reaches = gpu::hashImage(bench.render(base, 10.0, edit)) !=
                             gpu::hashImage(bench.render(moved, 10.0, edit));
        CHECK(reaches);
        // The gate: the same change with the amount at 0. (For `tears` itself that is 0 against 0.)
        base.tears = 0.0f;
        moved.tears = 0.0f;
        const bool gated = gpu::hashImage(bench.render(base, 10.0, edit)) ==
                           gpu::hashImage(bench.render(moved, 10.0, edit));
        CHECK(gated);
    }
    // And with no wind in the scene, the coupling has nothing to read: a scene without wind keeps its
    // seams as authored (ADR-916).
    scene::WaterSettings coupled = tearSettings();
    coupled.tearWind = 1.0f;
    const bool windless = gpu::hashImage(bench.render(tearSettings(), 10.0)) == gpu::hashImage(bench.render(coupled, 10.0));
    CHECK(windless);
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- every control, through its parameter path ------------------------------------------------------
//
// All ten tear settings are parameters under `nodes/<terrain>/water/tears/` -- the heading both panels
// show them under (UI reach) -- three of them routable. Proven on the QA scene through the Engine, the
// path a panel edit, a project's saved value and a route all take: parameter -> updateWaterSurfaces ->
// the uniform -> the picture. The gate first: with the amount at 0 none of the other nine moves a
// pixel, which is the byte-identity a scene without tears relies on, reached through the parameters.
TEST_CASE("every tear control reaches the picture through its parameter, and none does at amount 0",
          "[gpu][renderer][water][tears][parameters]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(256, 176);
    loadSeamScene(bench, 0.0f);
    // A blowing wind from a direction of its own, so "follow the wind" turns the seams and "wind" has a
    // strength and gusts to couple to.
    {
        auto& params = bench.engine->params();
        for (const auto& [path, value] : {std::pair{"scene/wind/enabled", 1.0f}, std::pair{"scene/windSpeed", 0.8f},
                                          std::pair{"scene/windDirection", 2.0f}, std::pair{"scene/wind/gustAmount", 1.0f}}) {
            params::IParameter* p = params.find(path);
            INFO(path);
            REQUIRE(p != nullptr);
            p->setBaseComponent(0, value);
        }
        params.resetFinals();
    }
    bench.aim({20.0f, 34.0f, -96.0f}, {14.0f, 0.0f, -110.0f});
    WaterBench::Arm live = bench.arm();

    const auto frame = [&]() {
        bench.hold(8.0);
        return gpu::hashImage(bench.render(live));
    };
    const auto set = [&](const std::string& leaf, float value) {
        params::IParameter* p = bench.engine->params().find("nodes/ground/water/tears/" + leaf);
        INFO(leaf);
        REQUIRE(p != nullptr);
        const float was = p->baseComponent(0);
        p->setBaseComponent(0, value);
        bench.engine->params().resetFinals();
        return was;
    };
    struct Move {
        const char* leaf;
        float value;
    };
    // Each away from what qaSceneWithSeams authored (shear 2.5, coverage 0.9, cell 1.25, spacing 10,
    // stretch 3, a fixed direction of 0.4, drift 0.15, wind 0.6).
    const std::vector<Move> moves{{"shear", 5.0f},      {"coverage", 0.4f},  {"cell", 2.0f},
                                  {"spacing", 14.0f},   {"stretch", 1.5f},   {"followWind", 1.0f},
                                  {"direction", 1.2f},  {"drift", 0.5f},     {"wind", 1.0f}};

    const std::uint64_t off = frame();
    for (const Move& move : moves) {
        INFO("at amount 0: " << move.leaf);
        const float was = set(move.leaf, move.value);
        CHECK(frame() == off);
        set(move.leaf, was);
    }

    set("amount", 0.6f);
    const std::uint64_t on = frame();
    CHECK(on != off);
    for (const Move& move : moves) {
        INFO("at amount 0.6: " << move.leaf);
        const float was = set(move.leaf, move.value);
        CHECK(frame() != on);
        set(move.leaf, was);
        CHECK(frame() == on); // and back: the same picture, so nothing was left behind
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- a seek lands on the frame a play reached --------------------------------------------------------
//
// The tears are a pure function of the timeline second: the lattice drifts rigidly with `t`, the wind
// is sampled at `t`, and nothing accumulates. So the pattern of `forensics :788` holds with them on and
// the wind blowing: a second reached by a seek, by a detour through two others, and by a fresh engine
// is the same picture to the bit.
TEST_CASE("tears reached by a seek, a detour and a fresh engine are the same picture",
          "[gpu][renderer][water][tears][determinism]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    const auto prepare = [](WaterBench& bench) {
        loadSeamScene(bench, 0.7f);
        auto& params = bench.engine->params();
        auto* enabled = params.findAs<bool>("scene/wind/enabled");
        auto* speed = params.findAs<float>("scene/windSpeed");
        REQUIRE(enabled != nullptr);
        REQUIRE(speed != nullptr);
        enabled->setBase(true);
        speed->setBase(0.8f);
        params.resetFinals();
    };
    const auto seamsAt = [](WaterBench& bench, WaterBench::Arm& arm, std::initializer_list<double> route) {
        for (const double seconds : route) {
            bench.engine->seekSeconds(seconds);
            FixedStepClock clock(60.0);
            clock.restartAt(seconds);
            bench.time = bench.engine->tick(clock);
            bench.engine->setViewport(bench.width, bench.height);
            bench.engine->update(bench.time);
        }
        return gpu::hashImage(bench.render(arm));
    };

    WaterBench bench = WaterBench::make(256, 176);
    prepare(bench);
    WaterBench::Arm live = bench.arm();
    const std::uint64_t direct = seamsAt(bench, live, {37.0});
    const std::uint64_t detour = seamsAt(bench, live, {5.5, 121.25, 37.0});
    CHECK(detour == direct);

    WaterBench fresh = WaterBench::make(256, 176);
    prepare(fresh);
    WaterBench::Arm freshArm = fresh.arm();
    CHECK(seamsAt(fresh, freshArm, {37.0}) == direct);

    // Live: another second is another picture, so the equalities are not two blank frames agreeing.
    CHECK(seamsAt(bench, live, {37.5}) != direct);
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- a preview and a final fade them the same --------------------------------------------------------
//
// The band's fade and the compressed layers' fade count reference pixels (ADR-915), so a 2160-row frame
// fades the seams where a 1080-row one does. Measured inside the seams only (both amounts past the
// shear's ramp, so the band is where they differ), on a grazing view where the seams run from near the
// lens to the far distance. As in test_water_lod_gpu.cpp the near half is the calibration: there nothing
// fades at either resolution, so its ratio is what the box-average itself does, and the far half is
// compared relative to it. The control is the shader counting its own pixels.
namespace {

struct BandRatios {
    double nearRatio = 0.0;
    double farRelative = 0.0;
    std::size_t nearPixels = 0;
    std::size_t farPixels = 0;
    // Where the band's own slope is still drawn: its pixels in the far half at each resolution, on the
    // 1080-row grid. The band fade counts reference pixels, so these should agree.
    std::size_t farBandLow = 0;
    std::size_t farBandHigh = 0;
};

BandRatios bandRatios(const std::vector<fs::path>& dirs) {
    auto ctx = makeWaterContext();
    std::vector<fs::path> search = dirs;
    search.emplace_back(AVGEN_SHADER_SOURCE_DIR);
    gpu::ShaderLibrary shaders(*ctx, search);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setPassToggles(waterQuantitativeToggles());
    scene::WaterSettings strong = tearSettings();
    strong.tearCell = 1.0f;
    strong.tearSpacing = 6.0f;
    scene::WaterSettings faint = strong;
    faint.tears = 0.3f;
    const auto render = [&](std::uint32_t size, const scene::WaterSettings& w) {
        scene::Scene s = tearScene(w);
        s.camera.position = {0.0f, 3.0f, 30.0f};
        s.camera.target = {0.0f, 0.0f, 0.0f};
        FrameTime time{};
        time.renderTime = 10.0;
        renderer.resetTemporalHistory();
        auto image = renderer.renderToImageFloat(s, time, size, size);
        REQUIRE(image.has_value());
        return luminance(*image);
    };
    const std::vector<float> lowStrong = render(1080, strong);
    const std::vector<char> lowBand = differs(lowStrong, render(1080, faint), 1e-4f);
    const std::vector<float> highStrong = render(2160, strong);
    const std::vector<char> highBand = maskDown2(differs(highStrong, render(2160, faint), 1e-4f), 2160, 2160);
    std::vector<char> band(lowBand.size());
    std::uint32_t top = 1080, bottom = 0;
    for (std::size_t i = 0; i < band.size(); ++i) {
        band[i] = (lowBand[i] != 0 || highBand[i] != 0) ? 1 : 0;
        if (band[i] != 0) {
            top = std::min(top, static_cast<std::uint32_t>(i / 1080));
            bottom = std::max(bottom, static_cast<std::uint32_t>(i / 1080));
        }
    }
    REQUIRE(bottom > top + 40);
    const std::uint32_t middle = (top + bottom) / 2;
    const std::vector<float> highDown = boxDown2(highStrong, 2160, 2160);
    const Fine nearLow = fineStructure(lowStrong, band, 1080, 1080, middle, bottom + 1);
    const Fine nearHigh = fineStructure(highDown, band, 1080, 1080, middle, bottom + 1);
    const Fine farLow = fineStructure(lowStrong, band, 1080, 1080, top, middle);
    const Fine farHigh = fineStructure(highDown, band, 1080, 1080, top, middle);
    BandRatios r;
    for (std::uint32_t y = top; y < middle; ++y) {
        for (std::uint32_t x = 0; x < 1080; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * 1080 + x;
            r.farBandLow += lowBand[i] != 0 ? 1 : 0;
            r.farBandHigh += highBand[i] != 0 ? 1 : 0;
        }
    }
    r.nearRatio = nearHigh.energy / nearLow.energy;
    r.farRelative = (farHigh.energy / farLow.energy) / r.nearRatio;
    r.nearPixels = nearLow.pixels;
    r.farPixels = farLow.pixels;
    CHECK(ctx->errorCount() == 0);
    return r;
}

} // namespace

TEST_CASE("tears fade the same at a preview's resolution and at twice it", "[gpu][renderer][water][tears][lod]") {
    const BandRatios now = bandRatios({});
    const fs::path ownDir = writeWaterShaderVariant(
        "tears-own-pixels", replaceOnce(readWaterShaderSource(), "const kWaterReferenceRows: f32 = 1080.0;",
                                        "const kWaterReferenceRows: f32 = 1.0e9;"));
    const BandRatios own = bandRatios({ownDir});
    INFO(fmt::format("seam fine structure, 2160 over 1080 rows: near half x{:.3f} ({} px), far half relative to "
                     "near x{:.3f} ({} px); counting its own pixels: near x{:.3f}, far relative x{:.3f}. Far-half band "
                     "pixels 1080 / 2160: {} / {}; counting its own pixels {} / {}",
                     now.nearRatio, now.nearPixels, now.farRelative, now.farPixels, own.nearRatio, own.farRelative,
                     now.farBandLow, now.farBandHigh, own.farBandLow, own.farBandHigh));
    REQUIRE(now.nearPixels > 2000);
    REQUIRE(now.farPixels > 2000);
    // Counting reference pixels, the two agree -- within +-30% rather than the whole surface's +-15%
    // (test_water_lod_gpu.cpp), because a seam packs its stripes right at the fade limit, where a
    // point-sampled 1080-row frame aliases and a box-averaged 2160-row one does not; the near-half
    // calibration cannot see that, since near the lens the stripes are far above the limit.
    const auto agrees = [](double ratio) { return ratio > 1.0 / 1.3 && ratio < 1.3; };
    CHECK(agrees(now.farRelative));
    // THE CONTROL: the shader counting its own pixels fails the same check (x0.55 when this was written:
    // on the low side, where before ADR-916's fold it failed on the high side, x5.1).
    CHECK_FALSE(agrees(own.farRelative));
    // And the band itself: its slope fades out at the same distance in both frames, so the far half holds
    // about as many band pixels at 2160 rows as at 1080 (6145 and 5310 when this was written); counting
    // its own pixels a final keeps bands a preview has faded (12007).
    const double bandsNow = static_cast<double>(now.farBandHigh) / static_cast<double>(std::max<std::size_t>(now.farBandLow, 1));
    const double bandsOwn = static_cast<double>(own.farBandHigh) / static_cast<double>(std::max<std::size_t>(own.farBandLow, 1));
    INFO(fmt::format("far band pixels at 2160 over 1080 rows: x{:.2f}; counting its own pixels x{:.2f}", bandsNow, bandsOwn));
    REQUIRE(now.farBandLow > 1000);
    CHECK(agrees(bandsNow));
    CHECK(bandsOwn > 1.5); // THE CONTROL
}

// ---- the permutation's source ------------------------------------------------------------------------
//
// No device needed: the function the renderer compiles its tearless pipeline from, checked against this
// file's own, independent strip, and on the two inputs it must not misread.
TEST_CASE("the tearless pipeline's source is the shader with every tear block removed",
          "[gpu][renderer][water][tears][identity]") {
    const std::string live = readWaterShaderSource();
    const auto plain = rendering::waterSourceWithoutTears(live);
    REQUIRE(plain.has_value());
    CHECK(*plain == stripMarkedBlocks(live, kTearsBegin, kTearsEnd));
    CHECK(plain->size() < live.size());
    // Source with no tears in it comes back unchanged -- a variant of the shader, or a future one.
    const auto again = rendering::waterSourceWithoutTears(*plain);
    REQUIRE(again.has_value());
    CHECK(*again == *plain);
    // And a block that never closes is refused rather than swallowing the rest of the file.
    const auto broken = rendering::waterSourceWithoutTears(std::string("a\n// ") + kTearsBegin + "\nb\n");
    CHECK_FALSE(broken.has_value());
}
