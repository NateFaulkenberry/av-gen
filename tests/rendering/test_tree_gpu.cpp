// The contact sheet (brief section 33), and the hero frame.
//
// This is the instrument the whole aesthetic evaluator has been waiting for. Every number the
// candidate search produces is structural -- projected areas, branch counts, silhouette moments --
// and none of it is evidence about whether the result is beautiful. A grid of thumbnails at one
// camera and one light rig, each labelled with its index and its score, is what makes the score
// arguable: if the top-ranked tree is not the best-looking one in the sheet, the bands are wrong
// and the sheet says which way.
//
// It writes files rather than asserting on pixels. A test that asserted "this looks good" would be
// a test that cannot fail for the right reason; what is asserted here is that every candidate
// renders, that the frames are not blank, and that two runs of the same candidate are identical.

#include "assets/image.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/tree_generator.hpp"
#include "scene/tree_scene.hpp"
#include "search/candidate_search.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <array>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// A 3x5 bitmap font, digits and the few marks a label needs. Five glyph rows packed as three bits
// each. `DebugDraw` has no text facility and the compositor's text layer is a whole authored 2D
// system; a contact sheet whose cells are unlabelled is a contact sheet nobody can act on, and this
// is forty lines.
std::uint8_t glyphRow(char c, int row) {
    static const std::vector<std::pair<char, std::array<std::uint8_t, 5>>> kFont = {
        {'0', {0b111, 0b101, 0b101, 0b101, 0b111}}, {'1', {0b010, 0b110, 0b010, 0b010, 0b111}},
        {'2', {0b111, 0b001, 0b111, 0b100, 0b111}}, {'3', {0b111, 0b001, 0b011, 0b001, 0b111}},
        {'4', {0b101, 0b101, 0b111, 0b001, 0b001}}, {'5', {0b111, 0b100, 0b111, 0b001, 0b111}},
        {'6', {0b111, 0b100, 0b111, 0b101, 0b111}}, {'7', {0b111, 0b001, 0b010, 0b010, 0b010}},
        {'8', {0b111, 0b101, 0b111, 0b101, 0b111}}, {'9', {0b111, 0b101, 0b111, 0b001, 0b111}},
        {'.', {0b000, 0b000, 0b000, 0b000, 0b010}}, {'#', {0b101, 0b111, 0b101, 0b111, 0b101}},
        {' ', {0b000, 0b000, 0b000, 0b000, 0b000}},
    };
    for (const auto& [key, rows] : kFont) {
        if (key == c) {
            return rows[static_cast<std::size_t>(row)];
        }
    }
    return 0;
}

void drawText(gpu::Image8& image, int x, int y, const std::string& text, int scale) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int gx = x + static_cast<int>(i) * 4 * scale;
        for (int row = 0; row < 5; ++row) {
            const std::uint8_t bits = glyphRow(text[i], row);
            for (int col = 0; col < 3; ++col) {
                if ((bits & (1u << (2 - col))) == 0) {
                    continue;
                }
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        const int px = gx + col * scale + sx;
                        const int py = y + row * scale + sy;
                        if (px < 0 || py < 0 || px >= static_cast<int>(image.width) ||
                            py >= static_cast<int>(image.height)) {
                            continue;
                        }
                        const std::size_t o = (static_cast<std::size_t>(py) * image.width + px) * 4;
                        image.rgba[o] = 255;
                        image.rgba[o + 1] = 255;
                        image.rgba[o + 2] = 255;
                        image.rgba[o + 3] = 255;
                    }
                }
            }
        }
    }
}

void blit(gpu::Image8& dst, const gpu::Image8& src, int x, int y) {
    for (std::uint32_t sy = 0; sy < src.height; ++sy) {
        const int dy = y + static_cast<int>(sy);
        if (dy < 0 || dy >= static_cast<int>(dst.height)) {
            continue;
        }
        for (std::uint32_t sx = 0; sx < src.width; ++sx) {
            const int dx = x + static_cast<int>(sx);
            if (dx < 0 || dx >= static_cast<int>(dst.width)) {
                continue;
            }
            const std::size_t so = (static_cast<std::size_t>(sy) * src.width + sx) * 4;
            const std::size_t dofs = (static_cast<std::size_t>(dy) * dst.width + dx) * 4;
            for (int c = 0; c < 4; ++c) {
                dst.rgba[dofs + c] = src.rgba[so + c];
            }
        }
    }
}

