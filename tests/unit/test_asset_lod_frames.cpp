// What each rung of the Tree of Life's ladder actually *looks* like, against LOD0, at five
// distances (ADR-344; §11 and §13 of the asset-LOD brief).
//
//   tools/make_assetlod_arms.py
//   <render every arm to $AVGEN_LOD_FRAMES/<arm>/frame_000000.png>
//   AVGEN_LOD_FRAMES=<dir> avgen_tests "[.analysis][assetlodframes]" --success
//
// This is a measuring instrument over frames somebody else rendered, deliberately: the renders need
// a GPU and the GPU lock, this needs neither, and separating them means the numbers can be
// recomputed from the same frames without a second render. It is skipped, not faked, when the
// frames are not there.
//
// **Why frames and not triangle counts.** A triangle count is not a picture, which is this
// project's standing instruction, and the question §13 asks -- "how little geometry can we render
// while producing essentially the same image at this screen size" -- has no arithmetic answer. The
// selector's cost rule (ADR-124) has no quality floor in it at all: it takes the coarsest rung
// under a px/triangle target, and on an asset whose triangles are a fifth of a pixel each, that is
// always the bottom of the ladder. Something has to say where "essentially the same image" stops,
// and only a pair of frames can.
//
// MS-SSIM rather than a pixel diff, because a pixel diff on a canopy of 122,000 leaves reports
// "94% of pixels differ" for a change nobody can see and for one nobody could miss.
#include "capture/sequence.hpp"
#include "metrics/spatial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

const char* const kViews[] = {"closeup", "hero", "medium", "wide", "small"};
const char* const kLadders[] = {"r1", "r2", "r3", "r4", "auto", "r2-nothin"};

fs::path frameRoot() {
    const char* env = std::getenv("AVGEN_LOD_FRAMES");
    return env == nullptr ? fs::path{} : fs::path(env);
}

fs::path framePath(const fs::path& root, const std::string& arm) {
    return root / arm / "frame_000000.png";
}

} // namespace

TEST_CASE("what each rung of the Tree of Life's ladder looks like", "[.analysis][assetlodframes]") {
    const fs::path root = frameRoot();
    if (root.empty() || !fs::is_directory(root)) {
        SKIP("set AVGEN_LOD_FRAMES to a directory of rendered arms (tools/make_assetlod_arms.py)");
    }
    std::printf("\n%-10s %-10s %9s %9s %9s %9s\n", "view", "ladder", "ms-ssim", "psnr", "dE mean",
                "dE p95");
    for (const char* view : kViews) {
        const fs::path basePath = framePath(root, std::string("tree-") + view + "-off");
        if (!fs::is_regular_file(basePath)) {
            continue;
        }
        const auto base = quality::readFrame(basePath);
        REQUIRE(base);
        for (const char* ladder : kLadders) {
            const fs::path armPath = framePath(root, std::string("tree-") + view + "-" + ladder);
            if (!fs::is_regular_file(armPath)) {
                continue;
            }
            const auto arm = quality::readFrame(armPath);
            REQUIRE(arm);
            REQUIRE(base->sameShapeAs(*arm));
            const double ssim = quality::msSsim(*base, *arm);
            const double db = quality::psnr(*base, *arm);
            const quality::ColourDifference colour = quality::ciede2000(*base, *arm);
            std::printf("%-10s %-10s %9.5f %9.2f %9.4f %9.4f\n", view, ladder, ssim, db, colour.mean,
                        colour.p95);
            // The control this table would otherwise be missing: a pair of frames that are the same
            // frame. Without it, a run where every arm accidentally rendered the same scene -- a
            // project path typo, a `lod` block that failed to parse -- would print a column of
            // 1.00000 and read as a triumph.
            CHECK(quality::msSsim(*base, *base) > 0.9999);
        }
        // And the other half of it: some arm in this view must actually differ from the base, or
        // the renders did not carry the change they were supposed to.
        bool anyDiffers = false;
        for (const char* ladder : kLadders) {
            const fs::path armPath = framePath(root, std::string("tree-") + view + "-" + ladder);
            if (!fs::is_regular_file(armPath)) {
                continue;
            }
            const auto arm = quality::readFrame(armPath);
            anyDiffers = anyDiffers || (arm && quality::msSsim(*base, *arm) < 0.9999);
        }
        CHECK(anyDiffers);
    }
}

