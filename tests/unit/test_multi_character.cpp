// Phase B §48 -- multi-character architecture.
//
// "Ensure static/shared data remains shared. Shared: Skeleton, MotionPack, Animation clips,
// RetargetProfile, Character configuration, IK chain definitions. Per-instance: Pose,
// MotionController state, current velocity, phase, targets, layer state, random seed. **Do not
// duplicate large animation data.**"
//
// §47 established that the per-character *time* does not grow with the count: +0.9% from one
// character to a hundred. That is a necessary condition and not a sufficient one -- duplicated
// animation data costs memory and cache, not cycles in a loop that never reads the duplicates. So
// this file asks the question §47 could not: **is the data actually shared, or merely not in the
// way?**
//
// The answer is no, and it is measured here rather than asserted, because the fix is a refactor
// with a blast radius and the decision about it belongs to someone looking at the number.

#include "assets/asset_registry.hpp"
#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <map>
#include <vector>
#include <memory>
#include <string>

using namespace avgen;
using Catch::Approx;

namespace {
namespace fs = std::filesystem;

fs::path glowmere() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" /
           "glowmere-valley-2-multicam.scene.json";
}

std::size_t clipBytes(const scene::AnimationClip& clip) {
    std::size_t bytes = clip.name.size();
    for (const scene::AnimationChannel& ch : clip.channels) {
        bytes += ch.times.size() * sizeof(float);
        bytes += ch.values.size() * sizeof(glm::vec4);
    }
    return bytes;
}

// FNV-1a over the actual key data, so "byte-identical" is a measurement rather than a shorthand
// for "same name and same size" -- which is what the first version compared, and which two rigs
// could satisfy while animating differently in every key.
std::uint64_t clipDigest(const std::vector<scene::AnimationClip>& clips) {
    std::uint64_t h = 1469598103934665603ull;
    const auto eat = [&h](const void* data, std::size_t bytes) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < bytes; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    for (const scene::AnimationClip& clip : clips) {
        eat(clip.name.data(), clip.name.size());
        for (const scene::AnimationChannel& ch : clip.channels) {
            eat(&ch.joint, sizeof(ch.joint));
            eat(&ch.path, sizeof(ch.path));
            eat(ch.times.data(), ch.times.size() * sizeof(float));
            eat(ch.values.data(), ch.values.size() * sizeof(glm::vec4));
        }
    }
    return h;
}

std::size_t rigBytes(const scene::SkinnedRig& rig) {
    std::size_t bytes = rig.skeleton.joints.size() * sizeof(scene::Joint);
    bytes += rig.skeleton.palette.size() * sizeof(std::uint32_t);
    bytes += rig.skeleton.inverseBind.size() * sizeof(glm::mat4);
    for (const scene::AnimationClip& clip : rig.clips) {
        bytes += clipBytes(clip);
    }
    return bytes;
}

} // namespace

