// Offline contact and phase extraction (ADR-546).
//
// **The standing rule (Phase A brief §2).** A detector that reports contact everywhere passes any
// test that only asks "did you find the plant". So every arm here is paired with its opposite: a
// joint that never settles must yield NO contact, a hesitation at the bottom of a swing must not
// become a second plant, and a clip with no cycle must say it has none. The synthetic fixtures are
// built so that the right answer is known in closed form, and the last test runs the whole thing
// against the real Glowmere pack, where the right answer was established by measurement.
//
// The arms:
//
//   found         a joint that rests, lifts and returns gives ONE span at the known instants
//   never         a joint in constant motion gives NO span      (a detector that always says yes fails)
//   always        a joint that never moves gives one span covering the clip
//   hesitation    a pause at the bottom of the swing does NOT become a second plant
//   scale         the same motion at 100x scale gives the same spans (the band is relative)
//   wrap          a contact straddling the loop point is ONE wrapped span, not two
//   cycle         two plants give a cycle, phase 0 at each plant, rising monotonically between
//   one-plant     a looping clip with one plant is cyclic over its own length
//   acyclic       a one-shot says so, and still hands out a usable phase
//   at()          phase interpolation takes the short way round the wrap
//   alien         Walking, Running and Idle on the real rig

#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/motion_analysis.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <cmath>
#include <vector>

using namespace avgen::scene;
using Catch::Approx;

namespace {

// A two-joint rig: a parentless root, and a "foot" hanging off it. Enough for the analysis, which
// only ever asks where a joint is.
Skeleton twoJointRig() {
    Skeleton sk;
    sk.name = "probe";
    sk.joints.push_back(Joint{"root", -1, Transform{}});
    sk.joints.push_back(Joint{"foot", 0, Transform{}});
    sk.palette = {0, 1};
    sk.inverseBind = {glm::mat4(1.0f), glm::mat4(1.0f)};
    return sk;
}

// A clip that animates the foot's translation through `heights`, one key per 1/30 s, at a fixed
// horizontal offset so the joint is somewhere sensible.
AnimationClip footHeightClip(std::string name, const std::vector<float>& heights, float scale = 1.0f) {
    AnimationClip clip;
    clip.name = std::move(name);
    AnimationChannel channel;
    channel.joint = 1;
    channel.path = AnimationPath::Translation;
    channel.interpolation = Interpolation::Linear;
    for (std::size_t i = 0; i < heights.size(); ++i) {
        channel.times.push_back(static_cast<float>(i) / 30.0f);
        channel.values.emplace_back(0.0f, heights[i] * scale, 0.0f, 0.0f);
    }
    clip.start = channel.times.front();
    clip.duration = channel.times.back();
    clip.channels.push_back(std::move(channel));
    return clip;
}

const std::array<ContactJoint, 1> kFoot{{{"foot", ContactKind::Foot}}};

} // namespace

TEST_CASE("a joint that rests, lifts and returns gives exactly one contact", "[motion][contacts]") {
    // Samples 0-5 down, 6-11 up, 12-17 down again. With looping off, that is two separate rests --
    // at the head and the tail -- and the arm asserts both, at the instants they really occur.
    std::vector<float> h;
    for (int i = 0; i < 6; ++i) { h.push_back(0.0f); }
    for (int i = 0; i < 6; ++i) { h.push_back(0.5f); }
    for (int i = 0; i < 6; ++i) { h.push_back(0.0f); }
    const Skeleton sk = twoJointRig();
    const AnimationClip clip = footHeightClip("rest-lift-rest", h);
    ContactSettings settings;
    settings.looping = false;

    const std::vector<ContactTrack> tracks = detectContacts(sk, clip, kFoot, settings);
    REQUIRE(tracks.size() == 1);
    const ContactTrack& t = tracks.front();
    CHECK(t.jointIndex == 1);
    REQUIRE(t.spans.size() == 2);
    CHECK(t.spans[0].start == Approx(0.0f).margin(1e-4));
    CHECK(t.spans[0].end < 0.2f);                 // it released before the lift completed
    CHECK(t.spans[1].start > 0.35f);              // and re-planted after it
    CHECK(t.spans[1].end == Approx(17.0f / 30.0f).margin(1e-4));
    // It is not in contact the whole time, which is the half a "found the plant" assertion misses.
    CHECK(t.dutyCycle > 0.3f);
    CHECK(t.dutyCycle < 0.8f);
}

