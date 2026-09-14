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

// Mean luminance and the fraction of non-background pixels. A frame that renders but shows nothing
// is the failure this catches, and it is the one a "did it return a value" check misses.
struct FrameStats {
    double meanLuma = 0.0;
    double litFraction = 0.0;
};

FrameStats frameStats(const gpu::Image8& image) {
    FrameStats out;
    double sum = 0.0;
    std::size_t lit = 0;
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t i = 0; i < pixels; ++i) {
        const double l = 0.2126 * image.rgba[i * 4] + 0.7152 * image.rgba[i * 4 + 1] + 0.0722 * image.rgba[i * 4 + 2];
        sum += l;
        if (l > 18.0) {
            ++lit;
        }
    }
    out.meanLuma = sum / static_cast<double>(pixels);
    out.litFraction = static_cast<double>(lit) / static_cast<double>(pixels);
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
    INFO(fmt::format("mean luma {:.2f}, lit fraction {:.3f}", stats.meanLuma, stats.litFraction));
    // Not blank, and not a white-out. Both are ways for the pipeline to "succeed" while showing
    // nothing, and neither is caught by checking that render returned a value.
    CHECK(stats.litFraction > 0.02);
    CHECK(stats.litFraction < 0.995);
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
        INFO("candidate " << candidate.index << " lit " << stats.litFraction);
        CHECK(stats.litFraction > 0.01);

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