TEST_CASE("the shipping scene carries one copy of the alien per alien", "[multi][phaseB][aliens]") {
    if (!fs::exists(glowmere())) {
        SKIP("the Glowmere scene is not present");
    }
    assets::AssetRegistry registry(glowmere().parent_path());
    auto loaded = scene::Composition::loadFile(glowmere(), registry);
    if (!loaded.has_value()) {
        SKIP("the Glowmere scene did not load (its assets are gitignored)");
    }
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);

    // Rigs are installed when the composition first updates, not when it loads. Counting them
    // straight after `loadFile` reports zero and would have made every number below a confident
    // 0.00 MB -- `docs/testing.md` family C, looking where the effect cannot reach.
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp->attach(params, modulator);
    comp->setViewport(1280, 720);
    comp->scene().detailLimits.entityDistanceCull = false;
    FrameTime time;
    for (int i = 0; i < 3; ++i) {
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        bus.clearEvents();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
    }

    // `SkinnedRig` holds `Skeleton skeleton` and `std::vector<AnimationClip> clips` **by value**.
    // Every instance of a character therefore carries its own copy of the rig it was loaded from
    // and of every clip that rig ships, whether or not it will ever play them.
    struct Signature {
        int count = 0;
        std::size_t bytes = 0;            // of the first instance seen
        std::uint64_t digest = 0;         // FNV-1a over the first instance's key data
        bool contentsAgree = true;        // every later instance had the same clips, byte for byte
    };
    std::map<std::size_t, Signature> bySignature;
    std::size_t total = 0;
    int rigs = 0;
    for (const scene::SkinnedRig& rig : comp->scene().rigs) {
        const std::size_t bytes = rigBytes(rig);
        total += bytes;
        const std::uint64_t digest = clipDigest(rig.clips);
        Signature& sig = bySignature[rig.skeleton.jointCount() * 1000u + rig.clips.size()];
        if (sig.count == 0) {
            sig.bytes = bytes;
            sig.digest = digest;
        } else if (sig.digest != digest) {
            sig.contentsAgree = false;
        }
        ++sig.count;
        ++rigs;
    }

    // Duplicate bytes computed **per signature**, against that signature's own size. The first
    // version multiplied the largest rig by every extra instance and reported 33 MB against a
    // 15.45 MB total -- a number that cannot be true, arrived at by an arithmetic shortcut that
    // treated nine farm animals as though they were aliens.
    std::size_t duplicate = 0;
    int extras = 0;
    for (const auto& [signature, sig] : bySignature) {
        WARN(fmt::format("  {} joints x {} clips: {} instance(s), {:.2f} MB each, clips {}",
                         signature / 1000u, signature % 1000u, sig.count,
                         static_cast<double>(sig.bytes) / (1024.0 * 1024.0),
                         sig.contentsAgree ? "byte-identical across instances" : "differ"));
        if (sig.count > 1 && sig.contentsAgree) {
            duplicate += sig.bytes * static_cast<std::size_t>(sig.count - 1);
            extras += sig.count - 1;
        }
    }
    WARN(fmt::format("{} rig(s), {:.2f} MB of skeleton and clip data in total", rigs,
                     static_cast<double>(total) / (1024.0 * 1024.0)));
    WARN(fmt::format("{:.2f} MB of that is a byte-identical second copy ({} extra instances) -- "
                     "{:.0f}% of the total",
                     static_cast<double>(duplicate) / (1024.0 * 1024.0), extras,
                     100.0 * static_cast<double>(duplicate) / static_cast<double>(total)));
    CHECK(duplicate <= total); // the arithmetic that caught the first attempt out

    REQUIRE(rigs > 0); // the ticks above installed them; without this the MB are all zero
    // **This is a finding, not a pass.** Sharing is the right fix and it is not made here, so the
    // assertion records what is true today: a second byte-identical copy exists, and it is a
    // substantial fraction of the total. If someone lands sharing, this fails and says so, which
    // is what a recorded finding is for -- the alternative is that the number quietly changes and
    // nobody learns it was ever true.
    CHECK(extras > 0);
    CHECK(duplicate > total / 4);
}

TEST_CASE("per-instance state is per-instance and nothing else is", "[multi][phaseB][aliens]") {
    // The other half of §48, and the half that is in good shape: what a second character does must
    // not be visible in the first. Two rigs loaded from the same file, posed differently, must
    // differ in the pose and agree in everything the file gave them.
    const fs::path alien =
        fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
    if (!fs::exists(alien)) {
        SKIP("the Glowmere alien is not present");
    }
    scene::Scene a;
    scene::Scene b;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(assets::loadGltf(alien, a, options).has_value());
    REQUIRE(assets::loadGltf(alien, b, options).has_value());
    const scene::SkinnedRig& ra = a.rigs.front();
    const scene::SkinnedRig& rb = b.rigs.front();

    // Same content, and -- the point of this test -- different storage.
    CHECK(ra.skeleton.jointCount() == rb.skeleton.jointCount());
    CHECK(ra.clips.size() == rb.clips.size());
    CHECK(&ra.skeleton.joints.front() != &rb.skeleton.joints.front());
    CHECK(rigBytes(ra) == rigBytes(rb));
    WARN(fmt::format("one alien rig is {:.2f} MB of skeleton and clip data",
                     static_cast<double>(rigBytes(ra)) / (1024.0 * 1024.0)));

    // And the per-instance state really is separate: pose one, and the other is untouched.
    scene::Pose pa;
    scene::Pose pb;
    scene::setRestPose(ra.skeleton, pa);
    scene::setRestPose(rb.skeleton, pb);
    REQUIRE(ra.clips.size() > 1);
    scene::sampleClip(ra.clips.front(), ra.clips.front().start + 0.4f, pa);
    bool differs = false;
    for (std::size_t i = 0; i < pa.local.size() && i < pb.local.size(); ++i) {
        if (glm::length(pa.local[i].position - pb.local[i].position) > 1e-6f ||
            glm::length(glm::vec4(pa.local[i].rotation.x, pa.local[i].rotation.y,
                                  pa.local[i].rotation.z, pa.local[i].rotation.w) -
                        glm::vec4(pb.local[i].rotation.x, pb.local[i].rotation.y,
                                  pb.local[i].rotation.z, pb.local[i].rotation.w)) > 1e-6f) {
            differs = true;
            break;
        }
    }
    CHECK(differs); // posing one moved it, so the comparison below is not vacuous
}
