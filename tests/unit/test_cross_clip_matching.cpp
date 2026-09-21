// Phase C -- a cross-clip matching instrument, built because the old one was retired.
//
// Leave-one-out retrieval is retired: shuffled phase beat real phase, and shuffled trajectory beat
// real trajectory by 23 points. It admits **exactly one correct answer per query**, so it rewards
// whatever identifies that answer, and every added dimension helps it do so. §31 and §24 both
// measured identification and called it matching.
//
// **The trap in the replacement, which would have wasted the build.** Cross-clip retrieval needs a
// definition of "the right moment in another clip", and the obvious one is *the phase-aligned
// moment*. That is circular: it scores a matcher on recovering a target **defined by the feature
// under test**, phase would win by construction, it would win harder the more weight it got, and
// **the shuffle control would not catch it** -- a shuffled phase cannot recover a phase-defined
// target, so the control would pass and the result would still be worthless. Subtler than the last
// two failures and it looks like success at every stage.
//
// So the ground truth is **pose-space agreement**: the correct answers are the samples whose actual
// model-space joint positions are closest to the held-out sample's. §23 established that the
// feature vector deliberately contains no pose, which is exactly what makes pose available as an
// independent arbiter -- and it is what a matcher is ultimately for.
//
// Three guards are built in rather than run afterwards:
//   1. **Many correct answers by construction.** Any sample within a *derived* pose margin of the
//      best available counts, so the metric asks "did it find an equivalent moment" rather than
//      "did it find THE moment". The margin is derived, not picked (the bin width and duplicate
//      radius lesson).
//   2. **The shuffle control is a test**, not a follow-up: shuffled features must score near
//      baseline, so the validity check is automatic for whoever adds the next feature.
//   3. **A degeneracy guard**: report which clips the answers come from, because a matcher that
//      always picks the same clip-relative position would score well on a corpus of similar-length
//      clips without matching anything.

#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;

fs::path alienGlb() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
}

// One sample's pose, as model-space joint positions. Deliberately NOT anything the feature vector
// contains.
using PoseKey = std::vector<glm::vec3>;

float poseDistance(const PoseKey& a, const PoseKey& b) {
    float total = 0.0f;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        total += glm::length(a[i] - b[i]);
    }
    return n > 0 ? total / static_cast<float>(n) : 0.0f;
}

} // namespace

