#pragma once

// Motion packs and databases for the Phase C infrastructure tests (§37-§43, §57-§58, §68-§83).
//
// Two kinds, deliberately: a small synthetic pack whose content a test can change one key at a
// time (the cache, the diff and the refusal tests need exactly that), and the real Glowmere alien,
// which is what every claim about the product has to be checked against.

#include "assets/gltf_loader.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"
#include "scene/scene.hpp"

#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace avgen::testsupport {

inline scene::Skeleton probeRig() {
    scene::Skeleton sk;
    sk.name = "body";
    sk.joints.push_back(scene::Joint{"root.x", -1, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"foot.l", 0, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"foot.r", 0, scene::Transform{}});
    sk.palette = {0, 1, 2};
    sk.inverseBind = {glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
    return sk;
}

// Feet swinging out of phase at `amplitude`, the body travelling at `travel` m/s.
inline scene::AnimationClip probeGait(std::string name, float amplitude, float travel,
                                      int frames = 31) {
    scene::AnimationClip clip;
    clip.name = std::move(name);
    scene::AnimationChannel root;
    root.joint = 0;
    root.path = scene::AnimationPath::Translation;
    root.interpolation = scene::Interpolation::Linear;
    scene::AnimationChannel left = root;
    left.joint = 1;
    scene::AnimationChannel right = root;
    right.joint = 2;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        const float a = 6.283185307179586f * t;
        root.times.push_back(t);
        root.values.emplace_back(0.0f, 0.0f, travel * t, 0.0f);
        left.times.push_back(t);
        left.values.emplace_back(0.0f, 0.1f, (travel * t) + (amplitude * std::sin(a)), 0.0f);
        right.times.push_back(t);
        right.values.emplace_back(0.0f, 0.1f, (travel * t) - (amplitude * std::sin(a)), 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = static_cast<float>(frames - 1) / 30.0f;
    clip.channels = {std::move(root), std::move(left), std::move(right)};
    return clip;
}

// `loop` marks every clip as a cycle. A clip that sets off, stops or rounds part of a curve is not
// one: looped, it continues into its own start, and that continuation is a stop, a start or a
// reversed turn that the clip does not contain.
inline scene::MotionPack probePack(std::vector<scene::AnimationClip> clips = {}, bool loop = true) {
    if (clips.empty()) {
        clips = {probeGait("Walking", 0.30f, 1.2f), probeGait("Running", 0.75f, 3.0f)};
    }
    scene::MotionPack pack;
    pack.name = "probe";
    pack.skeleton = probeRig();
    pack.skeletonDigest = scene::skeletonDigest(pack.skeleton);
    scene::Provenance provenance;
    provenance.source = "synthetic";
    provenance.license = "CC0-1.0";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.processing = {"synthesised by tests/support/motion_fixtures.hpp"};
    pack.provenance = {provenance};
    pack.animation = std::move(clips);
    for (const scene::AnimationClip& clip : pack.animation) {
        scene::PackClip meta;
        meta.name = clip.name;
        meta.loop = loop;
        meta.sampleRate = 30.0f;
        meta.length = clip.length();
        meta.frames = static_cast<std::uint32_t>(std::lround(clip.length() * 30.0f)) + 1u;
        pack.clips.push_back(std::move(meta));
    }
    return pack;
}

inline scene::MotionDatabaseOptions probeOptions() {
    scene::MotionDatabaseOptions options;
    options.sampleRate = 30.0f;
    options.config.joints = {"foot.l", "foot.r"};
    options.config.trajectoryTimes = {0.2f, 0.4f};
    return options;
}

inline std::filesystem::path alienGlbPath() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
}

// The real Glowmere scout: its pack and its database, built the way the Phase C tests build them.
struct GlowmereMotion {
    scene::MotionPack pack;
    scene::MotionDatabase db;
    scene::MotionDatabaseOptions options;
};

inline std::optional<GlowmereMotion> glowmereMotion() {
    if (!std::filesystem::exists(alienGlbPath())) {
        return std::nullopt;
    }
    scene::Scene sc;
    assets::GltfLoadOptions loadOptions;
    loadOptions.loadImages = false;
    if (!assets::loadGltf(alienGlbPath(), sc, loadOptions).has_value() || sc.rigs.empty()) {
        return std::nullopt;
    }
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.sourceFile = "alien-scout.glb";
    provenance.creator = "AV Gen";
    provenance.license = "CC0-1.0";
    provenance.licenseUrl = "https://creativecommons.org/publicdomain/zero/1.0/";
    provenance.redistribution = scene::Redistribution::Allowed;
    provenance.derivedDataAllowed = true;
    provenance.trainingAllowed = true;
    provenance.processing = {"Phase C infrastructure"};
    provenance.toolVersion = "avgen-phase-c";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    packOptions.contacts.looping = true;
    packOptions.toolVersion = "avgen-phase-c";
    auto pack = scene::buildMotionPack("glowmere-scout", sc.rigs.front().skeleton,
                                       sc.rigs.front().clips, provenance, packOptions);
    if (!pack.has_value()) {
        return std::nullopt;
    }
    GlowmereMotion out;
    out.pack = std::move(*pack);
    out.options.sampleRate = 30.0f;
    out.options.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto db = scene::buildMotionDatabase(out.pack, out.options);
    if (!db.has_value()) {
        return std::nullopt;
    }
    out.db = std::move(*db);
    return out;
}

} // namespace avgen::testsupport