TEST_CASE("a joint in constant motion is never in contact", "[motion][contacts]") {
    // THE ADVERSARIAL ARM. A detector that reports contact whenever a joint is near its lowest --
    // which every joint is, at some point -- passes every other test in this file and fails this.
    std::vector<float> h;
    for (int i = 0; i < 30; ++i) {
        h.push_back(static_cast<float>(i) * 0.1f); // rising steadily, never settling
    }
    const Skeleton sk = twoJointRig();
    const AnimationClip clip = footHeightClip("rising", h);
    ContactSettings settings;
    settings.looping = false;
    const std::vector<ContactTrack> tracks = detectContacts(sk, clip, kFoot, settings);
    REQUIRE(tracks.size() == 1);
    CHECK(tracks.front().spans.empty());
    CHECK(tracks.front().dutyCycle == 0.0f);
}

TEST_CASE("a joint that never moves is in contact throughout", "[motion][contacts]") {
    const Skeleton sk = twoJointRig();
    const AnimationClip clip = footHeightClip("still", std::vector<float>(30, 0.25f));
    ContactSettings settings;
    settings.looping = false;
    const std::vector<ContactTrack> tracks = detectContacts(sk, clip, kFoot, settings);
    REQUIRE(tracks.front().spans.size() == 1);
    CHECK(tracks.front().dutyCycle == Approx(1.0f));
}

TEST_CASE("a hesitation at the bottom of a swing is not a second plant", "[motion][contacts]") {
    // The false positive the first implementation actually produced on `Walking`: the foot dips,
    // hesitates a little above the ground, lifts again, and only later makes its real plant. An
    // absolute height band swallows both. A band measured as a fraction of the joint's own travel
    // does not.
    std::vector<float> h{0.50f, 0.30f, 0.17f, 0.17f, 0.17f, 0.19f, 0.30f, 0.45f, 0.30f, 0.15f};
    for (int i = 0; i < 8; ++i) { h.push_back(0.00f); } // the real plant
    for (int i = 0; i < 6; ++i) { h.push_back(0.20f + static_cast<float>(i) * 0.08f); }
    const Skeleton sk = twoJointRig();
    const AnimationClip clip = footHeightClip("hesitate", h);
    ContactSettings settings;
    settings.looping = false;

    const std::vector<ContactTrack> tracks = detectContacts(sk, clip, kFoot, settings);
    REQUIRE(tracks.size() == 1);
    INFO("spans: " << tracks.front().spans.size());
    REQUIRE(tracks.front().spans.size() == 1);
    // And it is the REAL plant -- the later, lower one -- not the hesitation.
    CHECK(tracks.front().spans.front().start > 0.25f);
}

TEST_CASE("the height band is relative, so scale does not change the answer", "[motion][contacts]") {
    std::vector<float> h{0.50f, 0.30f, 0.17f, 0.17f, 0.19f, 0.30f, 0.45f, 0.15f};
    for (int i = 0; i < 8; ++i) { h.push_back(0.00f); }
    for (int i = 0; i < 6; ++i) { h.push_back(0.30f); }
    const Skeleton sk = twoJointRig();
    ContactSettings settings;
    settings.looping = false;

    const auto spansAt = [&](float scale) {
        const AnimationClip clip = footHeightClip("scaled", h, scale);
        return detectContacts(sk, clip, kFoot, settings).front().spans;
    };
    const std::vector<ContactSpan> small = spansAt(1.0f);
    const std::vector<ContactSpan> large = spansAt(100.0f); // a rig authored in centimetres
    REQUIRE(small.size() == large.size());
    REQUIRE_FALSE(small.empty()); // it found something at all, or this test compares two emptinesses
    for (std::size_t i = 0; i < small.size(); ++i) {
        CHECK(small[i].start == Approx(large[i].start).margin(1e-5));
        CHECK(small[i].end == Approx(large[i].end).margin(1e-5));
    }
}

TEST_CASE("a contact across the loop point is one wrapped span, not two", "[motion][contacts]") {
    // Measured on the real pack: `Running`'s right foot is planted from 0.633 s through 0.033 s of
    // the next lap. Read linearly that is two contacts at opposite ends of the clip, and a
    // one-cycle clip then looks like a two-cycle one.
    std::vector<float> h;
    for (int i = 0; i < 4; ++i) { h.push_back(0.0f); }  // the tail of the stance
    for (int i = 0; i < 12; ++i) { h.push_back(0.6f); } // the swing
    for (int i = 0; i < 4; ++i) { h.push_back(0.0f); }  // the head of the same stance
    const Skeleton sk = twoJointRig();
    const AnimationClip clip = footHeightClip("wrapped", h);

    SECTION("with looping on it is one span that wraps") {
        ContactSettings settings;
        settings.looping = true;
        const std::vector<ContactTrack> tracks = detectContacts(sk, clip, kFoot, settings);
        REQUIRE(tracks.front().spans.size() == 1);
        const ContactSpan& span = tracks.front().spans.front();
        CHECK(span.wraps());
        CHECK(span.start > span.end);
        // Eight authored samples of stance, of which the detector keeps six: the central-difference
        // speed test correctly rejects the first sample of the tail and the last of the head,
        // because at those instants the foot IS moving -- it is the frame the lift begins on. Six
        // samples at 30 Hz is 0.1333 s, and asserting the authored eight would be asserting that
        // the detector cannot tell a transition frame from a planted one.
        CHECK(span.duration() == Approx(4.0f / 30.0f).margin(0.02));
    }

    SECTION("with looping off it is honestly two") {
        ContactSettings settings;
        settings.looping = false;
        const std::vector<ContactTrack> tracks = detectContacts(sk, clip, kFoot, settings);
        CHECK(tracks.front().spans.size() == 2);
    }
}