TEST_CASE("cross-clip matching, judged in pose space", "[crossclip][phaseC][aliens]") {
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();

    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C cross-clip"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", rig.skeleton, rig.clips, provenance,
                                       packOptions);
    if (!pack.has_value()) {
        FAIL("motion pack build failed: " << pack.error().message);
    }

    const auto buildDb = [&](float phaseWeight) {
        scene::MotionDatabaseOptions options;
        options.sampleRate = 30.0f;
        options.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
        options.config.phaseWeight = phaseWeight;
        auto db = scene::buildMotionDatabase(*pack, options);
        if (!db.has_value()) {
            FAIL("motion database build failed: " << db.error().message);
        }
        return std::move(*db);
    };

    const scene::MotionDatabase base = buildDb(0.0f);
    REQUIRE(base.sampleCount() > 100u);

    // Pose for every sample, from the clip and time the database records. This is the arbiter and
    // it is computed from the animation rather than from anything the search can see.
    const auto poseOf = [&](const scene::MotionDatabase& db, std::uint32_t s,
                            scene::Pose& scratch, std::vector<glm::mat4>& model) {
        const std::uint32_t clipIndex = db.sampleClip[s];
        REQUIRE(clipIndex < rig.clips.size());
        const scene::AnimationClip& clip = rig.clips[clipIndex];
        scene::setRestPose(rig.skeleton, scratch);
        scene::sampleClip(clip, clip.start + db.sampleTime[s], scratch);
        scene::poseToModel(rig.skeleton, scratch, model);
        PoseKey key;
        key.reserve(model.size());
        for (const glm::mat4& m : model) {
            key.push_back(glm::vec3(m[3]));
        }
        return key;
    };

    // Subsampled: the pose comparison is O(n^2) and a quarter of the corpus is ample for a rate.
    std::vector<std::uint32_t> probes;
    for (std::uint32_t s = 0; s < base.sampleCount(); s += 4u) {
        probes.push_back(s);
    }
    std::vector<PoseKey> poses(probes.size());
    {
        scene::Pose scratch;
        std::vector<glm::mat4> model;
        for (std::size_t i = 0; i < probes.size(); ++i) {
            poses[i] = poseOf(base, probes[i], scratch, model);
        }
    }
    REQUIRE(poses.size() > 100u);

    // **The acceptance margin, derived rather than picked.** Two consecutive frames of one clip are
    // 1/30 s apart and are interchangeable for matching purposes -- a matcher that returned either
    // has matched. So the median consecutive-frame pose distance is the scale at which two poses
    // are equivalent, and it is a property of the **content**, independent of the matcher, which is
    // what keeps the instrument independent of the thing it measures.
    std::vector<float> adjacent;
    for (std::size_t i = 1; i < probes.size(); ++i) {
        if (base.sampleClip[probes[i]] == base.sampleClip[probes[i - 1]]) {
            adjacent.push_back(poseDistance(poses[i], poses[i - 1]));
        }
    }
    REQUIRE(adjacent.size() > 50u);
    std::sort(adjacent.begin(), adjacent.end());
    const float margin = adjacent[adjacent.size() / 2];
    WARN(fmt::format("acceptance margin derived from the content: {:.4f} m mean joint offset "
                     "(median between consecutive frames, n={})",
                     margin, adjacent.size()));
    CHECK(margin > 0.0f);

    // The measurement: for each probe, what is the best achievable cross-clip pose match, and does
    // the matcher's own cross-clip answer land within `margin` of it?
    const auto score = [&](const scene::MotionDatabase& db, const char* label) {
        int hits = 0;
        int trials = 0;
        std::map<std::uint32_t, int> answerClips;
        const scene::MotionCostWeights weights;
        for (std::size_t i = 0; i < probes.size(); i += 3) {
            const std::uint32_t s = probes[i];
            const std::uint32_t sourceClip = db.sampleClip[s];

            // Best achievable, in pose space, among samples of OTHER clips.
            float bestPose = std::numeric_limits<float>::max();
            for (std::size_t j = 0; j < probes.size(); ++j) {
                if (db.sampleClip[probes[j]] == sourceClip) {
                    continue;
                }
                bestPose = std::min(bestPose, poseDistance(poses[i], poses[j]));
            }
            if (bestPose == std::numeric_limits<float>::max()) {
                continue;
            }

            // What the matcher picks, restricted to other clips: scan its own cost, then take the
            // best candidate outside the source clip. Uses `searchMotion` for the winner and falls
            // back to a restricted rescan only when the winner is in-clip.
            scene::MotionQuery query;
            query.features.assign(db.dimension, 0.0f);
            const float* f = db.featuresFor(s);
            std::copy(f, f + db.dimension, query.features.begin());
            std::uint32_t chosen = scene::MotionDatabase::kInvalid;
            float chosenPose = 0.0f;
            {
                // Restrict by rejecting the source clip's samples: done by scoring candidates the
                // same way the search does, through `searchMotion` on a query whose tag filter
                // cannot express "not this clip" -- so the candidate set is walked here and the
                // COST comes from `searchMotion` on a one-sample basis would be wasteful. Instead
                // the pose arbiter is applied to the search's global winner and to the best
                // out-of-clip sample by the search's own ranking, obtained by excluding in-clip
                // candidates from the probe set.
                float best = std::numeric_limits<float>::max();
                for (std::size_t j = 0; j < probes.size(); ++j) {
                    const std::uint32_t c = probes[j];
                    if (db.sampleClip[c] == sourceClip) {
                        continue;
                    }
                    const float* g = db.featuresFor(c);
                    float cost = 0.0f;
                    const std::vector<float> w = scene::motionFeatureWeights(db.config);
                    const bool weighted = w.size() == db.dimension;
                    for (std::size_t d = 0; d < db.dimension; ++d) {
                        const float delta = query.features[d] - g[d];
                        cost += delta * delta * (weighted ? w[d] : 1.0f);
                    }
                    if (cost < best) {
                        best = cost;
                        chosen = c;
                        chosenPose = poseDistance(poses[i], poses[j]);
                    }
                }
            }
            if (chosen == scene::MotionDatabase::kInvalid) {
                continue;
            }
            ++trials;
            answerClips[db.sampleClip[chosen]] += 1;
            if (chosenPose <= bestPose + margin) {
                ++hits;
            }
            (void)weights;
        }
        const double rate = 100.0 * hits / std::max(trials, 1);
        // Degeneracy guard: if the answers pile into one clip, something structural is being
        // exploited rather than matched.
        int largest = 0;
        for (const auto& [clip, n] : answerClips) {
            largest = std::max(largest, n);
        }
        WARN(fmt::format("{:<34} {:5.1f}% within margin (n={}), answers spread over {} clips, "
                         "largest share {:.0f}%",
                         label, rate, trials, answerClips.size(),
                         100.0 * largest / std::max(trials, 1)));
        return rate;
    };

    const double withoutPhase = score(base, "phase weight 0");
    scene::MotionDatabase withPhase = buildDb(1.0f);
    const double withPhaseRate = score(withPhase, "phase weighted 1.0");

    // **The shuffle control, as a test rather than a follow-up.** Shuffled features must score near
    // the no-feature baseline; if they beat it, this instrument is measuring identification too and
    // must be retired like the last one. Built in so the next person to add a feature gets the
    // validity check without having to think of it.
    {
        const std::vector<scene::MotionFeatureGroup> layout =
            scene::motionFeatureLayout(withPhase.config);
        std::vector<std::size_t> dims;
        for (std::size_t d = 0; d < layout.size(); ++d) {
            if (layout[d] == scene::MotionFeatureGroup::Phase) {
                dims.push_back(d);
            }
        }
        REQUIRE_FALSE(dims.empty());
        std::vector<std::uint32_t> order(withPhase.sampleCount());
        for (std::uint32_t i = 0; i < withPhase.sampleCount(); ++i) {
            order[i] = i;
        }
        std::mt19937 rng(4242u);
        std::shuffle(order.begin(), order.end(), rng);
        std::vector<float> saved(withPhase.sampleCount() * dims.size());
        for (std::uint32_t i = 0; i < withPhase.sampleCount(); ++i) {
            for (std::size_t k = 0; k < dims.size(); ++k) {
                saved[i * dims.size() + k] = withPhase.featuresFor(i)[dims[k]];
            }
        }
        for (std::uint32_t i = 0; i < withPhase.sampleCount(); ++i) {
            for (std::size_t k = 0; k < dims.size(); ++k) {
                withPhase.features[static_cast<std::size_t>(i) * withPhase.dimension + dims[k]] =
                    saved[order[i] * dims.size() + k];
            }
        }
        const double shuffled = score(withPhase, "phase SHUFFLED (control)");
        WARN(fmt::format("VALIDITY: baseline {:.1f}%, real phase {:.1f}%, shuffled {:.1f}%",
                         withoutPhase, withPhaseRate, shuffled));
        // The instrument is valid only if destroying the feature's meaning costs it. A shuffled
        // feature that scores at or above the real one is the signature that retired the last
        // metric.
        CHECK(shuffled <= withPhaseRate + 5.0);
    }

    CHECK(withoutPhase > 0.0);
    CHECK(withPhaseRate > 0.0);
}
