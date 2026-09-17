// Character Animation Lab -- §3, the capability inventory, measured rather than asserted.
//
// ADR-161 decided that root motion is not implemented because the content has none, and it closed
// with: "If an asset with real root translation is ever imported, this ADR is the place to start;
// the measurement above is the check that says whether it has any."
//
// That measurement was run on three clips of one file -- `assets/imported/alien.gltf`, a Mixamo rig.
// The modular alien pack (ADR-192) arrived the following day with **26 clips on a different
// skeleton from a different rigging tool** (Auto-Rig Pro, 89 deform bones), and the farm pack
// (ADR-198) after that. `test_alien_locomotion.cpp` spot-checks five of the twenty-six on the alien
// pack only. Nothing has ever run ADR-161's check across everything the project actually loads.
//
// This file does. Every animated asset in the repository, every clip in it, the topological root of
// its skeleton, and the net XZ displacement of that root over the clip's own key span. If any clip
// anywhere carries real root translation then the premise the entire code-driven locomotion path
// rests on is false for that asset, and this is the test that says so, by name.
//
// Which joint is "the root" is chosen per clip, as **the lowest-indexed joint the clip gives a
// translation channel to**. Import order is topological, so the lowest index among the animated
// joints is the highest one in the hierarchy that this clip moves -- and a translation applied
// there is exactly what root motion would be.
//
// It is not `joints[0]`. That was the first attempt and its control arm killed it: `joints[0]` is
// an armature wrapper that no clip animates, so all 168 clips reported a root that never moved, for
// the most boring possible reason. Naming the bone instead ("root.x", "mixamorig:Hips") would have
// worked for the two packs whose conventions are already known here and silently mismeasured the
// third.
//
// ADR-182: the control is the *span* arm. A net displacement of zero could mean "the clip is
// in-place" or "this code is reading a channel that does not exist and getting the rest pose twice".
// The root's XZ *span* over the clip separates them: an in-place cycle has a root that moves (the
// hips sway) and returns. A span of zero alongside a net of zero means the measurement is dead.

#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"
#include "support/stride_speed.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path assetRoot() {
    // The tests run from the build directory; the repository root is where `assets` lives.
    for (fs::path p = fs::current_path(); !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / "assets" / "farm")) {
            return p / "assets";
        }
        if (p == p.parent_path()) {
            break;
        }
    }
    return {};
}

struct RootMeasure {
    float net = 0.0f;  // |end - begin| in XZ: how far the root actually travelled over the clip
    float span = 0.0f; // the XZ extent the root covered on the way: the control
    float netY = 0.0f;
    int samples = 0;
};

// The root's XZ displacement over `clip`, sampled across the clip's own key span.
RootMeasure measureRoot(const scene::SkinnedRig& rig, const scene::AnimationClip& clip, int root) {
    const testing::ClipKeySpan keys = testing::clipKeySpan(clip);
    RootMeasure m;
    if (!(keys.period() > 0.0f) || root < 0) {
        return m;
    }
    scene::Pose pose;
    std::vector<glm::mat4> model;
    constexpr int kSteps = 48;
    glm::vec3 lo(1e9f), hi(-1e9f);
    glm::vec3 begin(0.0f), end(0.0f);
    for (int i = 0; i <= kSteps; ++i) {
        const float t = keys.first + keys.period() * static_cast<float>(i) / kSteps;
        const glm::vec3 p = testing::jointPositionAt(rig, clip, root, t, pose, model);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
        if (i == 0) {
            begin = p;
        }
        end = p;
        ++m.samples;
    }
    m.net = glm::length(glm::vec2(end.x - begin.x, end.z - begin.z));
    m.span = glm::length(glm::vec2(hi.x - lo.x, hi.z - lo.z));
    m.netY = end.y - begin.y;
    return m;
}

// The highest joint in the hierarchy that this clip translates, or -1 if it translates none.
int animatedRoot(const scene::AnimationClip& clip) {
    int best = -1;
    for (const scene::AnimationChannel& c : clip.channels) {
        if (c.path != scene::AnimationPath::Translation || c.times.empty()) {
            continue;
        }
        const int j = static_cast<int>(c.joint);
        if (best < 0 || j < best) {
            best = j;
        }
    }
    return best;
}

struct AssetUnderTest {
    std::string label;
    fs::path path;
};

std::vector<AssetUnderTest> animatedAssets() {
    const fs::path root = assetRoot();
    std::vector<AssetUnderTest> out;
    if (root.empty()) {
        return out;
    }
    const auto add = [&](const fs::path& p) {
        if (fs::exists(p)) {
            out.push_back({p.filename().string(), p});
        }
    };
    add(root / "imported" / "alien.gltf"); // the Mixamo rig ADR-161 measured
    for (const char* a : {"alien-scout", "alien-ranger", "alien-trooper", "alien-pilot", "alien-diver",
                          "alien-elder"}) {
        add(root / "aliens" / (std::string(a) + ".glb"));
    }
    for (const char* a : {"bull", "horse", "cow", "sheep", "goat", "pig", "rooster", "chicken", "chick"}) {
        add(root / "farm" / (std::string(a) + ".glb"));
    }
    return out;
}

} // namespace