// Mean luminance, and the fraction of pixels brighter than this frame's own background. A frame
// that renders but shows nothing is the failure this catches, and it is the one a "did it return a
// value" check misses.
//
// The background is SAMPLED, not assumed. A fixed luminance threshold counted the sky -- which is
// legitimately lit in a night scene with a sky gradient -- and reported three quarters of every
// frame as subject, which made the bound meaningless in both directions.
struct FrameStats {
    double meanLuma = 0.0;
    double subjectFraction = 0.0;
    double background = 0.0;
};

double lumaAt(const gpu::Image8& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t o = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return 0.2126 * image.rgba[o] + 0.7152 * image.rgba[o + 1] + 0.0722 * image.rgba[o + 2];
}

FrameStats frameStats(const gpu::Image8& image) {
    FrameStats out;
    // The brightest of the two top corners: the sky gradient is brightest near the horizon, so a
    // single corner would under-read it and count the upper sky as subject.
    out.background = std::max(lumaAt(image, 2, 2), lumaAt(image, image.width - 3, 2));
    const double threshold = out.background + 14.0;
    double sum = 0.0;
    std::size_t subject = 0;
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t i = 0; i < pixels; ++i) {
        const double l = 0.2126 * image.rgba[i * 4] + 0.7152 * image.rgba[i * 4 + 1] + 0.0722 * image.rgba[i * 4 + 2];
        sum += l;
        if (l > threshold) {
            ++subject;
        }
    }
    out.meanLuma = sum / static_cast<double>(pixels);
    out.subjectFraction = static_cast<double>(subject) / static_cast<double>(pixels);
    return out;
}