// ---- the dolly: popping, measured between consecutive frames -------------------------------------
//
// §11 asks for "camera moving toward" and "camera moving away" and says to look for popping. The
// selector's own stability is tested device-free in test_representation.cpp; this measures what a
// rung change does to the *picture*, which is the only place a pop exists.
//
// Thirteen steps from 660 m to 420 m, which brackets the rung 1 -> rung 2 boundary the eight-pixel
// floor puts at about 520 m. One project per step with the clock frozen, so the only thing that
// differs between two consecutive frames is the camera -- and, at one of the pairs, the rung.
//
// The control is the same thirteen steps with no ladder at all. A camera that moves changes the
// frame, so "consecutive frames differ" proves nothing on its own: what a pop is, is the LOD pair
// differing *more than the no-LOD pair at the same two distances*. That ratio is the number this
// prints, and it is the one to read.
TEST_CASE("a dolly across a rung boundary does not pop", "[.analysis][assetlodframes]") {
    const fs::path root = frameRoot();
    if (root.empty() || !fs::is_directory(root / "dolly-auto-00")) {
        SKIP("set AVGEN_LOD_FRAMES to a directory containing the rendered dolly arms");
    }
    constexpr int kSteps = 13;
    std::vector<double> lodPairs;
    std::vector<double> basePairs;
    std::printf("\n%-6s %-10s %10s %10s %10s\n", "pair", "distance", "lod ssim", "off ssim", "excess");
    for (int i = 1; i < kSteps; ++i) {
        const auto previousLod = quality::readFrame(
            framePath(root, fmt::format("dolly-auto-{:02d}", i - 1)));
        const auto currentLod = quality::readFrame(framePath(root, fmt::format("dolly-auto-{:02d}", i)));
        const auto previousOff = quality::readFrame(
            framePath(root, fmt::format("dolly-off-{:02d}", i - 1)));
        const auto currentOff = quality::readFrame(framePath(root, fmt::format("dolly-off-{:02d}", i)));
        if (!previousLod || !currentLod || !previousOff || !currentOff) {
            SKIP("the dolly arms are not fully rendered");
        }
        const double lodStep = quality::msSsim(*previousLod, *currentLod);
        const double baseStep = quality::msSsim(*previousOff, *currentOff);
        lodPairs.push_back(lodStep);
        basePairs.push_back(baseStep);
        // Dissimilarity rather than similarity, so "twice as different" is twice the number.
        const double excess = (1.0 - baseStep) > 1e-9 ? (1.0 - lodStep) / (1.0 - baseStep) : 1.0;
        std::printf("%-6d %10.0f %10.5f %10.5f %10.2f\n", i, 660.0 - 20.0 * i, lodStep, baseStep, excess);
    }
    REQUIRE(lodPairs.size() == kSteps - 1);

    // The control first. The camera moves twenty metres between steps, so consecutive frames differ
    // whatever the ladder does; if they did not, the dolly did not happen and every number below
    // would be measuring a still.
    const double worstBase = *std::ranges::min_element(basePairs);
    CHECK(worstBase < 0.9999);

    // The arm. The worst LOD pair may be no more than three times as different as the worst no-LOD
    // pair. A band rather than a floor, and three rather than one because a rung change *is* a
    // change -- the claim is that it is of the same order as the camera's own motion, not that it
    // is invisible.
    const double worstLod = *std::ranges::min_element(lodPairs);
    const double worstExcess = (1.0 - worstBase) > 1e-9 ? (1.0 - worstLod) / (1.0 - worstBase) : 1.0;
    std::printf("\nworst pair: lod %.5f, off %.5f, excess %.2fx\n", worstLod, worstBase, worstExcess);
    CHECK(worstExcess < 3.0);
}
