// The MotionPack (ADR-550): the one file that crosses the offline/runtime boundary.
//
// **The arm that matters most in this file is the licence refusal.** ADR-542 decided that a pack
// without a licence does not build -- not a warning, a refusal -- because a warning in a build log
// is how research assets ship. A test that only checked the happy path would pass on an
// implementation that wrote the field and never looked at it.
//
// The arms:
//
//   build         a pack from a real skeleton and real clips, with analysis carried
//   no licence    refused, by name, with a message that says what to do about it
//   no source     refused: a derived database whose ancestry cannot be printed cannot be cleared
//   default       an absent redistribution state reads back as REQUIRES_REVIEW, never as allowed
//   worst wins    one forbidden clip makes the whole pack unshippable
//   round trip    write, read, and compare every field -- including the phase samples
//   digest        a pack built against one skeleton does not silently accept another
//   version       a future version is refused rather than half-read
//   corrupt       a truncated blob is refused at the joint or clip it ran out on
//   report        the human-readable validation the brief asks for

#include "assets/gltf_loader.hpp"
#include "scene/motion_pack.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <nlohmann/json.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>

using namespace avgen::scene;
using Catch::Approx;

namespace {

namespace fs = std::filesystem;

Skeleton stubRig() {
    Skeleton sk;
    sk.name = "stub";
    sk.joints.push_back(Joint{"root", -1, Transform{}});
    Joint foot;
    foot.name = "foot";
    foot.parent = 0;
    foot.rest.position = glm::vec3(0.0f, -1.0f, 0.0f);
    sk.joints.push_back(foot);
    sk.palette = {0, 1};
    sk.inverseBind = {glm::mat4(1.0f), glm::mat4(1.0f)};
    return sk;
}

AnimationClip bobClip(const char* name) {
    AnimationClip clip;
    clip.name = name;
    clip.start = 0.0f;
    clip.duration = 1.0f;
    AnimationChannel c;
    c.joint = 1;
    c.path = AnimationPath::Translation;
    c.interpolation = Interpolation::Linear;
    for (int i = 0; i <= 30; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        c.times.push_back(t);
        // Down for the first half, up for the second: one plant per cycle.
        const float y = i < 15 ? -1.0f : -1.0f + 0.4f * static_cast<float>(i - 15) / 15.0f;
        c.values.emplace_back(0.0f, y, 0.0f, 0.0f);
    }
    clip.channels.push_back(std::move(c));
    return clip;
}

Provenance goodProvenance() {
    Provenance p;
    p.source = "test-fixture";
    p.sourceFile = "bob.glb";
    p.creator = "the test";
    p.license = "CC0-1.0";
    p.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    p.attributionRequired = false;
    p.redistribution = Redistribution::Allowed;
    p.derivedDataAllowed = true;
    p.trainingAllowed = true;
    p.processing = {"synthesised in a test"};
    p.toolVersion = "test";
    return p;
}

PackBuildOptions optionsWithFeet() {
    PackBuildOptions options;
    options.contactJoints = {ContactJoint{"foot", ContactKind::Foot}};
    options.contacts.looping = true;
    options.toolVersion = "avgen-test";
    return options;
}

fs::path scratchDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir;
}

} // namespace

TEST_CASE("a pack without a licence does not build", "[motionpack][licence]") {
    // THE ARM THIS FILE LEADS WITH. ADR-542: a warning in a build log is how research assets ship.
    const Skeleton sk = stubRig();
    const std::vector<AnimationClip> clips{bobClip("bob")};

    Provenance missing = goodProvenance();
    missing.license.clear();
    const auto noLicence = buildMotionPack("pack", sk, clips, missing, optionsWithFeet());
    REQUIRE_FALSE(noLicence.has_value());
    INFO(noLicence.error().message);
    CHECK(noLicence.error().message.find("licence") != std::string::npos);
    // The message tells a human what to do rather than only what went wrong.
    CHECK(noLicence.error().message.find("REQUIRES_REVIEW") != std::string::npos);

    Provenance anonymous = goodProvenance();
    anonymous.source.clear();
    const auto noSource = buildMotionPack("pack", sk, clips, anonymous, optionsWithFeet());
    REQUIRE_FALSE(noSource.has_value());
    CHECK(noSource.error().message.find("ancestry") != std::string::npos);

    // ...and the same pack with the field filled in builds, so the refusal is about the field and
    // not about something else in the fixture.
    CHECK(buildMotionPack("pack", sk, clips, goodProvenance(), optionsWithFeet()).has_value());
}

