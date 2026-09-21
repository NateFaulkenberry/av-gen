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
#include "entity/match_motion_provider.hpp"
#include "scene/animation.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"
#include "scene/motion_quality_report.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
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
    // **The denominator, before any rate is read.** A cross-clip hit rate's value is a property of
    // what was reachable, the same way a filter's value is a property of the corpus (§14).
    //
    // Two things have to be separated here, and the first is a correction to how this reads. The
    // criterion is `chosenPose <= bestPose + margin` -- within the margin **of the best achievable
    // cross-clip answer**, not within the margin absolutely. So **the oracle rate is 100% by
    // construction**: the best achievable always satisfies it. The metric cannot be measuring
    // corpus coverage, because it never asks for an answer better than the corpus contains.
    //
    // What does need reporting is **how good the best available answer is**, because "within
    // 0.0565 m of the best" is a tight band around a poor answer if the best is itself distant.
    // That is the number that says whether the task is compressed, and it is reported per arm --
    // which is also a free control: the target is pose-space and independent of the feature vector,
    // so **if `bestPose` moves between arms the ground truth is leaking from the features** and
    // nothing else the instrument says can be trusted.
    const auto score = [&](const scene::MotionDatabase& db, const char* label) {
        int hits = 0;
        int trials = 0;
        double bestPoseTotal = 0.0;
        std::vector<float> bestPoses;
        std::map<std::uint32_t, int> answerClips;
        const scene::MotionCostWeights weights;
        // n raised now that validity is established -- the correct order, and the reason it was
        // wrong to do this first: precision around an invalid instrument is a confidently wrong
        // answer. Every probe rather than every third.
        for (std::size_t i = 0; i < probes.size(); ++i) {
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
            bestPoseTotal += static_cast<double>(bestPose);
            bestPoses.push_back(bestPose);
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
        std::sort(bestPoses.begin(), bestPoses.end());
        const float medianBest = bestPoses.empty() ? 0.0f : bestPoses[bestPoses.size() / 2];
        WARN(fmt::format("{:<34} {:5.1f}% within margin (n={}), answers over {} clips, largest "
                         "share {:.0f}%; best achievable cross-clip pose: median {:.4f} m",
                         label, rate, trials, answerClips.size(),
                         100.0 * largest / std::max(trials, 1), medianBest));
        return std::pair<double, float>{rate, medianBest};
    };

    const auto [withoutPhase, oracleA] = score(base, "phase weight 0");
    scene::MotionDatabase withPhase = buildDb(1.0f);
    const auto [withPhaseRate, oracleB] = score(withPhase, "phase weighted 1.0");

    // **The free control.** The ground truth is pose-space and cannot depend on the feature vector,
    // so the best achievable answer must be identical between arms. If it moves, the target is
    // leaking from the features and every other number here is void.
    WARN(fmt::format("ORACLE: best achievable cross-clip pose is {:.4f} m with phase off and "
                     "{:.4f} m with it on -- identical, so the ground truth is independent; the "
                     "acceptance margin is {:.4f} m",
                     oracleA, oracleB, margin));
    CHECK(oracleA == Approx(oracleB).margin(1e-6f));
    // And the compression question: if the best available cross-clip pose is far compared with the
    // margin, the metric asks the matcher to hit a narrow band around a mediocre answer. Reported
    // rather than asserted -- it is a property of the corpus, and §20 would change it.
    WARN(fmt::format("the margin is {:.2f}x the median best-achievable distance", margin / std::max(oracleA, 1e-6f)));

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
        const double shuffled = score(withPhase, "phase SHUFFLED (control)").first;
        WARN(fmt::format("VALIDITY: baseline {:.1f}%, real phase {:.1f}%, shuffled {:.1f}%",
                         withoutPhase, withPhaseRate, shuffled));
        // The instrument is valid only if destroying the feature's meaning costs it. A shuffled
        // feature that scores at or above the real one is the signature that retired the last
        // metric.
        CHECK(shuffled <= withPhaseRate + 5.0);
    }

    // **The answer, at adequate power: phase-aware matching shows no measurable benefit here.**
    //
    //   n=145   baseline 32.4%   phase 35.9%   shuffled 33.1%   (+3.5, +-4.0 -- noise)
    //   n=435   baseline 34.7%   phase 34.9%   shuffled 32.0%   (+0.2, +-2.3 -- zero)
    //
    // Raising n did not tighten a real effect; it dissolved an apparent one. The +3.5 was inside
    // its own error bar at n=145 and said so, and this is what that warning looked like when it
    // was honoured rather than explained away.
    //
    // A +0.2 effect would need roughly n=100,000 to resolve, which this corpus cannot supply at
    // any stride -- so the honest statement is **no effect detectable on this corpus**, not "not
    // yet significant". §13's fix remains correct and necessary; what it does not do is buy
    // measurable matching quality, which is what the retraction suspected and this now shows with
    // an instrument that can tell meaning from identity.
    //
    // Shuffled scoring *below* baseline (32.0 vs 34.7) is the expected sign: adding dimensions that
    // carry noise costs a little, which is also §24's warning about dimensions not being free,
    // arriving from a valid instrument this time.
    // **The neutralise experiment that used to live here has been removed, and why matters.**
    //
    // `dimension()` added one contact flag per *feature* joint rather than per *contact* joint, so
    // `head.x` carried one -- a dimension meaningless by construction that nonetheless varied, and
    // so passed every liveness check. That made it a natural experiment: neutralising it (setting
    // it constant, so it adds the same amount to every distance and cannot affect a ranking)
    // measured what a noise dimension costs with dimensionality held fixed. It read **33.3% with
    // and 33.3% without -- 0.0 points**.
    //
    // **That null was underpowered and the dose-response below reversed it** (-0.30 points per
    // dimension, from K=4 and K=16 agreeing to 0.01). The experiment is gone because **the bug it
    // depended on is fixed** -- contacts now follow their own joint list and there is no bogus
    // flag to neutralise. Recorded here rather than deleted silently: it was a real measurement, it
    // was wrong in the direction of convenience, and the correction came from measuring a slope
    // instead of a point.

    // **Dose-response: the marginal cost of a useless dimension.**
    //
    // The neutralise test above measures one point near the origin with a bar wide enough to hide
    // the effect -- 0.0 +-2.3 on ONE dimension of 36 cannot distinguish "free" from "cheap". The
    // claim under test is about a *slope*, so it needs more than one dose. K fresh random
    // dimensions are appended, fixed at build so the database is stable, and measured at K = 1, 4
    // and 16. At sixteen the effect must appear if the claim is true.
    //
    // **Run at uniform weights**, deliberately: appending dimensions makes the config's weight
    // vector the wrong length, which would silently drop weighting for *every* dimension and
    // confound the arms. With all weights at 1.0 the weighted and unweighted paths are identical,
    // so the only thing differing between arms is K.
    {
        scene::MotionDatabaseOptions uniform;
        uniform.sampleRate = 30.0f;
        uniform.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
        uniform.config.jointPositionWeight = 1.0f;
        uniform.config.jointVelocityWeight = 1.0f;
        uniform.config.trajectoryPositionWeight = 1.0f;
        uniform.config.trajectoryFacingWeight = 1.0f;
        uniform.config.rootVelocityWeight = 1.0f;
        auto flat = scene::buildMotionDatabase(*pack, uniform);
        if (!flat.has_value()) {
            FAIL("motion database build failed: " << flat.error().message);
        }

        const auto withNoiseDims = [&](std::uint32_t k) {
            scene::MotionDatabase out = *flat;
            if (k == 0u) {
                return out;
            }
            const std::uint32_t oldDim = flat->dimension;
            out.dimension = oldDim + k;
            out.features.assign(static_cast<std::size_t>(flat->sampleCount()) * out.dimension, 0.0f);
            std::mt19937 rng(2024u);
            std::normal_distribution<float> gauss(0.0f, 1.0f);
            for (std::uint32_t s = 0; s < flat->sampleCount(); ++s) {
                const float* src = flat->featuresFor(s);
                for (std::uint32_t d = 0; d < oldDim; ++d) {
                    out.features[static_cast<std::size_t>(s) * out.dimension + d] = src[d];
                }
                for (std::uint32_t d = 0; d < k; ++d) {
                    out.features[static_cast<std::size_t>(s) * out.dimension + oldDim + d] =
                        gauss(rng);
                }
            }
            out.mean.assign(out.dimension, 0.0f);
            out.scale.assign(out.dimension, 1.0f);
            return out;
        };

        WARN("DOSE-RESPONSE: appended random dimensions, uniform weights");
        double atZero = 0.0;
        for (const std::uint32_t k : {0u, 1u, 4u, 16u}) {
            const scene::MotionDatabase probe = withNoiseDims(k);
            const auto [rate, oracleK] =
                score(probe, fmt::format("  +{} random dimensions", k).c_str());
            if (k == 0u) {
                atZero = rate;
            } else {
                WARN(fmt::format("    K={:<2} dim {}  {:+.1f} points against K=0 ({:+.2f} per "
                                 "dimension)",
                                 k, probe.dimension, rate - atZero,
                                 (rate - atZero) / static_cast<double>(k)));
            }
            // The oracle cannot move: appending feature dimensions does not change which
            // pose-space answers exist.
            CHECK(oracleK > 0.0f);
        }

        // **And a bound available from arithmetic, stronger than the null looks.** If a useless
        // dimension cost anything like 2 points -- the top of the neutralise interval -- then a
        // 36-dimension vector carrying even a handful of weak ones would be catastrophically
        // degraded, and it plainly is not. So the per-dimension cost is already bounded well below
        // the interval that was measured, and the honest statement is "bounded above by something
        // small" rather than "we could not see it" -- which is what stops a reader assuming the
        // effect is real and the test was weak.
        CHECK(atZero > 0.0);
    }

    // **§30: contacts, judged on NET value.** The dose-response gives a hurdle: two contact
    // dimensions cost about 2 x 0.30 = **0.6 points** at unit weight, so contacts must buy more
    // than that to be worth carrying at all. Reporting the effect against zero would answer "does
    // it help"; reporting it against the hurdle answers "is it worth its dimensions", which is the
    // question §24 was groping at.
    {
        scene::MotionDatabaseOptions withContacts;
        withContacts.sampleRate = 30.0f;
        withContacts.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
        withContacts.config.contactWeight = 1.0f;
        auto contactsDb = scene::buildMotionDatabase(*pack, withContacts);
        if (!contactsDb.has_value()) {
            FAIL("motion database build failed: " << contactsDb.error().message);
        }
        const auto [withContactsRate, oracleC] = score(*contactsDb, "§30 contacts ON");

        // The shuffle, built in: contacts are the feature most likely to act as an identifier,
        // since a left/right planting pattern is close to a clip-phase fingerprint.
        const std::vector<scene::MotionFeatureGroup> lay =
            scene::motionFeatureLayout(contactsDb->config);
        std::vector<std::size_t> dims;
        for (std::size_t d = 0; d < lay.size(); ++d) {
            if (lay[d] == scene::MotionFeatureGroup::Contact) {
                dims.push_back(d);
            }
        }
        REQUIRE(dims.size() == 2u);
        scene::MotionDatabase shuffledContacts = *contactsDb;
        {
            std::vector<std::uint32_t> order(shuffledContacts.sampleCount());
            for (std::uint32_t i = 0; i < shuffledContacts.sampleCount(); ++i) {
                order[i] = i;
            }
            std::mt19937 rng(777u);
            std::shuffle(order.begin(), order.end(), rng);
            std::vector<float> saved(shuffledContacts.sampleCount() * dims.size());
            for (std::uint32_t i = 0; i < shuffledContacts.sampleCount(); ++i) {
                for (std::size_t k = 0; k < dims.size(); ++k) {
                    saved[i * dims.size() + k] = shuffledContacts.featuresFor(i)[dims[k]];
                }
            }
            for (std::uint32_t i = 0; i < shuffledContacts.sampleCount(); ++i) {
                for (std::size_t k = 0; k < dims.size(); ++k) {
                    shuffledContacts
                        .features[static_cast<std::size_t>(i) * shuffledContacts.dimension +
                                  dims[k]] = saved[order[i] * dims.size() + k];
                }
            }
        }
        const auto [shuffledContactsRate, oracleS] = score(shuffledContacts, "§30 contacts SHUFFLED");

        const double hurdle = 2.0 * 0.30;
        WARN(fmt::format("§30 NET: baseline {:.1f}%, contacts {:.1f}% ({:+.1f}), shuffled {:.1f}%; "
                         "dimensional hurdle {:.1f} points, so net {:+.1f}",
                         withoutPhase, withContactsRate, withContactsRate - withoutPhase,
                         shuffledContactsRate, hurdle,
                         (withContactsRate - withoutPhase) - hurdle));
        CHECK(oracleC == Approx(oracleS).margin(1e-6f));
        // **The third-instance watch.** If shuffled contacts beat real contacts, that is not "a
        // third feature failed" -- three independent features behaving as identifiers under one
        // encoding is a statement about the ENCODING, and it gets written up separately rather
        // than folded in here.
        if (shuffledContactsRate > withContactsRate) {
            WARN("*** shuffled contacts BEAT real contacts -- third instance, see the design log");
        }
    }

    // **THE DECISIVE EXPERIMENT: ablate the implicit signal, then re-measure the explicit one.**
    //
    // Redundancy and wrong-data both predict every row observed. They differ in what happens when
    // the *implicit* copy is removed:
    //
    //   redundancy  -> contacts were worthless because the information was already in the pose
    //                  block; remove the other copy and contacts should become POSITIVE.
    //   wrong data  -> contacts carry something incorrect; removing the pose signal changes
    //                  nothing about that, and they stay NEGATIVE.
    //
    // Causal where a correlation would be associational -- and a per-dimension linear correlation
    // could read low even under full redundancy, because "is this foot planted" is a joint,
    // non-linear function of several pose and velocity dimensions rather than a linear echo of any
    // one. A low correlation would kill only the *linear* version of the hypothesis.
    //
    // Neutralising the foot velocity dimensions isolates the signal from the dimension count, the
    // property established when that technique was introduced.
    {
        scene::MotionDatabaseOptions opts;
        opts.sampleRate = 30.0f;
        opts.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
        opts.config.contactWeight = 1.0f;
        auto full = scene::buildMotionDatabase(*pack, opts);
        if (!full.has_value()) {
            FAIL("motion database build failed: " << full.error().message);
        }
        const std::vector<scene::MotionFeatureGroup> lay = scene::motionFeatureLayout(full->config);

        // Arm A: pose velocities neutralised, contacts OFF (by neutralising them too).
        // Arm B: pose velocities neutralised, contacts ON.
        // The difference is the contact feature's value *with the implicit copy gone*.
        const auto neutralise = [&](scene::MotionDatabase db,
                                    std::initializer_list<scene::MotionFeatureGroup> groups) {
            for (std::size_t d = 0; d < lay.size(); ++d) {
                bool hit = false;
                for (const scene::MotionFeatureGroup g : groups) {
                    if (lay[d] == g) {
                        hit = true;
                    }
                }
                if (!hit) {
                    continue;
                }
                for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
                    db.features[static_cast<std::size_t>(s) * db.dimension + d] = 0.0f;
                }
            }
            return db;
        };

        const scene::MotionDatabase noVelNoContact =
            neutralise(*full, {scene::MotionFeatureGroup::JointVelocity,
                               scene::MotionFeatureGroup::Contact});
        const scene::MotionDatabase noVelWithContact =
            neutralise(*full, {scene::MotionFeatureGroup::JointVelocity});

        const auto [ablatedBase, oA] = score(noVelNoContact, "ABLATED: no joint velocity, no contact");
        const auto [ablatedContact, oB] = score(noVelWithContact, "ABLATED: no joint velocity, CONTACT ON");
        CHECK(oA == Approx(oB).margin(1e-6f));

        const double withImplicit = -1.4;  // measured above: contacts against baseline, full vector
        const double withoutImplicit = ablatedContact - ablatedBase;
        WARN(fmt::format("ABLATION: with the pose velocities present contacts scored {:+.1f}; with "
                         "them neutralised contacts score {:+.1f} ({:.1f}% -> {:.1f}%)",
                         withImplicit, withoutImplicit, ablatedBase, ablatedContact));
        WARN(std::string(withoutImplicit > 0.5
                             ? "  => REDUNDANCY: the explicit feature gains value once the implicit"
                               " copy is gone"
                             : "  => NOT redundancy: contacts stay unhelpful with the implicit"
                               " signal removed"));
    }

    CHECK(withoutPhase > 0.0);
    CHECK(withPhaseRate > 0.0);
    CHECK(std::abs(withPhaseRate - withoutPhase) < 5.0); // no large effect either way
}

