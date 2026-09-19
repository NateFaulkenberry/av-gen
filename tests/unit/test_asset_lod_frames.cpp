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