TEST_CASE("a pack carries its analysis rather than making the runtime redo it", "[motionpack]") {
    const Skeleton sk = stubRig();
    const std::vector<AnimationClip> clips{bobClip("walk"), bobClip("run")};
    const auto built = buildMotionPack("stub", sk, clips, goodProvenance(), optionsWithFeet());
    REQUIRE(built.has_value());
    const MotionPack& pack = *built;

    CHECK(pack.version == kMotionPackVersion);
    CHECK(pack.clips.size() == 2);
    CHECK(pack.animation.size() == 2);
    CHECK(pack.frameCount() > 50);
    CHECK(pack.findClip("run") == 1);
    CHECK(pack.findClip("nothing") == -1);
    CHECK_FALSE(pack.skeletonDigest.empty());
    CHECK(pack.skeletonDigest == skeletonDigest(sk));

    // The contacts and the phase are there, which is the point of carrying them.
    REQUIRE_FALSE(pack.clips.front().contacts.empty());
    CHECK(pack.clips.front().contacts.front().joint == "foot");
    CHECK(pack.clips.front().contacts.front().spans.size() >= 1);
    CHECK_FALSE(pack.clips.front().phase.empty());
    // And the tool version reached the provenance.
    CHECK(pack.provenance.front().toolVersion == "avgen-test");
}