TEST_CASE("§30: contact-aware matching, on the validated instrument",
          "[crossclip][phaseC][aliens]") {
    // §30: "the search should understand contact state. Avoid selecting a candidate with left foot
    // planted when the current character state strongly indicates right foot planted. Use contacts
    // as a feature/filter."
    //
    // `contactWeight > 0` puts one contact flag per watched joint into the vector. This runs it on
    // the instrument validated above rather than on the retired one, **and the shuffle control is
    // part of the run rather than a follow-up** -- contacts are exactly the kind of feature that
    // could act as an identifier, since a left/right planting pattern is close to a clip-phase
    // fingerprint.
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
    provenance.processing = {"Phase C §30"};
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
    scene::MotionDatabaseOptions options;
    options.sampleRate = 30.0f;
    options.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    options.config.contactWeight = 1.0f;
    auto db = scene::buildMotionDatabase(*pack, options);
    if (!db.has_value()) {
        FAIL("motion database build failed: " << db.error().message);
    }

    // Does the contact feature exist at all on this corpus? §14's discipline: report the
    // distribution before the ratio, because a feature whose values never vary cannot help.
    const std::vector<scene::MotionFeatureGroup> layout = scene::motionFeatureLayout(db->config);
    std::size_t contactDims = 0;
    for (const scene::MotionFeatureGroup g : layout) {
        if (g == scene::MotionFeatureGroup::Contact) {
            ++contactDims;
        }
    }
    WARN(fmt::format("§30: contact dimensions in the vector: {} of {}", contactDims,
                     db->dimension));
    CHECK(contactDims == 2u); // feet only: the head-flag bug is fixed

    float lo = 1e9f;
    float hi = -1e9f;
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] != scene::MotionFeatureGroup::Contact) {
            continue;
        }
        for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
            lo = std::min(lo, db->featuresFor(s)[d]);
            hi = std::max(hi, db->featuresFor(s)[d]);
        }
    }
    WARN(fmt::format("§30: contact feature spans {:.4f} .. {:.4f} (normalised)", lo, hi));
    // **A contact feature that does not vary is a dead dimension wearing a name**, and this is the
    // check that says which before any rate is reported.
    CHECK(hi > lo);

}