TEST_CASE("two plants give a cycle anchored on them", "[motion][phase]") {
    ContactTrack ref;
    ref.joint = "foot";
    ref.jointIndex = 1;
    ref.spans.push_back(ContactSpan{0.20f, 0.40f, 1.0f});
    ref.spans.push_back(ContactSpan{0.70f, 0.90f, 1.0f});
    const std::array<ContactTrack, 1> tracks{ref};
    ContactSettings settings;
    const PhaseTrack phase = extractPhase(tracks, 0, 1.0f, settings);

    CHECK(phase.cyclic);
    CHECK(phase.cycleSeconds == Approx(0.5f).margin(1e-4));
    CHECK(phase.cycleVariance == Approx(0.0f).margin(1e-4));
    // Phase is 0 at each plant...
    CHECK(phase.at(0.20f) == Approx(0.0f).margin(0.02));
    CHECK(phase.at(0.70f) == Approx(0.0f).margin(0.02));
    // ...and rises monotonically between them, which a phase that is merely "0 at the plants" does
    // not have to do.
    float previous = phase.at(0.20f);
    for (float t = 0.22f; t < 0.69f; t += 0.02f) {
        const float now = phase.at(t);
        INFO(t);
        CHECK(now > previous);
        previous = now;
    }
    CHECK(previous > 0.9f); // it got all the way round before wrapping
    // The landmarks name what they came from.
    REQUIRE(phase.landmarks.size() == 4);
    CHECK(phase.landmarks.front().kind == PhaseLandmarkKind::Plant);
    CHECK(phase.landmarks.front().contactTrack == 0);
}

TEST_CASE("a looping clip with one plant is cyclic over its own length", "[motion][phase]") {
    // A one-cycle walk loop plants each foot once. Reading that as "not enough plants to have a
    // cycle" throws away the phase of exactly the clips phase matching is for.
    ContactTrack ref;
    ref.jointIndex = 1;
    ref.spans.push_back(ContactSpan{0.30f, 0.67f, 1.033f});
    const std::array<ContactTrack, 1> tracks{ref};

    SECTION("looping") {
        ContactSettings settings;
        settings.looping = true;
        const PhaseTrack phase = extractPhase(tracks, 0, 1.033f, settings);
        CHECK(phase.cyclic);
        CHECK(phase.cycleSeconds == Approx(1.033f).margin(1e-3));
        CHECK(phase.at(0.30f) == Approx(0.0f).margin(0.02));
        CHECK(phase.at(0.30f + 0.5f * 1.033f) == Approx(0.5f).margin(0.03));
    }

    SECTION("not looping -- a one-shot has no cycle and says so") {
        ContactSettings settings;
        settings.looping = false;
        const PhaseTrack phase = extractPhase(tracks, 0, 1.033f, settings);
        CHECK_FALSE(phase.cyclic);
        CHECK(phase.cycleSeconds == 0.0f);
        // ...and still hands out a usable phase, rising across the clip.
        CHECK(phase.at(0.0f) < phase.at(0.5f));
        CHECK(phase.at(0.5f) < phase.at(1.0f));
    }
}

TEST_CASE("a clip with no contacts still has a phase", "[motion][phase]") {
    const std::array<ContactTrack, 1> tracks{ContactTrack{}};
    const PhaseTrack phase = extractPhase(tracks, 0, 2.0f, {});
    CHECK_FALSE(phase.cyclic);
    CHECK(phase.at(0.0f) == Approx(0.0f).margin(1e-3));
    CHECK(phase.at(2.0f) > 0.99f);
    CHECK(phase.at(1.0f) == Approx(0.5f).margin(0.02));
}