TEST_CASE("an unresolved licence defaults to REQUIRES_REVIEW, never to allowed", "[motionpack][licence]") {
    // A reader that defaulted the other way would turn a field nobody filled in into a shipping
    // permission. The arm reads a pack.json with the key absent entirely.
    const Skeleton sk = stubRig();
    const std::vector<AnimationClip> clips{bobClip("walk")};
    Provenance vague = goodProvenance();
    vague.redistribution = Redistribution::RequiresReview;
    const auto built = buildMotionPack("vague", sk, clips, vague, optionsWithFeet());
    REQUIRE(built.has_value());

    const fs::path dir = scratchDir("avgen-pack-vague");
    REQUIRE(writeMotionPack(*built, dir).has_value());
    // Strip the field from the written manifest, as a hand-edited pack would have it.
    {
        std::ifstream in(dir / "pack.json");
        nlohmann::json doc = nlohmann::json::parse(in);
        doc["provenance"][0].erase("redistribution");
        std::ofstream out(dir / "pack.json");
        out << doc.dump(1);
    }
    const auto read = readMotionPack(dir);
    REQUIRE(read.has_value());
    CHECK(read->provenance.front().redistribution == Redistribution::RequiresReview);
    CHECK(read->redistribution() == Redistribution::RequiresReview);
    // And a validation says so in words.
    const PackValidation validation = validateMotionPack(*read);
    CHECK(validation.clipsRequiringReview == 1);
    CHECK(validation.report().find("REQUIRES_REVIEW") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("one unshippable clip makes the whole pack unshippable", "[motionpack][licence]") {
    const Skeleton sk = stubRig();
    const std::vector<AnimationClip> clips{bobClip("ours"), bobClip("theirs")};
    auto built = buildMotionPack("mixed", sk, clips, goodProvenance(), optionsWithFeet());
    REQUIRE(built.has_value());
    // A second source, forbidden, on the second clip only -- the realistic case, where a pack mixes
    // a CC0 character's own takes with corpus motion.
    Provenance forbidden = goodProvenance();
    forbidden.source = "some-research-corpus";
    forbidden.license = "CC-BY-NC-ND-4.0";
    forbidden.redistribution = Redistribution::Forbidden;
    forbidden.derivedDataAllowed = false;
    built->provenance.push_back(forbidden);
    built->clips[1].provenance = 1;

    CHECK(built->redistribution() == Redistribution::Forbidden);
    const PackValidation validation = validateMotionPack(*built);
    CHECK(validation.clipsForbidden == 1);
    CHECK_FALSE(validation.ok());
    CHECK(validation.report().find("FAIL") != std::string::npos);
    CHECK(validation.report().find("must not ship") != std::string::npos);
}

TEST_CASE("a pack round-trips through disk", "[motionpack]") {
    const Skeleton sk = stubRig();
    const std::vector<AnimationClip> clips{bobClip("walk"), bobClip("run")};
    const auto built = buildMotionPack("round", sk, clips, goodProvenance(), optionsWithFeet());
    REQUIRE(built.has_value());
    const fs::path dir = scratchDir("avgen-pack-round");
    REQUIRE(writeMotionPack(*built, dir).has_value());
    CHECK(fs::exists(dir / "pack.json"));
    CHECK(fs::exists(dir / "skeleton.bin"));
    CHECK(fs::exists(dir / "clips.bin"));
    CHECK(fs::exists(dir / "meta.bin"));

    const auto read = readMotionPack(dir);
    INFO((read ? std::string() : read.error().message));
    REQUIRE(read.has_value());

    CHECK(read->name == built->name);
    CHECK(read->skeletonDigest == built->skeletonDigest);
    // The skeleton survived, joint for joint.
    REQUIRE(read->skeleton.jointCount() == built->skeleton.jointCount());
    for (std::size_t i = 0; i < read->skeleton.joints.size(); ++i) {
        INFO(i);
        CHECK(read->skeleton.joints[i].name == built->skeleton.joints[i].name);
        CHECK(read->skeleton.joints[i].parent == built->skeleton.joints[i].parent);
        CHECK(read->skeleton.joints[i].rest.position.y ==
              Approx(built->skeleton.joints[i].rest.position.y));
    }
    // ...and the digest still matches the skeleton that came back, which is the check that catches
    // a reader that reconstructed a *different* skeleton and recorded the old hash.
    CHECK(read->skeletonDigest == skeletonDigest(read->skeleton));

    // The animation survived, channel for channel and key for key.
    REQUIRE(read->animation.size() == built->animation.size());
    for (std::size_t c = 0; c < read->animation.size(); ++c) {
        INFO(read->animation[c].name);
        CHECK(read->animation[c].name == built->animation[c].name);
        CHECK(read->animation[c].duration == Approx(built->animation[c].duration));
        REQUIRE(read->animation[c].channels.size() == built->animation[c].channels.size());
        const AnimationChannel& a = read->animation[c].channels.front();
        const AnimationChannel& b = built->animation[c].channels.front();
        CHECK(a.joint == b.joint);
        CHECK(a.path == b.path);
        REQUIRE(a.values.size() == b.values.size());
        for (std::size_t k = 0; k < a.values.size(); ++k) {
            CHECK(a.values[k].y == Approx(b.values[k].y));
        }
    }

    // The phase samples survived -- the part that lives in meta.bin and is easiest to lose.
    REQUIRE(read->clips.size() == built->clips.size());
    for (std::size_t i = 0; i < read->clips.size(); ++i) {
        INFO(read->clips[i].name);
        CHECK(read->clips[i].phase.cyclic == built->clips[i].phase.cyclic);
        CHECK(read->clips[i].phase.cycleSeconds == Approx(built->clips[i].phase.cycleSeconds));
        REQUIRE(read->clips[i].phase.phase.size() == built->clips[i].phase.phase.size());
        CHECK_FALSE(read->clips[i].phase.phase.empty());
        for (std::size_t k = 0; k < read->clips[i].phase.phase.size(); ++k) {
            CHECK(read->clips[i].phase.phase[k] == Approx(built->clips[i].phase.phase[k]));
        }
        // And the contacts, which live in the JSON.
        REQUIRE(read->clips[i].contacts.size() == built->clips[i].contacts.size());
        CHECK(read->clips[i].contacts.front().spans.size() ==
              built->clips[i].contacts.front().spans.size());
        CHECK(read->clips[i].contacts.front().dutyCycle ==
              Approx(built->clips[i].contacts.front().dutyCycle));
    }
    // The provenance survived intact, which is the field the whole format exists to carry.
    REQUIRE(read->provenance.size() == 1);
    CHECK(read->provenance.front() == built->provenance.front());

    fs::remove_all(dir);
}

TEST_CASE("a pack knows which skeleton it belongs to", "[motionpack]") {
    // The silent-wrong-character failure: a pack played on a rig it was not built for. The digest
    // is what makes it loud.
    const Skeleton a = stubRig();
    Skeleton b = stubRig();
    b.joints[1].rest.position.y = -1.5f; // a different leg length is a different skeleton
    CHECK(skeletonDigest(a) != skeletonDigest(b));
    // ...and a rename is too.
    Skeleton c = stubRig();
    c.joints[1].name = "hoof";
    CHECK(skeletonDigest(a) != skeletonDigest(c));
    // The same skeleton twice is the same digest, or the check is useless.
    CHECK(skeletonDigest(a) == skeletonDigest(stubRig()));

    auto built = buildMotionPack("p", a, {bobClip("w")}, goodProvenance(), optionsWithFeet());
    REQUIRE(built.has_value());
    built->skeletonDigest = skeletonDigest(b); // as a hand-edited or stale pack would be
    const PackValidation validation = validateMotionPack(*built);
    CHECK_FALSE(validation.ok());
    CHECK(validation.report().find("digest") != std::string::npos);
}

TEST_CASE("a pack from the future is refused rather than half-read", "[motionpack]") {
    const Skeleton sk = stubRig();
    const auto built = buildMotionPack("p", sk, {bobClip("w")}, goodProvenance(), optionsWithFeet());
    REQUIRE(built.has_value());
    const fs::path dir = scratchDir("avgen-pack-future");
    REQUIRE(writeMotionPack(*built, dir).has_value());
    {
        std::ifstream in(dir / "pack.json");
        nlohmann::json doc = nlohmann::json::parse(in);
        doc["version"] = kMotionPackVersion + 7;
        std::ofstream out(dir / "pack.json");
        out << doc.dump(1);
    }
    const auto read = readMotionPack(dir);
    REQUIRE_FALSE(read.has_value());
    INFO(read.error().message);
    CHECK(read.error().message.find("cannot be read by this build") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a truncated pack is refused at the point it ran out", "[motionpack]") {
    const Skeleton sk = stubRig();
    const auto built = buildMotionPack("p", sk, {bobClip("w")}, goodProvenance(), optionsWithFeet());
    REQUIRE(built.has_value());

    SECTION("a truncated clips.bin") {
        const fs::path dir = scratchDir("avgen-pack-trunc-clips");
        REQUIRE(writeMotionPack(*built, dir).has_value());
        const auto size = fs::file_size(dir / "clips.bin");
        fs::resize_file(dir / "clips.bin", size / 2);
        const auto read = readMotionPack(dir);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().message.find("truncated") != std::string::npos);
        fs::remove_all(dir);
    }

    SECTION("a blob with the wrong magic") {
        const fs::path dir = scratchDir("avgen-pack-magic");
        REQUIRE(writeMotionPack(*built, dir).has_value());
        std::ofstream out(dir / "skeleton.bin", std::ios::binary | std::ios::trunc);
        out << "not a skeleton at all";
        out.close();
        const auto read = readMotionPack(dir);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().message.find("magic") != std::string::npos);
        fs::remove_all(dir);
    }

    SECTION("a missing file") {
        const fs::path dir = scratchDir("avgen-pack-missing");
        REQUIRE(writeMotionPack(*built, dir).has_value());
        fs::remove(dir / "meta.bin");
        CHECK_FALSE(readMotionPack(dir).has_value());
        fs::remove_all(dir);
    }
}

TEST_CASE("the real Glowmere pack builds, validates and round-trips", "[motionpack][aliens]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
    if (!fs::exists(path)) {
        SKIP("assets are not present");
    }
    avgen::scene::Scene scene;
    avgen::assets::GltfLoadOptions gltf;
    gltf.loadImages = false;
    REQUIRE(avgen::assets::loadGltf(path, scene, gltf));
    const SkinnedRig& rig = scene.rigs.front();

    Provenance p;
    p.source = "assets/aliens";
    p.sourceFile = "alien-scout.glb";
    p.creator = "supplied by the project owner";
    p.license = "CC0-1.0";
    p.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    p.attributionRequired = false;
    p.redistribution = Redistribution::Allowed;
    p.derivedDataAllowed = true;
    p.trainingAllowed = true;
    p.processing = {"glTF import"};
    p.toolVersion = "avgen-test";

    PackBuildOptions options;
    options.contactJoints = {ContactJoint{"foot.l", ContactKind::Foot},
                             ContactJoint{"foot.r", ContactKind::Foot}};
    const auto built = buildMotionPack("glowmere-alien", rig.skeleton, rig.clips, p, options);
    INFO((built ? std::string() : built.error().message));
    REQUIRE(built.has_value());
    CHECK(built->clips.size() == 26);
    CHECK(built->redistribution() == Redistribution::Allowed);

    const PackValidation validation = validateMotionPack(*built);
    INFO(validation.report());
    CHECK(validation.ok());
    CHECK(validation.clips == 26);
    CHECK(validation.frames > 1700); // the 1,712 frames ADR-540 measured
    CHECK(validation.clipsForbidden == 0);
    CHECK(validation.clipsRequiringReview == 0);
    CHECK(validation.clipsWithoutContacts == 0);

    const fs::path dir = scratchDir("avgen-pack-alien");
    REQUIRE(writeMotionPack(*built, dir).has_value());
    const auto read = readMotionPack(dir);
    INFO((read ? std::string() : read.error().message));
    REQUIRE(read.has_value());
    CHECK(read->clips.size() == 26);
    CHECK(read->skeleton.jointCount() == rig.skeleton.jointCount());
    CHECK(read->skeletonDigest == skeletonDigest(rig.skeleton));
    CHECK(validateMotionPack(*read).ok());
    fs::remove_all(dir);
#endif
}