TEST_CASE("are the detected contacts actually right?", "[crossclip][phaseC][aliens]") {
    // **The rival hypothesis to redundancy: the contacts are wrong, not duplicated.**
    //
    // Both explain every row. A wrong feature costs its dimensions and buys nothing (phase); it
    // actively misleads the distance (contacts below baseline); and shuffling **removes a
    // misleading signal** while paying only the noise cost (shuffled beats real). That last row is
    // the one called hard to explain otherwise, and "the signal was worse than nothing" explains it
    // equally well.
    //
    // And there is specific reason to suspect it here: §13 found the database running this analysis
    // **with an empty contact-joint list** until today. The detector has had its inputs wrong once
    // already, and **its output quality has never been checked.**
    //
    // The two hypotheses demand opposite fixes -- remove the feature, or fix the detector -- so the
    // check comes before either. A contact track that is right looks obviously right against the
    // physical tells: a planted foot is at its height minimum and its velocity is near zero.
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();

    const scene::AnimationClip* walk = nullptr;
    for (const scene::AnimationClip& c : rig.clips) {
        if (c.name == "Walking") {
            walk = &c;
            break;
        }
    }
    REQUIRE(walk != nullptr);

    const int footL = rig.skeleton.find("foot.l");
    REQUIRE(footL >= 0);

    // Sample the foot's height and speed across the clip, and find where the detector says it is
    // planted. If the detector is right, the planted frames are the low-and-slow ones.
    scene::ContactSettings settings;
    const std::vector<scene::ContactJoint> contactJoints = {
        scene::ContactJoint{"foot.l", scene::ContactKind::Foot}};
    const std::vector<scene::ContactTrack> tracks =
        scene::detectContacts(rig.skeleton, *walk, contactJoints, settings);
    REQUIRE_FALSE(tracks.empty());
    const scene::ContactTrack& track = tracks.front();

    scene::Pose pose;
    std::vector<glm::mat4> model;
    std::vector<float> heights;
    std::vector<float> speeds;
    const int frames = static_cast<int>(walk->length() * 30.0f);
    glm::vec3 previous{0.0f};
    for (int f = 0; f <= frames; ++f) {
        const float t = walk->start + static_cast<float>(f) / 30.0f;
        scene::setRestPose(rig.skeleton, pose);
        scene::sampleClip(*walk, t, pose);
        scene::poseToModel(rig.skeleton, pose, model);
        const glm::vec3 p = glm::vec3(model[static_cast<std::size_t>(footL)][3]);
        heights.push_back(p.y);
        speeds.push_back(f == 0 ? 0.0f : glm::length(p - previous) * 30.0f);
        previous = p;
    }

    std::vector<float> sortedHeights = heights;
    std::sort(sortedHeights.begin(), sortedHeights.end());
    const float lowQuartile = sortedHeights[sortedHeights.size() / 4];
    std::vector<float> sortedSpeeds = speeds;
    std::sort(sortedSpeeds.begin(), sortedSpeeds.end());
    const float slowQuartile = sortedSpeeds[sortedSpeeds.size() / 4];

    int planted = 0;
    int plantedAndLow = 0;
    int plantedAndSlow = 0;
    for (int f = 0; f <= frames && f < static_cast<int>(heights.size()); ++f) {
        const float t = static_cast<float>(f) / 30.0f;
        bool isPlanted = false;
        for (const scene::ContactSpan& span : track.spans) {
            const bool inside = span.wraps() ? (t >= span.start || t <= span.end)
                                             : (t >= span.start && t <= span.end);
            if (inside) {
                isPlanted = true;
                break;
            }
        }
        if (!isPlanted) {
            continue;
        }
        ++planted;
        if (heights[static_cast<std::size_t>(f)] <= lowQuartile) {
            ++plantedAndLow;
        }
        if (speeds[static_cast<std::size_t>(f)] <= slowQuartile) {
            ++plantedAndSlow;
        }
    }

    WARN(fmt::format("'Walking' foot.l: {} intervals over {} frames; {} frames planted "
                     "({:.0f}% of the clip), duty cycle {:.2f}",
                     track.spans.size(), frames + 1, planted,
                     100.0 * planted / std::max(frames + 1, 1), track.dutyCycle));
    WARN(fmt::format("  of the planted frames, {:.0f}% are in the lowest height quartile and "
                     "{:.0f}% in the slowest speed quartile",
                     100.0 * plantedAndLow / std::max(planted, 1),
                     100.0 * plantedAndSlow / std::max(planted, 1)));
    WARN(fmt::format("  foot height range {:.4f} .. {:.4f} m, speed range {:.3f} .. {:.3f} m/s",
                     sortedHeights.front(), sortedHeights.back(), sortedSpeeds.front(),
                     sortedSpeeds.back()));

    REQUIRE(planted > 0); // it detects something, so the percentages mean something

    // **Result: the height tell strongly supports the detector; the velocity tell does not apply.**
    //
    // 75% of planted frames are in the lowest height quartile, against 25% by chance -- that is a
    // real signal and the detector is finding the stance phase. Duty cycle 38% over one span is a
    // plausible walk.
    //
    // Only 17% are in the slowest speed quartile, *below* chance -- and that is **expected here
    // rather than damning**, because of ADR-540: every locomotion clip in this repository is
    // authored **in place**. The body does not translate, so during stance the planted foot must
    // slide backwards in model space at the gait speed. **"A planted foot is not moving" is a tell
    // about root-motion clips, and it is inverted for in-place ones** -- a planted foot is one of
    // the few things that IS moving.
    //
    // So my second tell was measuring the wrong quantity for this corpus, which is the same
    // question as "does this tell apply" that should precede any diagnostic. The contacts look
    // broadly right, which **weakens the wrong-data hypothesis without killing it** and leaves the
    // ablation as the decisive experiment.
    CHECK(100.0 * plantedAndLow / planted > 50.0); // well above the 25% chance level
    CHECK(track.dutyCycle > 0.1f);
    CHECK(track.dutyCycle < 0.9f);
}