fs::path outputDir() {
    const fs::path dir = fs::temp_directory_path() / "avgen_tree";
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("A generated tree renders, and renders the same way twice", "[gpu][tree]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::TreeGenerator generator;
    const search::Parameters params = search::sampleAt(generator.schema().parameters, 11);
    const auto treeParams = scene::treeParamsFrom(params);
    REQUIRE(treeParams.has_value());
    const auto sceneResult = scene::buildTreeScene(*treeParams, generator.camera());
    REQUIRE(sceneResult.has_value());

    const FrameTime time{0.0, 0.0, 0};
    const auto first = renderer.renderToImage(*sceneResult, time, 960, 540);
    REQUIRE(first.has_value());
    const auto second = renderer.renderToImage(*sceneResult, time, 960, 540);
    REQUIRE(second.has_value());

    const FrameStats stats = frameStats(*first);
    INFO(fmt::format("mean luma {:.2f}, lit fraction {:.3f}", stats.meanLuma, stats.subjectFraction));
    // Not blank, and not a white-out. Both are ways for the pipeline to "succeed" while showing
    // nothing, and neither is caught by checking that render returned a value.
    CHECK(stats.subjectFraction > 0.04);
    CHECK(stats.subjectFraction < 0.60);
    CHECK(stats.meanLuma > 1.0);

    // The same scene, the same time, the same renderer: identical pixels. This is the offline
    // determinism claim at the only level it can actually be checked.
    CHECK(gpu::hashImage(*first) == gpu::hashImage(*second));

    const fs::path out = outputDir() / "tree-hero.png";
    REQUIRE(assets::writePng(out, first->width, first->height, first->rgba).has_value());
    WARN("hero frame written to " << out.string());
}

TEST_CASE("The candidate contact sheet renders", "[gpu][tree][contact]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::TreeGenerator generator;
    search::SearchSettings settings;
    settings.population = 24;
    settings.select = 12;
    const auto search = search::runSearch(generator, settings);
    REQUIRE(search.has_value());
    REQUIRE_FALSE(search->selected.empty());

    // Identical camera and lighting for every cell, which is the whole point: a thumbnail grid
    // where the shots differ is a grid that compares nothing.
    // The cell keeps the EVALUATION camera's aspect, not a nicer portrait one. A contact sheet whose
    // job is to say whether the score agrees with the eye has to show the frame the score was
    // computed on; a portrait cell with the same camera simply crops, which is what the first sheet
    // did and why every tree in it was cut off at the top.
    constexpr std::uint32_t kCell = 448;
    constexpr std::uint32_t kCellH = 252;
    constexpr int kCols = 3;
    const auto count = static_cast<int>(search->selected.size());
    const int rows = (count + kCols - 1) / kCols;

    gpu::Image8 sheet;
    sheet.width = kCell * kCols;
    sheet.height = kCellH * static_cast<std::uint32_t>(rows);
    sheet.rgba.assign(static_cast<std::size_t>(sheet.width) * sheet.height * 4, 0);
    for (std::size_t i = 3; i < sheet.rgba.size(); i += 4) {
        sheet.rgba[i] = 255;
    }

    // A portrait cell: the tree is taller than it is wide, and a 16:9 thumbnail of it is mostly sky.
    scene::TreeCameraView cell = generator.camera();
    cell.width = static_cast<int>(kCell);
    cell.height = static_cast<int>(kCellH);

    int rendered = 0;
    std::string manifest;
    for (int i = 0; i < count; ++i) {
        const search::Candidate& candidate = search->candidates[search->selected[static_cast<std::size_t>(i)]];
        const auto treeParams = scene::treeParamsFrom(candidate.parameters);
        if (!treeParams) {
            continue;
        }
        const auto built = scene::buildTreeScene(*treeParams, cell);
        REQUIRE(built.has_value());
        const auto image = renderer.renderToImage(*built, FrameTime{0.0, 0.0, 0}, kCell, kCellH);
        REQUIRE(image.has_value());
        const FrameStats stats = frameStats(*image);
        // Every cell must show a tree. A blank cell in a contact sheet is worse than a missing one:
        // it reads as "this candidate is empty" when it may mean "this candidate did not render".
        INFO("candidate " << candidate.index << " subject " << stats.subjectFraction);
        CHECK(stats.subjectFraction > 0.03);

        const int col = i % kCols;
        const int row = i / kCols;
        blit(sheet, *image, col * static_cast<int>(kCell), row * static_cast<int>(kCellH));
        drawText(sheet, col * static_cast<int>(kCell) + 6, row * static_cast<int>(kCellH) + 6,
                 fmt::format("#{}", candidate.index), 2);
        drawText(sheet, col * static_cast<int>(kCell) + 6, row * static_cast<int>(kCellH) + 22,
                 fmt::format("{:.3f}", candidate.score.overall()), 2);
        manifest += fmt::format("cell {:2} (row {} col {}): #{} score {:.3f}", i, row, col, candidate.index,
                                candidate.score.overall());
        for (const search::ScoreComponent& c : candidate.score.components) {
            manifest += fmt::format(" {}={:.3f}({:.2f})", c.name, c.raw, c.score);
        }
        manifest += "\n";
        ++rendered;
    }
    CHECK(rendered == count);

    const fs::path sheetPath = outputDir() / "tree-contact-sheet.png";
    REQUIRE(assets::writePng(sheetPath, sheet.width, sheet.height, sheet.rgba).has_value());
    const fs::path manifestPath = outputDir() / "tree-contact-sheet.txt";
    {
        std::ofstream file(manifestPath);
        file << manifest;
    }
    WARN("contact sheet written to " << sheetPath.string() << " (" << rendered << " cells)");
    WARN("scores written to " << manifestPath.string());
}

TEST_CASE("The tree moves, and stays in one piece while it does", "[gpu][tree][animation]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::TreeGenerator generator;
    const search::Parameters params = search::sampleAt(generator.schema().parameters, 11);
    const auto treeParams = scene::treeParamsFrom(params);
    REQUIRE(treeParams.has_value());
    auto built = scene::buildAnimatedTree(*treeParams, generator.camera());
    REQUIRE(built.has_value());
    WARN(fmt::format("{} joints, {} triangles", built->rig.size(), built->triangles));

    scene::TreeAnimator animator;
    scene::TreeMotionInputs inputs;
    inputs.windSpeed = 0.8f;
    inputs.gust = 0.35f;
    inputs.flutter = 0.5f;
    // Settle first, so frame zero is the wind's steady state rather than a tree standing perfectly
    // still and then lurching into motion.
    animator.settle(built->rig, inputs, 6.0f);

    std::vector<std::uint64_t> hashes;
    std::vector<double> luma;
    for (int frame = 0; frame < 3; ++frame) {
        // Two seconds between samples: far enough apart that a slow limb has visibly travelled.
        for (int sub = 0; sub < 120; ++sub) {
            inputs.time = frame * 2.0 + sub / 60.0;
            animator.step(built->rig, inputs, 1.0f / 60.0f);
        }
        scene::applyTreePose(built->rig, animator, built->scene.rigs[0]);
        const auto image = renderer.renderToImage(built->scene, FrameTime{inputs.time, 1.0 / 60.0, 0}, 960, 540);
        REQUIRE(image.has_value());
        const FrameStats stats = frameStats(*image);
        INFO("frame " << frame << " subject " << stats.subjectFraction);
        // Still a tree. A skinned mesh whose weights or palette are wrong does not render blank --
        // it renders as an explosion of triangles reaching to the horizon, which shows up here as
        // the lit fraction going through the roof.
        CHECK(stats.subjectFraction > 0.04);
        CHECK(stats.subjectFraction < 0.65);
        hashes.push_back(gpu::hashImage(*image));
        luma.push_back(stats.meanLuma);
        REQUIRE(assets::writePng(outputDir() / fmt::format("tree-motion-{}.png", frame), image->width,
                                 image->height, image->rgba)
                    .has_value());
    }
    // It moved. Identical hashes would mean the pose never reached the palette, which is the
    // failure mode a disabled rig invites and the reason this is asserted rather than assumed.
    CHECK(hashes[0] != hashes[1]);
    CHECK(hashes[1] != hashes[2]);
    // But it did not wander off: a tree whose mean brightness swings wildly between samples is one
    // whose geometry is coming apart, not one that is swaying.
    CHECK(std::abs(luma[0] - luma[1]) < 4.0);
    WARN("motion frames written to " << outputDir().string());
}

// Hidden by the leading dot, following this repository's convention for anything whose assertion is
// a wall-clock magnitude: `[.perf]` tests are absent from ctest entirely rather than present and
// flaky. Run deliberately and alone, under tools/gpu-lock.sh.
TEST_CASE("tree probe: what the atmosphere costs", "[.perf][tree]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::TreeGenerator generator;
    const auto treeParams = scene::treeParamsFrom(search::sampleAt(generator.schema().parameters, 11));
    REQUIRE(treeParams.has_value());

    scene::TreeLook lit = scene::TreeLook{};
    scene::TreeLook bare = lit;
    bare.volumeDensity = 0.0f;   // turns the volumetric pass off entirely: no allocation, no passes
    bare.distantTrees = 0;

    auto withAtmosphere = scene::buildTreeScene(*treeParams, generator.camera(), lit);
    auto without = scene::buildTreeScene(*treeParams, generator.camera(), bare);
    REQUIRE(withAtmosphere.has_value());
    REQUIRE(without.has_value());

    // INTERLEAVED AND COUNTERBALANCED, INSIDE ONE PROCESS. Two invocations of a byte-identical
    // scene on this machine differ by about 3 ms, so a between-process comparison of anything
    // smaller is unreadable. Fixed-order interleaving is not enough either: whichever arm always
    // runs second pays for the run's own drift, so the order alternates ABBA and each arm sees both
    // positions equally.
    //
    // The statistic is the MINIMUM, not the median. Contention is never negative, so the fastest
    // observation is the one least polluted by everything else on the machine.
    constexpr int kPairs = 24;
    constexpr std::uint32_t kW = 960;
    constexpr std::uint32_t kH = 540;
    double bestWith = 1e30;
    double bestWithout = 1e30;

    const auto timeOne = [&](const scene::Scene& s) {
        const auto start = std::chrono::steady_clock::now();
        const auto image = renderer.renderToImage(s, FrameTime{0.0, 1.0 / 60.0, 0}, kW, kH);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        REQUIRE(image.has_value());
        return ms;
    };
    // Warm-up, discarded: a batch run right after a build treats its first run as warm-up, and
    // pipeline creation and the first upload of every buffer land on whichever arm goes first.
    for (int i = 0; i < 4; ++i) {
        timeOne(*withAtmosphere);
        timeOne(*without);
    }
    for (int pair = 0; pair < kPairs; ++pair) {
        if ((pair & 1) == 0) {
            bestWith = std::min(bestWith, timeOne(*withAtmosphere));
            bestWithout = std::min(bestWithout, timeOne(*without));
        } else {
            bestWithout = std::min(bestWithout, timeOne(*without));
            bestWith = std::min(bestWith, timeOne(*withAtmosphere));
        }
    }
    WARN(fmt::format("{}x{}, minimum of {} per arm, ABBA interleaved in one process:\n"
                     "  with atmosphere    {:7.2f} ms\n"
                     "  without            {:7.2f} ms\n"
                     "  difference         {:7.2f} ms",
                     kW, kH, kPairs, bestWith, bestWithout, bestWith - bestWithout));
    // No assertion on the magnitude. This reports; it does not gate. A wall-clock threshold in a
    // test is a cross-session comparison with the other session hidden inside a constant.
    CHECK(bestWith > 0.0);
}