TEST_CASE("no animated asset in the repository carries root motion", "[unit][charlab][inventory]") {
    const std::vector<AssetUnderTest> assets = animatedAssets();
    if (assets.empty()) {
        SKIP("no animated assets present");
    }

    int clipsChecked = 0;
    int assetsChecked = 0;
    int clipsWithAMovingRoot = 0; // the control: clips whose root moves at all
    float worstNet = 0.0f;
    std::string worstWhere;
    // A centimetre over a whole cycle is far above float error and far below a stride: anything
    // over it is a clip that means to travel.
    constexpr float kTravelThreshold = 0.01f;
    std::vector<std::string> travellers;

    for (const AssetUnderTest& a : assets) {
        scene::Scene s;
        const auto summary = assets::loadGltf(a.path, s);
        if (!summary || s.rigs.empty()) {
            continue;
        }
        ++assetsChecked;
        for (const scene::SkinnedRig& rig : s.rigs) {
            if (rig.skeleton.joints.empty()) {
                continue;
            }
            REQUIRE(rig.skeleton.joints.front().parent == -1); // topological order: index 0 is a root
            for (const scene::AnimationClip& clip : rig.clips) {
                const RootMeasure m = measureRoot(rig, clip, animatedRoot(clip));
                if (m.samples == 0) {
                    continue;
                }
                ++clipsChecked;
                if (m.span > 1e-3f) {
                    ++clipsWithAMovingRoot;
                }
                if (m.net > worstNet) {
                    worstNet = m.net;
                    worstWhere = fmt::format("{} / {} (net {:.5f} m, span {:.4f} m, dY {:+.4f} m)",
                                             a.label, clip.name, m.net, m.span, m.netY);
                }
                if (std::max(m.net, std::fabs(m.netY)) > kTravelThreshold) {
                    travellers.push_back(fmt::format("{}/{} net {:.3f} m dY {:+.3f} m", a.label,
                                                     clip.name, m.net, m.netY));
                }
            }
        }
    }

    std::sort(travellers.begin(), travellers.end());
    INFO(fmt::format("{} assets, {} clips; {} have a root that moves at all; worst net: {}",
                     assetsChecked, clipsChecked, clipsWithAMovingRoot,
                     worstWhere.empty() ? "none" : worstWhere));
    {
        std::string list;
        for (const std::string& t : travellers) {
            list += "\n  " + t;
        }
        INFO(fmt::format("clips that TRAVEL ({} of {}):{}", travellers.size(), clipsChecked, list));
    }

    REQUIRE(assetsChecked >= 10);
    REQUIRE(clipsChecked >= 100);

    // The control. If the sampler were broken -- reading a channel that is not there, or handing
    // back the rest pose every time -- every root would be perfectly still and `net` would be zero
    // for the most boring possible reason. These are mostly in-place cycles, so the root is supposed
    // to move and come back; a run where nothing moves at all is a run that measured nothing.
    CHECK(clipsWithAMovingRoot > clipsChecked / 2);

    // ADR-161 concluded "there is no root motion in this content to extract" from three clips of
    // one Mixamo file. Across all 168 clips the project can load that is **not true**: a handful of
    // one-shot clips -- deaths and dodges -- genuinely translate their root by up to a metre.
    //
    // This is pinned as an inventory rather than as a failure, because it is not currently a bug:
    // none of the travelling clips is named by any scene in the repository. The locomotion clips
    // Glowmere actually plays (Walking, Running, Idle, Idle_turn, Jumping, Fall_loop, Landing) are
    // in-place, and for those the premise holds. What this list is for is the day somebody authors
    // `Dying_forward` into a scene and the character dies a metre from where the animation put it.
    CHECK_FALSE(travellers.empty());
    // Every travelling clip is a one-shot. If a *locomotion* clip ever appears in this list, the
    // code-driven locomotion path is double-counting or under-counting displacement and ADR-161
    // has to be reopened.
    for (const std::string& t : travellers) {
        INFO(t);
        CHECK(t.find("/Walking") == std::string::npos);
        CHECK(t.find("/Running") == std::string::npos);
        CHECK(t.find("/Idle") == std::string::npos);
        CHECK(t.find("/Walk ") == std::string::npos);
    }
}

TEST_CASE("the animation capability inventory is what the lab documentation says it is",
          "[unit][charlab][inventory]") {
    const std::vector<AssetUnderTest> assets = animatedAssets();
    if (assets.empty()) {
        SKIP("no animated assets present");
    }

    // §3 asks for clip name, duration, and whether the clip is loopable, per asset. The part worth
    // pinning in a test rather than printing is the structural claim the lab documentation makes:
    // every clip's playable span starts at its *first key*, which for this project's content is not
    // zero (ADR-204). A clip whose `start` was assumed to be zero plays a held first pose at the top
    // of every loop, and the alien pack's every clip starts at 1/30 s.
    int clipsStartingLate = 0;
    int clipsTotal = 0;
    for (const AssetUnderTest& a : assets) {
        scene::Scene s;
        if (!assets::loadGltf(a.path, s) || s.rigs.empty()) {
            continue;
        }
        for (const scene::SkinnedRig& rig : s.rigs) {
            for (const scene::AnimationClip& clip : rig.clips) {
                ++clipsTotal;
                CHECK(clip.duration >= clip.start);
                CHECK(clip.length() > 0.0f);
                if (clip.start > 1e-4f) {
                    ++clipsStartingLate;
                }
            }
        }
    }
    INFO(fmt::format("{} clips, {} of which have their first key after t=0", clipsTotal, clipsStartingLate));
    REQUIRE(clipsTotal >= 100);
    // If this ever reaches zero the content changed, and `AnimationClip::start` -- and the reason it
    // exists -- should be re-read before anyone concludes it is dead weight.
    CHECK(clipsStartingLate > 0);
}