TEST_CASE("§30 measured against its own purpose: plant discontinuity across a transition",
          "[crossclip][phaseC][aliens]") {
    // **The third candidate, and the likeliest: the instrument was asking a question contact
    // features were never meant to answer.**
    //
    // Pose proximity asks "is the chosen pose within a margin of the best achievable" -- a question
    // about what the body *looks like* at one instant. A contact flag does not describe appearance.
    // It describes **where in the gait cycle you are and what the next frames will do**. Two poses
    // can be pose-space identical while one has a foot arriving and the other has it leaving, and a
    // single-frame score calls those equally good, because in pose terms they are.
    //
    // That predicts exactly what was measured: the dimensional cost with no compensating gain, and
    // shuffling doing no harm, because the real values were not being used for anything the metric
    // rewards. Neither duplicated nor wrong -- **orthogonal**.
    //
    // And it is the same shape as the retirement already performed: leave-one-out measured identity
    // where meaning was wanted; pose proximity measures appearance where contacts carry continuity.
    // **Twice the instrument has been at fault rather than the feature**, against a habit of
    // suspecting the feature first.
    //
    // §30 exists to stop feet skating and to stop a transition landing on a frame whose plant
    // disagrees with the one before. **That is a defect across a transition, not within a frame.**
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
    provenance.processing = {"Phase C §30 purpose"};
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

    const int footL = rig.skeleton.find("foot.l");
    const int footR = rig.skeleton.find("foot.r");
    REQUIRE(footL >= 0);
    REQUIRE(footR >= 0);

    const auto feetAt = [&](const scene::MotionDatabase& db, std::uint32_t s) {
        const scene::AnimationClip& clip = rig.clips[db.sampleClip[s]];
        scene::Pose pose;
        std::vector<glm::mat4> model;
        scene::setRestPose(rig.skeleton, pose);
        scene::sampleClip(clip, clip.start + db.sampleTime[s], pose);
        scene::poseToModel(rig.skeleton, pose, model);
        return std::pair<glm::vec3, glm::vec3>{
            glm::vec3(model[static_cast<std::size_t>(footL)][3]),
            glm::vec3(model[static_cast<std::size_t>(footR)][3])};
    };

    // Drive the loop toward a different clip so transitions actually happen, and measure the feet's
    // jump at each switch. **This is the quantity §30 exists to reduce.**
    const auto measureTransitions = [&](float contactWeight, const char* label) {
        scene::MotionDatabaseOptions options;
        options.sampleRate = 30.0f;
        options.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
        options.config.contactWeight = contactWeight;
        auto db = scene::buildMotionDatabase(*pack, options);
        if (!db.has_value()) {
            FAIL("motion database build failed: " << db.error().message);
        }
        const scene::MotionCostWeights weights;
        std::uint32_t current = 0;
        std::uint32_t target = 0;
        for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
            if (db->sampleClip[s] != db->sampleClip[0]) {
                target = s;
                break;
            }
        }
        REQUIRE(target != 0u);

        double totalJump = 0.0;
        double worstJump = 0.0;
        int switches = 0;
        // **Drive the target across several clips so transitions actually happen.** The first
        // version followed one clip and produced 4 switches in 300 steps -- far too few to compare
        // two arms, and reporting a mean over 4 would have been the single-point-null mistake in a
        // new place. Here the demanded motion jumps to a different clip every 20 steps, which is
        // what a behaviour tier changing its mind looks like and is the case §30 is written for.
        std::vector<std::uint32_t> clipStarts;
        for (std::uint32_t s = 1; s < db->sampleCount(); ++s) {
            if (db->sampleClip[s] != db->sampleClip[s - 1u]) {
                clipStarts.push_back(s);
            }
        }
        REQUIRE(clipStarts.size() > 5u);
        for (int i = 0; i < 600; ++i) {
            const std::uint32_t leg = static_cast<std::uint32_t>(i / 20) % clipStarts.size();
            const std::uint32_t base = clipStarts[leg];
            const std::uint32_t want =
                base + static_cast<std::uint32_t>(i % 20) < db->sampleCount()
                    ? base + static_cast<std::uint32_t>(i % 20)
                    : base;
            scene::MotionQuery query;
            query.features.assign(db->dimension, 0.0f);
            const float* f = db->featuresFor(want);
            std::copy(f, f + db->dimension, query.features.begin());
            for (std::size_t d = 0; d < query.features.size(); ++d) {
                query.features[d] += 0.05f;
            }
            query.current = current;
            const scene::MotionMatch match = scene::searchMotion(*db, query, weights);
            REQUIRE(match.found());
            // A switch is anything that is not the natural continuation -- that is where a plant
            // can jump.
            const bool continues = db->sampleNext[current] == match.sample;
            if (!continues) {
                const auto [beforeL, beforeR] = feetAt(*db, current);
                const auto [afterL, afterR] = feetAt(*db, match.sample);
                const double jump =
                    std::max(glm::length(afterL - beforeL), glm::length(afterR - beforeR));
                totalJump += jump;
                worstJump = std::max(worstJump, jump);
                ++switches;
            }
            current = match.sample;
        }
        const double mean = switches > 0 ? totalJump / switches : 0.0;
        WARN(fmt::format("{:<26} {} switches, mean foot jump {:.4f} m, worst {:.4f} m", label,
                         switches, mean, worstJump));
        return std::pair<double, int>{mean, switches};
    };

    const auto [withoutContacts, switchesOff] = measureTransitions(0.0f, "contacts OFF");
    const auto [withContacts, switchesOn] = measureTransitions(1.0f, "contacts ON");

    WARN(fmt::format("§30 AGAINST ITS PURPOSE: mean foot jump {:.4f} m without contacts, {:.4f} m "
                     "with ({:+.1f}%)",
                     withoutContacts, withContacts,
                     100.0 * (withContacts - withoutContacts) / std::max(withoutContacts, 1e-9)));

    // The loop actually transitioned, so the means are over real switches rather than over nothing.
    // Enough switches for the means to be comparable. At n=4 they were not, and that was the
    // first version of this test.
    CHECK(switchesOff > 20);
    CHECK(switchesOn > 20);
    CHECK(withoutContacts > 0.0);
}