TEST_CASE("phase interpolation takes the short way round the wrap", "[motion][phase]") {
    PhaseTrack phase;
    phase.sampleRate = 10.0f;
    phase.phase = {0.90f, 0.95f, 0.00f, 0.05f}; // wraps between samples 1 and 2
    // Half way between 0.95 and 0.00 is 0.975, not 0.475 -- the long way round is the bug this
    // catches, and it is the one that makes a phase-matched transition pick the wrong frame.
    CHECK(phase.at(0.15f) == Approx(0.975f).margin(0.01));
    CHECK(phase.at(0.25f) == Approx(0.025f).margin(0.01));
}

TEST_CASE("contacts and phase on the real Glowmere alien", "[motion][contacts][phase][aliens]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    namespace fs = std::filesystem;
    const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
    if (!fs::exists(path)) {
        SKIP("assets are not present");
    }
    avgen::scene::Scene scene;
    avgen::assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(avgen::assets::loadGltf(path, scene, options));
    const SkinnedRig& rig = scene.rigs.front();
    const Skeleton& sk = rig.skeleton;
    const std::array<ContactJoint, 2> feet{{{"foot.l", ContactKind::Foot}, {"foot.r", ContactKind::Foot}}};

    const auto analyse = [&](const char* name) {
        const int i = rig.findClip(name);
        REQUIRE(i >= 0);
        return analyseClip(sk, rig.clips[static_cast<std::size_t>(i)], feet, 0, {});
    };

    SECTION("Walking is one cycle: each foot plants once, half a cycle apart") {
        const ClipAnalysis a = analyse("Walking");
        REQUIRE(a.contacts.size() == 2);
        CHECK(a.contacts[0].spans.size() == 1);
        CHECK(a.contacts[1].spans.size() == 1);
        // Both feet spend a real fraction of the cycle down -- and NOT all of it, which is the
        // arm that separates a walk from a stand.
        for (const ContactTrack& t : a.contacts) {
            INFO(t.joint);
            CHECK(t.dutyCycle > 0.2f);
            CHECK(t.dutyCycle < 0.8f);
        }
        CHECK(a.phase.cyclic);
        CHECK(a.phase.cycleSeconds == Approx(a.length).margin(1e-3));
        // The right foot's plant is about half a cycle from the left's. Measured at 0.300 and
        // 0.833 on a 1.033 s clip, which is 0.52 of a cycle apart the short way round.
        const float lp = a.phase.at(a.contacts[0].spans.front().start);
        const float rp = a.phase.at(a.contacts[1].spans.front().start);
        float apart = std::fabs(rp - lp);
        apart = std::min(apart, 1.0f - apart);
        INFO("left phase " << lp << " right phase " << rp);
        CHECK(apart > 0.3f);
        CHECK(apart <= 0.5f);
    }

    SECTION("Running's stances are shorter than Walking's, with flight between") {
        const ClipAnalysis walk = analyse("Walking");
        const ClipAnalysis run = analyse("Running");
        // A run has flight phases, so both feet are down for less of the cycle than in a walk.
        // This is a comparison rather than an absolute threshold, so it does not encode one
        // asset's numbers as a magic constant.
        const float walkDuty = walk.contacts[0].dutyCycle + walk.contacts[1].dutyCycle;
        const float runDuty = run.contacts[0].dutyCycle + run.contacts[1].dutyCycle;
        INFO("walk " << walkDuty << " run " << runDuty);
        CHECK(runDuty < walkDuty);
        // And there is a genuine flight phase: the two feet together are down for less than the
        // whole cycle.
        CHECK(runDuty < 1.0f);
    }

    SECTION("Idle has both feet planted the whole time") {
        const ClipAnalysis a = analyse("Idle");
        for (const ContactTrack& t : a.contacts) {
            INFO(t.joint);
            REQUIRE(t.spans.size() == 1);
            CHECK(t.dutyCycle == Approx(1.0f));
        }
    }

    SECTION("every clip in the pack yields a phase, and none of them is empty") {
        for (const AnimationClip& clip : rig.clips) {
            INFO(clip.name);
            const ClipAnalysis a = analyseClip(sk, clip, feet, 0, {});
            CHECK_FALSE(a.phase.empty());
            // Phase stays in range everywhere, including at the ends.
            for (float t = 0.0f; t <= a.length; t += a.length / 16.0f) {
                const float p = a.phase.at(t);
                CHECK(p >= 0.0f);
                CHECK(p < 1.0f);
            }
        }
    }

    SECTION("the whole pack is in place, which is why the ground frame is a no-op here") {
        // ADR-540's measurement, re-derived by this unit rather than quoted from it.
        for (const AnimationClip& clip : rig.clips) {
            INFO(clip.name);
            const ClipAnalysis a = analyseClip(sk, clip, feet, 0, {});
            CHECK(a.groundSpeed < 0.30f);
        }
    }
#endif
}