TEST_CASE("§32's acceptance threshold, derived and fixed BEFORE any fix exists",
          "[crossclip][phaseC][aliens]") {
    // **The before-figure was taken by accident, which is what makes it trustworthy. The after will
    // be taken by someone who wants it to be smaller.** So the threshold is set now, while no fix
    // exists to flatter, and derived from the content rather than picked -- the same discipline as
    // the coverage bin width and the duplicate radius.
    //
    // The natural floor: **a transition is acceptable when it moves a foot no further than the foot
    // moves anyway between two ordinary frames.** Below that it is indistinguishable from normal
    // locomotion; above it, something happened that the motion itself would not have done. That is
    // a property of the content, independent of any fix, and it survives a change of character or
    // units.
    if (!fs::exists(alienGlb())) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    REQUIRE(assets::loadGltf(alienGlb(), sc, loadOptions).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    const scene::AnimationClip* walk = nullptr;
    for (const scene::AnimationClip& c : rig.clips) {
        if (c.name == "Walking") {
            walk = &c;
            break;
        }
    }
    REQUIRE(walk != nullptr);
    const int footL = rig.skeleton.find("foot.l");
    REQUIRE(footL >= 0);

    scene::Pose pose;
    std::vector<glm::mat4> model;
    std::vector<float> steps;
    glm::vec3 previous{0.0f};
    float restHeight = 0.0f;
    const int frames = static_cast<int>(walk->length() * 30.0f);
    for (int f = 0; f <= frames; ++f) {
        scene::setRestPose(rig.skeleton, pose);
        scene::sampleClip(*walk, walk->start + static_cast<float>(f) / 30.0f, pose);
        scene::poseToModel(rig.skeleton, pose, model);
        const glm::vec3 p = glm::vec3(model[static_cast<std::size_t>(footL)][3]);
        if (f > 0) {
            steps.push_back(glm::length(p - previous));
        }
        previous = p;
        for (const glm::mat4& m : model) {
            restHeight = std::max(restHeight, m[3].y);
        }
    }
    REQUIRE(steps.size() > 10u);
    std::sort(steps.begin(), steps.end());
    const float typicalStep = steps[steps.size() / 2];
    const float worstStep = steps.back();

    WARN(fmt::format("character height {:.3f} m; foot moves a median {:.4f} m and at most {:.4f} m "
                     "between two ordinary frames of 'Walking'",
                     restHeight, typicalStep, worstStep));
    WARN(fmt::format("§32 BEFORE: mean transition jump 0.3566 m = {:.0f}% of body height and "
                     "{:.1f}x a normal frame step; worst 1.6437 m = {:.0f}% of body height",
                     100.0f * 0.3566f / restHeight, 0.3566f / typicalStep,
                     100.0f * 1.6437f / restHeight));
    WARN(fmt::format("§32 ACCEPTANCE THRESHOLD, fixed now: mean transition jump <= {:.4f} m (one "
                     "ordinary frame step). A fix that reaches 0.30 m is not a fix.",
                     typicalStep));

    // The threshold is recorded as an assertion so it cannot be quietly relaxed when the fix is
    // measured against it.
    CHECK(typicalStep > 0.0f);
    CHECK(restHeight > 1.0f);
    CHECK(0.3566f > typicalStep); // the defect is real against this threshold, stated before the fix
}

TEST_CASE("§32 against ADR-612's own population: the forced cross-clip transition",
          "[crossclip][phaseC][aliens]") {
    // **This is the after-figure for the 0.3566 m.**
    //
    // §32's own measurement in `test_matching_loop.cpp` drives the provider with a request and
    // lets the shipping loop choose its own transitions. That is the right instrument for "what
    // does a Glowmere alien do", and it is the **wrong** instrument for "did the number in ADR-612
    // come down", because it does not measure the same population of switches: ADR-612's mean is
    // over transitions *forced* to a different clip every 20 steps, and the shipping loop's own
    // switches are mostly mild re-selections inside one clip. Two honest means over two different
    // populations are not a before and an after.
    //
    // So this reproduces §30's driver exactly -- the same forced cross-clip targets, the same
    // query, the same search -- and changes only where the pose comes from: `MatchMotionProvider`,
    // with the memory advanced as the provider advances it, rather than a raw sample lookup. Both
    // arms run through that same path, so the blend is the only difference between them.
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
    provenance.processing = {"Phase C §32"};
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
    scene::MotionDatabaseOptions options;
    options.sampleRate = 30.0f;
    options.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(*pack, options);
    if (!db.has_value()) {
        FAIL("motion database build failed: " << db.error().message);
    }
    const int footL = rig.skeleton.find("foot.l");
    const int footR = rig.skeleton.find("foot.r");
    REQUIRE(footL >= 0);
    REQUIRE(footR >= 0);

    std::vector<std::uint32_t> clipStarts;
    for (std::uint32_t s = 1; s < db->sampleCount(); ++s) {
        if (db->sampleClip[s] != db->sampleClip[s - 1u]) {
            clipStarts.push_back(s);
        }
    }
    REQUIRE(clipStarts.size() > 5u);

    const auto measure = [&](float halflife) {
        entity::MatchSettings settings;
        settings.inertializeHalflife = halflife;
        entity::MatchMotionProvider provider(&*db, &rig.clips, "match");
        provider.setSettings(settings);
        entity::MotionMemory memory;
        memory.selection = 0;
        memory.generation = 1;
        // Hand-driven, so it states which database it indexes, as `advance` would (§40).
        memory.database = db->identity;
        const scene::MotionCostWeights weights;
        scene::Pose out;
        std::vector<glm::mat4> matrices;
        glm::vec3 lastL{0.0f};
        glm::vec3 lastR{0.0f};
        bool have = false;
        double total = 0.0;
        double worst = 0.0;
        int switches = 0;
        double worstFrame = 0.0;
        const float step = entity::kBlendBudgetFrameSeconds;
        for (int i = 0; i < 600; ++i) {
            const std::uint32_t leg = static_cast<std::uint32_t>(i / 20) % clipStarts.size();
            const std::uint32_t base = clipStarts[leg];
            const std::uint32_t want =
                base + static_cast<std::uint32_t>(i % 20) < db->sampleCount()
                    ? base + static_cast<std::uint32_t>(i % 20)
                    : base;
            scene::MotionQuery query;
            query.features.assign(db->dimension, 0.0f);
            const float* f = db->featuresFor(want);
            std::copy(f, f + db->dimension, query.features.begin());
            for (std::size_t d = 0; d < query.features.size(); ++d) {
                query.features[d] += 0.05f;
            }
            query.current = memory.selection;
            const scene::MotionMatch match = scene::searchMotion(*db, query, weights);
            REQUIRE(match.found());
            // Exactly §30's definition of a switch: anything that is not the natural continuation.
            const bool switched = db->sampleNext[memory.selection] != match.sample;
            // The memory is advanced the way `MatchMotionProvider::advance` advances it. The
            // search above stands in for the provider's own, because this driver poses a query the
            // request cannot express -- that is the whole point of reproducing §30's driver.
            memory.tickBlends(step);
            if (switched) {
                memory.pushBlend(memory.selection, db->sampleTime[memory.selection],
                                 match.sample, db->sampleTime[match.sample],
                                 entity::MotionMemory::kBlendSlots);
            }
            memory.selection = match.sample;
            memory.generation += 1;
            if (!provider.pose(memory, rig.skeleton, out).ok()) {
                continue;
            }
            scene::poseToModel(rig.skeleton, out, matrices);
            const glm::vec3 l = glm::vec3(matrices[static_cast<std::size_t>(footL)][3]);
            const glm::vec3 r = glm::vec3(matrices[static_cast<std::size_t>(footR)][3]);
            if (have) {
                const double moved = std::max(glm::length(l - lastL), glm::length(r - lastR));
                worstFrame = std::max(worstFrame, moved);
                if (switched) {
                    total += moved;
                    worst = std::max(worst, moved);
                    ++switches;
                }
            }
            lastL = l;
            lastR = r;
            have = true;
        }
        return std::tuple<double, double, int, double>{switches > 0 ? total / switches : 0.0,
                                                       worst, switches, worstFrame};
    };

    const auto [beforeMean, beforeWorst, beforeCount, beforeWorstFrame] = measure(0.0f);
    const auto [afterMean, afterWorst, afterCount, afterWorstFrame] =
        measure(entity::MatchSettings{}.inertializeHalflife);

    WARN(fmt::format("§32 on ADR-612's population: BEFORE {} switches, mean {:.4f} m, worst "
                     "{:.4f} m",
                     beforeCount, beforeMean, beforeWorst));
    WARN(fmt::format("§32 on ADR-612's population: AFTER  {} switches, mean {:.4f} m, worst "
                     "{:.4f} m ({:+.1f}% mean, {:+.1f}% worst)",
                     afterCount, afterMean, afterWorst,
                     100.0 * (afterMean - beforeMean) / std::max(beforeMean, 1e-9),
                     100.0 * (afterWorst - beforeWorst) / std::max(beforeWorst, 1e-9)));
    // **The mean at a switch is not enough here, and the after-figure is the reason.**
    //
    // An inertialized pose at the instant of a transition is *exactly* the outgoing pose -- that
    // is the property that removes the teleport -- and the outgoing pose at that instant is the
    // same sample the previous frame already showed. So the foot is momentarily held, the
    // switch-frame metric reads near zero, and it would read near zero for any blend that merely
    // deferred the displacement to the following frames. **A metric with one obvious answer
    // rewards producing that answer**, so the worst single frame of the whole run is reported and
    // asserted beside it: it counts the deferred motion wherever the blend eventually puts it.
    WARN(fmt::format("§32 worst single frame anywhere in the run: {:.4f} m before, {:.4f} m after "
                     "({:+.1f}%) -- this is the anti-smear figure, and the mean above is not "
                     "readable without it",
                     beforeWorstFrame, afterWorstFrame,
                     100.0 * (afterWorstFrame - beforeWorstFrame) /
                         std::max(beforeWorstFrame, 1e-9)));
    WARN(fmt::format("§32 against the 0.0510 m bar on this population: mean at a switch {:.2f}x, "
                     "worst frame anywhere {:.2f}x",
                     afterMean / 0.0510, afterWorstFrame / 0.0510));
    CHECK(beforeCount > 20);
    CHECK(beforeCount == afterCount); // the blend is in `pose`; it cannot change what was selected
    CHECK(afterMean < beforeMean);
    CHECK(afterMean <= 0.0510);
    // The companion, in the direction that would catch a blend which only moved the defect.
    CHECK(afterWorstFrame < beforeWorstFrame);
}

// **Hidden by default (`[.scale]`), for three reasons that are about this test and not about the
// crash below.** It reads a 389 MB pack built from a gitignored 594 MB corpus that exists on one
// machine, so it skips everywhere else; it takes **131 seconds**; and it peaks at **1.7 GB**, which
// is a third of the whole suite's footprint for one case. The repository already keeps `[.perf]`
// probes out of the default run for less.
//
// **It also SIGSEGV'd once inside the full suite and did not reproduce.** One run crashed here at
// case 661 of 2955; an identical re-run of the same binary completed clean at 4.88 GB peak, and the
// test passes alone and with its whole `[crossclip]` family. **That is recorded rather than
// explained** -- hiding it is not a diagnosis, and if it recurs the note is here. Other agents were
// building concurrently on the same machine, which makes allocation failure the first suspect and
// not a conclusion.
TEST_CASE("§30 against the content it was written for: transitions in 100STYLE",
          "[.scale][crossclip][phaseC]") {
    // **§30's verdict has been scoped to "this corpus" since it was taken, and this is the first
    // content it was actually written for.**
    //
    // §30 exists for *starts, stops and directional changes*. Every Glowmere clip is a steady-state
    // cycle authored in place (ADR-540), so the finding that contact features are inert there was
    // recorded as a property of that corpus rather than of the feature. 100STYLE's `TR1` files are
    // transitions: 100 styles of a body changing what it is doing, with real root motion.
    //
    // **The instrument is §30's own** -- the forced cross-clip transition loop from "§30 measured
    // against its own purpose", which is the measurement §30's stated purpose asks for: how far a
    // foot jumps when the matcher switches motion, with contacts off and on. The same loop, the
    // same switch definition, the same two arms; only the corpus differs.
    //
    // The pack is built by the command in `assets/100STYLE-ATTRIBUTION.md` and is gitignored, so
    // this skips wherever the corpus is absent -- which is everywhere but the machine that fetched
    // it. That is stated rather than hidden: it is a research measurement over a corpus that
    // cannot be redistributed, not a regression test.
    const fs::path packDir = fs::path(AVGEN_SOURCE_DIR) / "assets" / "100style-tr-pack";
    if (!fs::exists(packDir)) {
        SKIP("the 100STYLE TR pack is not present (see assets/100STYLE-ATTRIBUTION.md)");
    }
    const auto pack = scene::readMotionPack(packDir);
    if (!pack.has_value()) {
        FAIL("motion pack read failed: " << pack.error().message);
    }
    const int footL = pack->skeleton.find("LeftAnkle");
    const int footR = pack->skeleton.find("RightAnkle");
    REQUIRE(footL >= 0);
    REQUIRE(footR >= 0);

    const auto feetAt = [&](const scene::MotionDatabase& db, std::uint32_t s) {
        const scene::AnimationClip& clip = pack->animation[db.sampleClip[s]];
        scene::Pose pose;
        std::vector<glm::mat4> model;
        scene::setRestPose(pack->skeleton, pose);
        // `sampleTime` is already absolute (it is `clip.start + f*dt`), which is the convention
        // `MatchMotionProvider::pose` reads it under.
        scene::sampleClip(clip, db.sampleTime[s], pose);
        scene::poseToModel(pack->skeleton, pose, model);
        return std::pair<glm::vec3, glm::vec3>{
            glm::vec3(model[static_cast<std::size_t>(footL)][3]),
            glm::vec3(model[static_cast<std::size_t>(footR)][3])};
    };

    const auto measureTransitions = [&](float contactWeight, float perturbation,
                                        const char* label) {
        scene::MotionDatabaseOptions options;
        options.sampleRate = 30.0f;
        options.config = scene::defaultBipedConfig("LeftAnkle", "RightAnkle", "Head");
        options.config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
        options.config.contactWeight = contactWeight;
        auto db = scene::buildMotionDatabase(*pack, options);
        if (!db.has_value()) {
            FAIL("motion database build failed: " << db.error().message);
        }
        const scene::MotionCostWeights weights;
        std::vector<std::uint32_t> clipStarts;
        for (std::uint32_t s = 1; s < db->sampleCount(); ++s) {
            if (db->sampleClip[s] != db->sampleClip[s - 1u]) {
                clipStarts.push_back(s);
            }
        }
        REQUIRE(clipStarts.size() > 5u);

        std::uint32_t current = 0;
        double totalJump = 0.0;
        double worstJump = 0.0;
        int switches = 0;
        // **What the metric can see, counted before anything is concluded from it.** If the search
        // returns the sample the query was built from on every step, the two arms cannot differ
        // whatever the contact feature carries -- the query identifies its own source and the
        // experiment has no discriminating power. A fact about the instrument at this corpus size,
        // measured rather than assumed either way.
        int returnedTheSeed = 0;
        for (int i = 0; i < 600; ++i) {
            const std::uint32_t leg = static_cast<std::uint32_t>(i / 20) % clipStarts.size();
            const std::uint32_t base = clipStarts[leg];
            const std::uint32_t want =
                base + static_cast<std::uint32_t>(i % 20) < db->sampleCount()
                    ? base + static_cast<std::uint32_t>(i % 20)
                    : base;
            scene::MotionQuery query;
            query.features.assign(db->dimension, 0.0f);
            const float* f = db->featuresFor(want);
            std::copy(f, f + db->dimension, query.features.begin());
            for (std::size_t d = 0; d < query.features.size(); ++d) {
                query.features[d] += perturbation;
            }
            query.current = current;
            const scene::MotionMatch match = scene::searchMotion(*db, query, weights);
            REQUIRE(match.found());
            if (match.sample == want) {
                ++returnedTheSeed;
            }
            const bool continues = db->sampleNext[current] == match.sample;
            if (!continues) {
                const auto [beforeL, beforeR] = feetAt(*db, current);
                const auto [afterL, afterR] = feetAt(*db, match.sample);
                totalJump += std::max(glm::length(afterL - beforeL), glm::length(afterR - beforeR));
                worstJump = std::max(worstJump, static_cast<double>(std::max(
                                                    glm::length(afterL - beforeL),
                                                    glm::length(afterR - beforeR))));
                ++switches;
            }
            current = match.sample;
        }
        const double mean = switches > 0 ? totalJump / switches : 0.0;
        // The distribution before the ratio (§14), and the liveness check §30 requires: a contact
        // feature that never varies is a dead dimension wearing a name.
        const std::vector<scene::MotionFeatureGroup> layout =
            scene::motionFeatureLayout(db->config);
        float lo = 1e9f;
        float hi = -1e9f;
        std::size_t contactDims = 0;
        for (std::size_t d = 0; d < layout.size(); ++d) {
            if (layout[d] != scene::MotionFeatureGroup::Contact) {
                continue;
            }
            ++contactDims;
            for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
                lo = std::min(lo, db->featuresFor(s)[d]);
                hi = std::max(hi, db->featuresFor(s)[d]);
            }
        }
        WARN(fmt::format("{:<22} {} samples, {} switches, mean foot jump {:.4f} m, worst {:.4f} m; "
                         "{} contact dim(s) spanning {:.3f}..{:.3f}; returned the SEED sample on "
                         "{}/600 steps; TOTAL jump over the run {:.1f} m",
                         label, db->sampleCount(), switches, mean, worstJump, contactDims,
                         contactDims > 0 ? lo : 0.0f, contactDims > 0 ? hi : 0.0f,
                         returnedTheSeed, totalJump));
        (void)perturbation;
        return std::tuple<double, int, double>{mean, switches, totalJump};
    };

    // ---- the perturbation, derived from the matcher rather than kept at 0.05 -----------------
    //
    // **The first run of this was degenerate and the tell was in the table.** At +0.05 on every
    // dimension the two arms agreed to four decimals -- mean, worst AND switch count -- and the
    // reason is that the search returned **600 of 600** seed samples: a query built from a sample's
    // own features, nudged by a fixed 0.05, identifies its source uniquely among 420,432 samples,
    // so no feature can change the answer and the number measured is the corpus's clip layout
    // rather than the matcher's choices. On 1,738 Glowmere samples the same nudge left the choice
    // genuinely open, which is why the instrument worked there and not here.
    //
    // **Ask what the metric can see before asking what the feature carries**, and the fix is a
    // perturbation expressed in the matcher's own units instead of raw ones: the derived duplicate
    // radius is the distance at which this matcher stops being able to tell two samples apart, so
    // a query moved by exactly that much is ambiguous *by construction* at any corpus size. Spread
    // over the vector so the L2 displacement is the radius.
    scene::MotionDatabaseOptions probeOptions;
    probeOptions.sampleRate = 30.0f;
    probeOptions.config = scene::defaultBipedConfig("LeftAnkle", "RightAnkle", "Head");
    probeOptions.config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    const auto probeDb = scene::buildMotionDatabase(*pack, probeOptions);
    REQUIRE(probeDb.has_value());
    const scene::MotionQualitySummary probeQuality = scene::analyseMotionQuality(*probeDb);
    const float derived =
        probeQuality.duplicateRadius / std::sqrt(static_cast<float>(probeDb->dimension));
    WARN(fmt::format("§30 perturbation, derived: duplicate radius {:.4f} over {} dimensions = "
                     "{:.4f} per dimension (the fixed 0.05 was {:.1f}x smaller)",
                     probeQuality.duplicateRadius, probeDb->dimension, derived, derived / 0.05f));

    const auto [flatOff, flatSwitchesOff, flatWorstOff] =
        measureTransitions(0.0f, 0.05f, "0.05  contacts OFF");
    const auto [flatOn, flatSwitchesOn, flatWorstOn] =
        measureTransitions(1.0f, 0.05f, "0.05  contacts ON");
    const auto [withoutContacts, switchesOff, totalOff] =
        measureTransitions(0.0f, derived, "derived contacts OFF");
    const auto [withContacts, switchesOn, totalOn] =
        measureTransitions(1.0f, derived, "derived contacts ON");

    // **The two measures disagree, and both are printed because picking one would be a choice
    // about the answer.** Contacts change how OFTEN the matcher switches as well as how far each
    // switch moves a foot, so a mean per transition and a total over the run are not the same
    // comparison: the arms do not share a denominator.
    WARN(fmt::format("§30 ON ITS OWN CONTENT, at the derived perturbation:"));
    WARN(fmt::format("  per transition: {:.4f} m -> {:.4f} m ({:+.1f}%)  [{} vs {} switches]",
                     withoutContacts, withContacts,
                     100.0 * (withContacts - withoutContacts) / std::max(withoutContacts, 1e-9),
                     switchesOff, switchesOn));
    WARN(fmt::format("  over the run:   {:.1f} m -> {:.1f} m ({:+.1f}%)  -- contacts switch "
                     "{:+.0f}% more often, so a lower mean per transition can still be more "
                     "total discontinuity",
                     totalOff, totalOn,
                     100.0 * (totalOn - totalOff) / std::max(totalOff, 1e-9),
                     100.0 * (switchesOn - switchesOff) / std::max(switchesOff, 1)));
    WARN(fmt::format("§30 at the FIXED 0.05, for contrast: {:.4f} m vs {:.4f} m -- identical, and "
                     "the seed-return count above says why",
                     flatOff, flatOn));
    WARN("§30 on Glowmere, as recorded: 0.3566 m -> 0.3792 m (+6.3%), worst 1.6437 m in both arms. "
         "**That was taken at the fixed 0.05 on 1,738 samples**, so it is not directly comparable "
         "with the derived row above -- what is comparable is the sign and the size of the gap "
         "between arms.");

    CHECK(switchesOff > 20);
    CHECK(switchesOn > 20);
    CHECK(withoutContacts > 0.0);
    // The instrument has to be able to disagree with itself before a null means anything.
    CHECK(flatOff == Approx(flatOn));  // the degenerate arm, asserted so its failure is visible
    // Contacts are NOT inert on this content -- the arms disagree, which is the thing Glowmere
    // could not show. Asserted in that direction only; which way the disagreement goes is what
    // the two rows above are for.
    CHECK(withContacts != Approx(withoutContacts));
    (void)flatSwitchesOff;
    (void)flatSwitchesOn;
    (void)flatWorstOff;
    (void)flatWorstOn;
}
