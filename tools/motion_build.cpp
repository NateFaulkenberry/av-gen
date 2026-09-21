// `avgen-motion` -- the offline motion pipeline (Phase A step 12).
//
// A build target rather than a script, for the reason every other tool in this directory is one:
// it uses exactly the importer, the analyser, the retargeter and the pack writer the engine uses.
// A reimplementation would eventually disagree with them, and the disagreement would be silent.
//
// **Offline by construction.** It opens no window, touches no GPU, and links nothing the runtime
// does not already have. It is where the expensive work lives: importing a four-million-frame
// corpus, retargeting it, analysing every clip, and writing a pack.
//
//   avgen-motion inspect   <file...>                  what is in these files
//   avgen-motion analyse   <file> --contacts a,b      contacts, phase and travel per clip
//   avgen-motion retarget  <src> <dst> --profile p    move motion between skeletons
//   avgen-motion pack      <file...> --out dir        build a MotionPack
//   avgen-motion validate  <pack>                     the report, and a non-zero exit on FAIL
//   avgen-motion benchmark <file> [--repeat n]        what each stage costs on this machine
//
// Exit codes: 0 success, 1 usage or I/O, 2 a validation that FAILED. A validation that passes with
// warnings exits 0 -- a warning is for a human, and a build that failed on one would be a build
// nobody could run.

#include "assets/bvh_loader.hpp"
#include "assets/gltf_loader.hpp"
#include "scene/motion_analysis.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"
#include "scene/retarget.hpp"
#include "scene/scene.hpp"

#include <random>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr char kToolVersion[] = "avgen-motion/1";

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;

    [[nodiscard]] std::string option(const std::string& key, std::string fallback = {}) const {
        const auto it = options.find(key);
        return it == options.end() ? std::move(fallback) : it->second;
    }
    [[nodiscard]] bool has(const std::string& key) const { return options.count(key) > 0; }
};

Args parseArgs(int argc, char** argv) {
    Args args;
    if (argc > 1) {
        args.command = argv[1];
    }
    for (int i = 2; i < argc; ++i) {
        std::string token = argv[i];
        if (token.rfind("--", 0) == 0) {
            const std::size_t eq = token.find('=');
            if (eq != std::string::npos) {
                args.options[token.substr(2, eq - 2)] = token.substr(eq + 1);
            } else if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                args.options[token.substr(2)] = argv[++i];
            } else {
                args.options[token.substr(2)] = "true";
            }
        } else {
            args.positional.push_back(std::move(token));
        }
    }
    return args;
}

std::vector<std::string> splitCommas(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == ',') {
            if (!current.empty()) {
                out.push_back(current);
            }
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

// One loaded source: a skeleton and its clips, from either format.
struct Source {
    scene::Skeleton skeleton;
    std::vector<scene::AnimationClip> clips;
    std::string label;
    std::string format;
};

// **The two unit conversions on this path, in the one place that knows about both.**
//
// A motion corpus arrives in centimetres at 60 Hz; AV Gen is metres and the pack is 30 Hz. Each
// conversion is individually obvious and each has a silent failure mode: the wrong scale lands a
// 170-unit human in a world where the alien is 1.66, and the wrong rate has already produced a
// one-frame time-base disagreement three times this session (ADR-204). They are applied here, and
// checked here, rather than at each of the seven call sites that needs one.
struct SourceUnits {
    float scale = 1.0f;     // source length units to metres
    float sampleRate = 30.0f; // the pack's rate, whatever the source was captured at
};

// A character is between a cat and an elephant. This is not a style check -- it is the assertion
// that catches a forgotten `--scale 0.01`, which otherwise produces a skeleton that retargets,
// packs and validates cleanly and is a hundred times too big.
constexpr float kMinCharacterHeight = 0.2f;
constexpr float kMaxCharacterHeight = 5.0f;

float restHeight(const scene::Skeleton& skeleton) {
    scene::Pose pose;
    std::vector<glm::mat4> model;
    scene::setRestPose(skeleton, pose);
    scene::poseToModel(skeleton, pose, model);
    float lo = 0.0f;
    float hi = 0.0f;
    bool first = true;
    for (const glm::mat4& m : model) {
        if (first) {
            lo = m[3].y;
            hi = m[3].y;
            first = false;
        }
        lo = std::min(lo, m[3].y);
        hi = std::max(hi, m[3].y);
    }
    return hi - lo;
}

Result<Source> loadSource(const fs::path& path, float bvhScale) {
    Source source;
    source.label = path.filename().string();
    const std::string ext = path.extension().string();
    if (ext == ".bvh" || ext == ".BVH") {
        assets::BvhLoadOptions options;
        options.scale = bvhScale;
        options.clipName = path.stem().string();
        auto bvh = assets::loadBvh(path, options);
        if (!bvh) {
            return std::unexpected(bvh.error());
        }
        source.skeleton = std::move(bvh->skeleton);
        source.clips.push_back(std::move(bvh->clip));
        source.format = "bvh";
        const float height = restHeight(source.skeleton);
        if (height < kMinCharacterHeight || height > kMaxCharacterHeight) {
            return fail(
                "'{}' is {:.2f} units tall at scale {}, which is not a character. 100STYLE, CMU and "
                "ACCAD are in centimetres: pass --scale 0.01.",
                path.filename().string(), height, bvhScale);
        }
        return source;
    }
    scene::Scene scene;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    auto loaded = assets::loadGltf(path, scene, options);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    if (scene.rigs.empty()) {
        return fail("'{}' has no skinned rig", path.string());
    }
    source.skeleton = scene.rigs.front().skeleton;
    source.clips = scene.rigs.front().clips;
    source.format = "gltf";
    return source;
}

int usage() {
    fmt::print(stderr,
               "avgen-motion -- the offline motion pipeline\n\n"
               "  inspect   <file...>                     what is in these files\n"
               "  analyse   <file> --contacts a,b         contacts, phase and travel per clip\n"
               "  retarget  <src> <dst> --map s:t,s:t     move motion between skeletons\n"
               "  pack      <file...> --out <dir>         build a MotionPack\n"
               "  validate  <pack>                        the report; exit 2 on FAIL\n"
               "  reach     <pack> [--chain h:k:a,...]    how close each pose is to a straight leg\n"
               "  database  <pack> --joints a,b,c [--bench]  build a motion database and search it\n"
               "  benchmark <file> [--repeat n]           what each stage costs here\n"
               "  survey    <dir>                         per-FILE rotation orders, up axis,\n"
               "                                          skeleton consistency across a corpus\n\n"
               "  --scale <f>       BVH units to metres (0.01 for centimetres)\n"
               "  --contacts a,b    joints to analyse; the first is the phase reference\n"
               "  --license <id>    SPDX identifier, REQUIRED by `pack`\n"
               "  --retarget-to <f> --map s:t,...   pack onto ANOTHER skeleton\n"
               "  --source <name>   the corpus this came from, REQUIRED by `pack`\n");
    return 1;
}

// ---- commands ----------------------------------------------------------------------------------

int cmdInspect(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    const float scale = std::stof(args.option("scale", "1.0"));
    for (const std::string& file : args.positional) {
        auto source = loadSource(file, scale);
        if (!source) {
            fmt::print(stderr, "{}: {}\n", file, source.error().message);
            return 1;
        }
        float seconds = 0.0f;
        for (const scene::AnimationClip& clip : source->clips) {
            seconds += clip.length();
        }
        fmt::print("{}\n  format {}  joints {}  clips {}  {:.2f} s\n", file, source->format,
                   source->skeleton.jointCount(), source->clips.size(), seconds);
        // The joints, so a human writing a retarget profile can see the names.
        if (args.has("joints")) {
            for (std::size_t i = 0; i < source->skeleton.joints.size(); ++i) {
                const scene::Joint& joint = source->skeleton.joints[i];
                fmt::print("    [{:3}] {:<28} parent {:3}  role {}\n", i, joint.name, joint.parent,
                           scene::humanoidRoleName(scene::roleForJointName(joint.name)));
            }
        }
    }
    return 0;
}

int cmdAnalyse(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    const float scale = std::stof(args.option("scale", "1.0"));
    auto source = loadSource(args.positional.front(), scale);
    if (!source) {
        fmt::print(stderr, "{}\n", source.error().message);
        return 1;
    }
    std::vector<scene::ContactJoint> joints;
    for (const std::string& name : splitCommas(args.option("contacts"))) {
        joints.push_back(scene::ContactJoint{name, scene::ContactKind::Foot});
    }
    if (joints.empty()) {
        fmt::print(stderr, "analyse needs --contacts (the first joint is the phase reference)\n");
        return 1;
    }
    // `net` is where the root ended up and `path` is how far it actually went. They are printed
    // side by side because on real mocap they disagree by two orders of magnitude, and only `path`
    // answers "does this clip travel".
    // `net` is where it ended up, `path` is how far it went, `ext` is how far apart the two most
    // distant places it stood are as a multiple of its own height -- and only `ext` decides
    // `travels`. See `measureRoot` for why the other two both give the wrong answer.
    fmt::print("{:<28} {:>7} {:>8} {:>9} {:>8} {:>7} {:>6} {:>7} {:>8}  contacts\n", "clip", "len",
               "net", "path", "speed", "ext/h", "trav", "cyclic", "cycle");
    for (const scene::AnimationClip& clip : source->clips) {
        const scene::ClipAnalysis a = scene::analyseClip(source->skeleton, clip, joints, 0, {});
        std::string spans;
        for (const scene::ContactTrack& track : a.contacts) {
            spans += fmt::format(" {}:{} slide{:.3f}", track.joint, track.spans.size(),
                                 track.worstSlide);
        }
        fmt::print("{:<28} {:7.3f} {:8.3f} {:9.3f} {:8.3f} {:7.2f} {:>6} {:>7} {:8.3f} {}\n",
                   clip.name, a.length,
                   glm::length(glm::vec3(a.rootTravel.x, 0.0f, a.rootTravel.z)), a.rootPathLength,
                   a.groundSpeed, a.restHeight > 0.0f ? a.rootExtent / a.restHeight : 0.0f,
                   a.travels ? (a.travelAmbiguous ? "yes?" : "yes")
                             : (a.travelAmbiguous ? "no?" : "no"), a.phase.cyclic ? "yes" : "no", a.phase.cycleSeconds,
                   spans);
    }
    return 0;
}

int cmdRetarget(const Args& args) {
    if (args.positional.size() < 2) {
        return usage();
    }
    const float scale = std::stof(args.option("scale", "1.0"));
    auto from = loadSource(args.positional[0], scale);
    auto to = loadSource(args.positional[1], 1.0f);
    if (!from) {
        fmt::print(stderr, "{}\n", from.error().message);
        return 1;
    }
    if (!to) {
        fmt::print(stderr, "{}\n", to.error().message);
        return 1;
    }

    scene::RetargetProfile profile;
    profile.name = args.option("profile", "cli");
    profile.rootJoint = args.option("root");
    const std::string map = args.option("map");
    if (map.empty()) {
        // No map given: guess one and say so loudly. A guess is a starting point a human corrects,
        // never an answer (ADR-548).
        const scene::RoleGuess guess = scene::guessRetargetProfile(from->skeleton, to->skeleton);
        profile.joints = guess.profile.joints;
        fmt::print("no --map given; guessed {} mapping(s) by role. CHECK THEM.\n", profile.joints.size());
        for (const scene::JointMapping& m : profile.joints) {
            fmt::print("  {:<28} -> {:<28} ({})\n", m.source, m.target, scene::humanoidRoleName(m.role));
        }
        if (!guess.unmatchedTarget.empty()) {
            fmt::print("  target has no joint for:");
            for (const scene::HumanoidRole role : guess.unmatchedTarget) {
                fmt::print(" {}", scene::humanoidRoleName(role));
            }
            fmt::print("\n");
        }
    } else {
        for (const std::string& pair : splitCommas(map)) {
            const std::size_t colon = pair.find(':');
            if (colon == std::string::npos) {
                fmt::print(stderr, "--map entries are source:target, got '{}'\n", pair);
                return 1;
            }
            scene::JointMapping m;
            m.source = pair.substr(0, colon);
            m.target = pair.substr(colon + 1);
            m.role = scene::roleForJointName(m.target);
            profile.joints.push_back(std::move(m));
        }
    }

    const scene::RetargetBinding binding =
        scene::bindRetarget(from->skeleton, to->skeleton, profile);
    for (const std::string& problem : binding.problems) {
        fmt::print(stderr, "  {}\n", problem);
    }
    if (!binding.usable()) {
        fmt::print(stderr, "nothing to retarget\n");
        return 1;
    }
    fmt::print("bound {} joint(s), rootScale {:.4f}\n", binding.links.size(), binding.rootScale);
    for (const scene::AnimationClip& clip : from->clips) {
        scene::RetargetStats stats;
        const scene::AnimationClip out =
            scene::retargetClip(clip, from->skeleton, to->skeleton, binding, &stats);
        fmt::print("  {:<28} {:5} frames  {:3} channels  orientation worst {:.4f} deg mean {:.5f}  "
                   "bone worst {:.6f}\n",
                   clip.name, stats.frames, stats.channels, stats.worstOrientationError,
                   stats.meanOrientationError, stats.worstBoneLengthError);
    }
    return 0;
}

int cmdPack(const Args& args) {
    if (args.positional.empty() || !args.has("out")) {
        return usage();
    }
    const float scale = std::stof(args.option("scale", "1.0"));
    scene::Provenance provenance;
    provenance.source = args.option("source");
    provenance.license = args.option("license");
    provenance.licenseUrl = args.option("licenseUrl");
    provenance.creator = args.option("creator");
    provenance.sourceFile = args.positional.front();
    provenance.toolVersion = kToolVersion;
    provenance.processing.push_back(fmt::format("import scale={}", scale));
    if (args.has("redistribution")) {
        scene::Redistribution value = scene::Redistribution::RequiresReview;
        if (!scene::redistributionFromName(args.option("redistribution"), value)) {
            fmt::print(stderr, "--redistribution must be allowed, forbidden or REQUIRES_REVIEW\n");
            return 1;
        }
        provenance.redistribution = value;
    }
    provenance.derivedDataAllowed = args.option("derivedDataAllowed", "false") == "true";
    provenance.trainingAllowed = args.option("trainingAllowed", "false") == "true";

    auto first = loadSource(args.positional.front(), scale);
    if (!first) {
        fmt::print(stderr, "{}\n", first.error().message);
        return 1;
    }

    // ---- optional retarget, so a corpus can be imported and packed in one pass ------------------
    //
    // `--retarget-to <rig> --map s:t,...` makes the pack belong to the TARGET skeleton. Without it
    // a pack of 100STYLE would be a pack of a skeleton no AV Gen character has.
    scene::Skeleton packSkeleton = first->skeleton;
    scene::RetargetBinding binding;
    bool retargeting = false;
    if (args.has("retarget-to")) {
        auto target = loadSource(args.option("retarget-to"), 1.0f);
        if (!target) {
            fmt::print(stderr, "{}\n", target.error().message);
            return 1;
        }
        scene::RetargetProfile profile;
        profile.name = args.option("profile", "cli");
        profile.rootJoint = args.option("root");
        for (const std::string& pair : splitCommas(args.option("map"))) {
            const std::size_t colon = pair.find(':');
            if (colon == std::string::npos) {
                fmt::print(stderr, "--map entries are source:target, got '{}'\n", pair);
                return 1;
            }
            profile.joints.push_back(scene::JointMapping{
                pair.substr(0, colon), pair.substr(colon + 1),
                scene::roleForJointName(pair.substr(colon + 1))});
        }
        if (profile.joints.empty()) {
            fmt::print(stderr, "--retarget-to needs --map\n");
            return 1;
        }
        binding = scene::bindRetarget(first->skeleton, target->skeleton, profile);
        for (const std::string& problem : binding.problems) {
            fmt::print(stderr, "  {}\n", problem);
        }
        if (!binding.usable()) {
            fmt::print(stderr, "the retarget profile resolved to nothing\n");
            return 1;
        }
        packSkeleton = target->skeleton;
        retargeting = true;
        provenance.processing.push_back(
            fmt::format("retarget profile={} joints={} rootScale={:.4f}", profile.name,
                        binding.links.size(), binding.rootScale));
        fmt::print("retargeting through {} joint(s), rootScale {:.4f}\n", binding.links.size(),
                   binding.rootScale);
    }

    const auto convert = [&](std::vector<scene::AnimationClip>& source) {
        if (!retargeting) {
            return;
        }
        for (scene::AnimationClip& clip : source) {
            scene::RetargetStats stats;
            scene::AnimationClip out =
                scene::retargetClip(clip, first->skeleton, packSkeleton, binding, &stats);
            // The retarget's own error, per clip, into the provenance. A pack that cannot say how
            // accurately its motion was transferred is a pack nobody can judge.
            provenance.processing.push_back(fmt::format(
                "retarget '{}': {} frames, orientation worst {:.4f} deg mean {:.5f}, bone worst {:.6f}",
                clip.name, stats.frames, stats.worstOrientationError, stats.meanOrientationError,
                stats.worstBoneLengthError));
            clip = std::move(out);
        }
    };

    convert(first->clips);
    std::vector<scene::AnimationClip> clips = first->clips;
    for (std::size_t i = 1; i < args.positional.size(); ++i) {
        auto more = loadSource(args.positional[i], scale);
        if (!more) {
            fmt::print(stderr, "{}\n", more.error().message);
            return 1;
        }
        // Every source in one pack must be the same skeleton. Two skeletons in one pack is a pack
        // whose clips cannot all be played, and it would be found at load rather than here.
        if (scene::skeletonDigest(more->skeleton) != scene::skeletonDigest(first->skeleton)) {
            fmt::print(stderr, "'{}' has a different skeleton from '{}'; retarget it first\n",
                       args.positional[i], args.positional.front());
            return 1;
        }
        convert(more->clips);
        for (scene::AnimationClip& clip : more->clips) {
            clips.push_back(std::move(clip));
        }
    }

    scene::PackBuildOptions options;
    options.sampleRate = std::stof(args.option("rate", "30"));
    options.toolVersion = kToolVersion;
    for (const std::string& name : splitCommas(args.option("contacts"))) {
        options.contactJoints.push_back(scene::ContactJoint{name, scene::ContactKind::Foot});
    }

    const auto built = scene::buildMotionPack(args.option("name", first->label), packSkeleton, clips,
                                              provenance, options);
    if (!built) {
        fmt::print(stderr, "{}\n", built.error().message);
        return 1;
    }
    const scene::PackValidation validation = scene::validateMotionPack(*built);
    fmt::print("{}", validation.report());
    if (auto ok = scene::writeMotionPack(*built, args.option("out")); !ok) {
        fmt::print(stderr, "{}\n", ok.error().message);
        return 1;
    }
    fmt::print("wrote {}\n", args.option("out"));
    return validation.ok() ? 0 : 2;
}

int cmdValidate(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    const auto pack = scene::readMotionPack(args.positional.front());
    if (!pack) {
        fmt::print(stderr, "{}\n", pack.error().message);
        return 1;
    }
    const scene::PackValidation validation = scene::validateMotionPack(*pack);
    fmt::print("{}", validation.report());
    return validation.ok() ? 0 : 2;
}

// A per-FILE survey of a corpus. Not a sample: "the corpus is Y-up and uses ZXY" is a claim about
// every file in it, and a corpus assembled over time is exactly the kind of thing that has one odd
// file. Reports the two facts that would silently corrupt every downstream number -- the rotation
// orders declared, and which axis the skeleton actually stands up in -- plus the skeleton's shape,
// so that "one retarget profile covers all of this" can be checked rather than assumed.
// Reach: how close a retargeted pose comes to straightening the target's leg.
//
// **The prediction this measures.** Phase 0 noted that a 1.79 m human's locomotion on a 1.66 m
// alien whose rest leg has 0.0098 m of slack will clamp constantly unless stride and hip height
// scale to the target's actual reach. The retarget scales root translation, and whether that is
// enough is a measurement, not an argument. For every frame of every clip this reports the hip-to-
// ankle distance as a fraction of the leg's own length: 1.0 is a straight leg, and anything at or
// above it is a pose foot IK could not have produced and a pose foot IK cannot correct from.
int cmdReach(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    // A pack, or any file `loadSource` reads -- because the whole point of this measurement is to
    // compare retargeted motion against the character's own, and those live in different formats.
    scene::Skeleton skeleton;
    std::vector<scene::AnimationClip> animation;
    std::vector<std::string> clipNames;
    std::vector<float> clipRates;
    const fs::path input = args.positional.front();
    if (fs::is_directory(input)) {
        const auto pack = scene::readMotionPack(input);
        if (!pack) {
            fmt::print(stderr, "{}\n", pack.error().message);
            return 1;
        }
        skeleton = pack->skeleton;
        animation = pack->animation;
        for (const scene::PackClip& pc : pack->clips) {
            clipNames.push_back(pc.name);
            clipRates.push_back(pc.sampleRate > 0.0f ? pc.sampleRate : 30.0f);
        }
    } else {
        auto source = loadSource(input, std::stof(args.option("scale", "1.0")));
        if (!source) {
            fmt::print(stderr, "{}\n", source.error().message);
            return 1;
        }
        skeleton = std::move(source->skeleton);
        animation = std::move(source->clips);
        for (const scene::AnimationClip& clip : animation) {
            clipNames.push_back(clip.name);
            clipRates.push_back(30.0f);
        }
    }
    if (skeleton.joints.empty() || animation.empty()) {
        fmt::print(stderr, "'{}' gave {} joint(s) and {} clip(s); nothing to measure\n",
                   input.string(), skeleton.joints.size(), animation.size());
        return 1;
    }
    const std::vector<std::string> chains = splitCommas(args.option("chain", "thigh_twist.l:leg_stretch.l:foot.l,thigh_twist.r:leg_stretch.r:foot.r"));
    struct Leg {
        std::string name;
        int hip = -1;
        int knee = -1;
        int ankle = -1;
        float length = 0.0f;
    };
    std::vector<Leg> legs;
    scene::Pose rest;
    std::vector<glm::mat4> restModel;
    scene::setRestPose(skeleton, rest);
    scene::poseToModel(skeleton, rest, restModel);
    for (const std::string& spec : chains) {
        std::vector<std::string> parts;
        std::string current;
        for (const char c : spec) {
            if (c == ':') {
                parts.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        parts.push_back(current);
        if (parts.size() != 3) {
            fmt::print(stderr, "chain '{}' is not hip:knee:ankle\n", spec);
            return 1;
        }
        Leg leg;
        leg.name = parts[2];
        leg.hip = skeleton.find(parts[0]);
        leg.knee = skeleton.find(parts[1]);
        leg.ankle = skeleton.find(parts[2]);
        if (leg.hip < 0 || leg.knee < 0 || leg.ankle < 0) {
            fmt::print(stderr, "chain '{}' does not resolve. This skeleton has:\n", spec);
            for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
                fmt::print(stderr, "  {:>3} {}\n", i, skeleton.joints[i].name);
            }
            return 1;
        }
        const glm::vec3 h(restModel[static_cast<std::size_t>(leg.hip)][3]);
        const glm::vec3 k(restModel[static_cast<std::size_t>(leg.knee)][3]);
        const glm::vec3 a(restModel[static_cast<std::size_t>(leg.ankle)][3]);
        leg.length = glm::length(k - h) + glm::length(a - k);
        // **The assertion that the two chains are not the same chain.** A pair computed by one loop
        // is where an off-by-one hides: earlier this session `chains()[layerIndex]` after a
        // post-increment gave the left foot the right foot's data and read one past the end, and it
        // looked entirely plausible. Two legs must resolve to different joints.
        for (const Leg& other : legs) {
            // The ankle, not the hip: two legs legitimately share a pelvis, and on this rig they
            // do. It is the end of the chain that must differ.
            if (other.ankle == leg.ankle) {
                fmt::print(stderr, "chains '{}' and '{}' resolve to the same joints\n", other.name,
                           leg.name);
                return 1;
            }
        }
        fmt::print("{:<18} hip {:>3} knee {:>3} ankle {:>3}  leg {:.4f} m  rest reach {:.4f} m ({:.1f}%)\n",
                   leg.name, leg.hip, leg.knee, leg.ankle, leg.length,
                   glm::length(a - h), 100.0f * glm::length(a - h) / leg.length);
        legs.push_back(leg);
    }

    // **`excursion` is the column that matters on a rig like this one.** How far the ankle moves
    // relative to the body over the clip. A walking character's ankle swings a stride; an ankle
    // that does not move relative to the pelvis is not walking, whatever its legs are doing. It is
    // reported beside reach because a constant reach ratio has two causes -- a leg held at a fixed
    // bend, and a leg the animation never reaches -- and this tells them apart.
    fmt::print("\n{:<24} {:>8} {:>8} {:>9} {:>9} {:>9} {:>10}\n", "clip/leg", "frames", "mean",
               "p99", "worst", ">=99.5%", "excursion");
    std::size_t totalFrames = 0;
    std::size_t totalOver = 0;
    float globalWorst = 0.0f;
    for (std::size_t c = 0; c < animation.size(); ++c) {
        const scene::AnimationClip& clip = animation[c];
        const float rate = clipRates[c];
        const auto frames =
            static_cast<std::size_t>(std::max(2.0f, std::floor(clip.length() * rate + 0.5f) + 1.0f));
        // The datum for "relative to the body" is the joint that carries travel -- ADR-337's rule,
        // the lowest-indexed joint this clip translates. Using joint 0 gives `rig`, an armature
        // wrapper no clip animates, and the subtraction is then a no-op that credits the feet with
        // the whole body's travel: measured, 4.74 m of "excursion" on a clip whose feet never move.
        int datum = 0;
        for (const scene::AnimationChannel& ch : clip.channels) {
            if (ch.path == scene::AnimationPath::Translation &&
                (datum == 0 || static_cast<int>(ch.joint) < datum)) {
                datum = static_cast<int>(ch.joint);
            }
        }
        scene::Pose pose;
        std::vector<glm::mat4> model;
        std::vector<std::vector<float>> ratios(legs.size());
        std::vector<glm::vec3> ankleLo(legs.size());
        std::vector<glm::vec3> ankleHi(legs.size());
        bool firstFrame = true;
        for (std::size_t f = 0; f < frames; ++f) {
            const float t = std::min(clip.start + (static_cast<float>(f) / rate), clip.duration);
            scene::setRestPose(skeleton, pose);
            scene::sampleClip(clip, t, pose);
            scene::poseToModel(skeleton, pose, model);
            for (std::size_t l = 0; l < legs.size(); ++l) {
                const glm::vec3 h(model[static_cast<std::size_t>(legs[l].hip)][3]);
                const glm::vec3 a(model[static_cast<std::size_t>(legs[l].ankle)][3]);
                ratios[l].push_back(glm::length(a - h) / legs[l].length);
                // Relative to the body, so a travelling clip is not credited with foot motion that
                // is really the root going past.
                const glm::vec3 rel = a - glm::vec3(model[static_cast<std::size_t>(datum)][3]);
                if (firstFrame) {
                    ankleLo[l] = rel;
                    ankleHi[l] = rel;
                }
                ankleLo[l] = glm::min(ankleLo[l], rel);
                ankleHi[l] = glm::max(ankleHi[l], rel);
            }
            firstFrame = false;
        }
        for (std::size_t l = 0; l < legs.size(); ++l) {
            std::vector<float>& r = ratios[l];
            if (r.empty()) {
                continue;
            }
            std::sort(r.begin(), r.end());
            double sum = 0.0;
            std::size_t over = 0;
            for (const float v : r) {
                sum += v;
                if (v >= 0.995f) {
                    ++over;
                }
            }
            const float worst = r.back();
            globalWorst = std::max(globalWorst, worst);
            totalFrames += r.size();
            totalOver += over;
            fmt::print("{:<24} {:>8} {:>8.3f} {:>9.3f} {:>9.3f} {:>8.2f}% {:>10.4f}\n",
                       fmt::format("{}/{}", clipNames[c], legs[l].name), r.size(),
                       sum / static_cast<double>(r.size()), r[(r.size() * 99) / 100], worst,
                       100.0 * static_cast<double>(over) / static_cast<double>(r.size()),
                       glm::length(ankleHi[l] - ankleLo[l]));
        }
    }
    fmt::print("\n{} leg-frames, worst reach {:.3f}, {:.3f}% at or past a straight leg\n",
               totalFrames, globalWorst,
               totalFrames > 0 ? 100.0 * static_cast<double>(totalOver) / static_cast<double>(totalFrames) : 0.0);
    return 0;
}

// Phase C §6/§17: build a motion database from a pack, report its memory, and benchmark the
// search on **real motion distributions** rather than a synthetic fixture.
//
// The fixture question is not academic here. ADR-540 measured an early-out as 2.23x SLOWER using
// white-noise queries and 0.80x FASTER using near queries on real data, because a real query is
// close to its answer and far from everything else. So the benchmark below draws its queries from
// the database itself and perturbs them, which is what a character actually asks.
int cmdDatabase(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    const auto pack = scene::readMotionPack(args.positional.front());
    if (!pack) {
        fmt::print(stderr, "{}\n", pack.error().message);
        return 1;
    }
    scene::MotionDatabaseOptions options;
    options.sampleRate = std::stof(args.option("rate", "30"));
    const std::vector<std::string> joints = splitCommas(args.option("joints"));
    if (joints.empty()) {
        fmt::print(stderr, "--joints is required: the feature joints are a property of the "
                           "character, not of the search (Phase C §8)\n");
        return 1;
    }
    options.config.joints = joints;
    if (args.has("trajectory")) {
        for (const std::string& t : splitCommas(args.option("trajectory"))) {
            options.config.trajectoryTimes.push_back(std::stof(t));
        }
    } else {
        options.config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    }
    if (args.has("phase")) {
        options.config.phaseWeight = std::stof(args.option("phase", "1"));
    }
    if (args.has("contacts-feature")) {
        options.config.contactWeight = std::stof(args.option("contacts-feature", "1"));
    }

    const auto t0 = Clock::now();
    auto db = scene::buildMotionDatabase(*pack, options);
    const double buildMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (!db) {
        fmt::print(stderr, "{}\n", db.error().message);
        return 1;
    }
    fmt::print("{}", db->stats.report());
    fmt::print("  build {:.1f} ms  ({:.0f} samples/s)\n", buildMs,
               buildMs > 0.0 ? db->stats.samples / (buildMs / 1000.0) : 0.0);

    // Which dimensions died, named rather than counted, because "the trajectory block is dead"
    // and "the phase block is dead" are completely different facts about a corpus.
    if (db->stats.deadDimensions > 0) {
        const auto perJoint = static_cast<std::uint32_t>(options.config.joints.size()) * 6u;
        const auto trajectory = static_cast<std::uint32_t>(options.config.trajectoryTimes.size()) * 4u;
        std::uint32_t deadJoint = 0;
        std::uint32_t deadTrajectory = 0;
        std::uint32_t deadOther = 0;
        for (std::uint32_t d = 0; d < db->dimension; ++d) {
            if (db->scale[d] != 1.0f) {
                continue;
            }
            // scale==1 is how a dead dimension is stored; a live one whose stddev is exactly 1 is
            // possible in principle and vanishingly unlikely on standardised motion.
            if (d < perJoint) { ++deadJoint; }
            else if (d < perJoint + trajectory) { ++deadTrajectory; }
            else { ++deadOther; }
        }
        fmt::print("  dead: {} joint, {} trajectory, {} other\n", deadJoint, deadTrajectory, deadOther);
        if (deadTrajectory == trajectory && trajectory > 0) {
            fmt::print("  ** every trajectory dimension is dead: this corpus does not travel "
                       "(ADR-540), so motion matching here cannot match on where the body is going\n");
        }
    }

    if (!args.has("bench")) {
        return 0;
    }

    // ---- the benchmark (§17) ------------------------------------------------------------------
    const int repeat = std::stoi(args.option("repeat", "5"));
    const int queries = std::stoi(args.option("queries", "200"));
    scene::MotionCostWeights weights;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<std::uint32_t> pick(0, db->sampleCount() - 1);
    std::normal_distribution<float> jitter(0.0f, 0.15f);

    // Queries drawn from the database and perturbed: what a character asks, which is "something
    // near where I am", not "a random point in feature space".
    std::vector<std::vector<float>> qs;
    std::vector<std::uint32_t> currents;
    qs.reserve(static_cast<std::size_t>(queries));
    for (int i = 0; i < queries; ++i) {
        const std::uint32_t s = pick(rng);
        std::vector<float> f(db->featuresFor(s), db->featuresFor(s) + db->dimension);
        for (float& v : f) {
            v += jitter(rng);
        }
        qs.push_back(std::move(f));
        currents.push_back(s);
    }

    double best = 1e30;
    std::uint64_t considered = 0;
    std::uint64_t rejected = 0;
    double worstSingle = 0.0;
    for (int r = 0; r < repeat; ++r) {
        const auto begin = Clock::now();
        for (int i = 0; i < queries; ++i) {
            scene::MotionQuery query;
            query.features = qs[static_cast<std::size_t>(i)];
            query.current = currents[static_cast<std::size_t>(i)];
            const auto one = Clock::now();
            const scene::MotionMatch m = scene::searchMotion(*db, query, weights);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - one).count();
            worstSingle = std::max(worstSingle, ms);
            considered += m.considered;
            rejected += m.rejected;
        }
        // ADR-170: minima over repeats, never means.
        best = std::min(best,
                        std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
    }
    const double perQuery = best / queries;
    fmt::print("\n  search, minimum of {} run(s) over {} real queries:\n", repeat, queries);
    fmt::print("    {:.4f} ms/query   worst single {:.4f} ms   {:.0f} queries/s\n", perQuery,
               worstSingle, perQuery > 0.0 ? 1000.0 / perQuery : 0.0);
    fmt::print("    {:.0f} samples scored per query, {:.0f} rejected by tag filter\n",
               static_cast<double>(considered) / (repeat * queries),
               static_cast<double>(rejected) / (repeat * queries));
    fmt::print("    {:.1f} ns per sample scored\n",
               considered > 0 ? (best * 1e6 * repeat) / static_cast<double>(considered) : 0.0);
    return 0;
}

int cmdSurvey(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    const float scale = std::stof(args.option("scale", "1.0"));
    std::vector<fs::path> files;
    for (const std::string& entry : args.positional) {
        if (fs::is_directory(entry)) {
            for (const auto& item : fs::recursive_directory_iterator(entry)) {
                if (item.is_regular_file() && item.path().extension() == ".bvh") {
                    files.push_back(item.path());
                }
            }
        } else {
            files.push_back(entry);
        }
    }
    std::sort(files.begin(), files.end());
    fmt::print("surveying {} file(s)\n", files.size());

    std::map<std::string, std::size_t> orderCounts;
    std::map<std::string, std::size_t> skeletonCounts; // digest -> files
    std::map<std::string, std::string> skeletonExample;
    std::map<char, std::size_t> upAxisCounts;
    std::vector<glm::vec3> extents;
    std::map<std::size_t, std::size_t> jointCounts;
    std::size_t failed = 0;
    std::size_t totalFrames = 0;
    double totalSeconds = 0.0;
    std::map<float, std::size_t> frameTimes;

    for (const fs::path& file : files) {
        assets::BvhLoadOptions options;
        options.scale = scale;
        auto bvh = assets::loadBvh(file, options);
        if (!bvh) {
            ++failed;
            if (failed <= 5) {
                fmt::print(stderr, "  FAILED {}: {}\n", file.filename().string(), bvh.error().message);
            }
            continue;
        }
        for (const std::string& order : bvh->rotationOrders) {
            ++orderCounts[order];
        }
        ++jointCounts[bvh->skeleton.jointCount()];
        ++frameTimes[bvh->frameSeconds];
        totalFrames += bvh->frames;
        totalSeconds += static_cast<double>(bvh->frames) * bvh->frameSeconds;
        const std::string digest = scene::skeletonDigest(bvh->skeleton);
        ++skeletonCounts[digest];
        if (skeletonExample.find(digest) == skeletonExample.end()) {
            skeletonExample[digest] = file.filename().string();
        }
        // Which way is up, from the REST pose's shape. Measured per file rather than assumed,
        // because nothing in BVH declares it.
        //
        // **"The largest extent is the height" is wrong on a T-pose, and 100STYLE is T-posed.**
        // Measured on it: fingertip to fingertip is **1.896 m** and head to toe is **1.790 m**, so
        // the naive rule reported a Y-up corpus as X-up -- a claim that, believed, would have made
        // every retarget number meaningless. The extents are printed and the classification is
        // allowed to say it does not know, because a diagnostic that is confidently wrong is worse
        // than one that abstains.
        scene::Pose rest = scene::restPose(bvh->skeleton);
        std::vector<glm::mat4> model;
        scene::poseToModel(bvh->skeleton, rest, model);
        glm::vec3 lo(1e30f);
        glm::vec3 hi(-1e30f);
        for (const glm::mat4& m : model) {
            const glm::vec3 p(m[3]);
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        const glm::vec3 extent = hi - lo;
        extents.push_back(extent);
        float first = std::max({extent.x, extent.y, extent.z});
        float second = 0.0f;
        for (const float e : {extent.x, extent.y, extent.z}) {
            if (e < first) {
                second = std::max(second, e);
            }
        }
        char up = extent.y >= first ? 'Y' : (extent.z >= first ? 'Z' : 'X');
        // Within a quarter of each other, the two largest extents cannot tell a standing rig from
        // a T-posed one. Say so rather than picking.
        if (first > 0.0f && second / first > 0.75f) {
            up = '?';
        }
        ++upAxisCounts[up];
    }

    fmt::print("\nROTATION ORDERS declared (file count per order)\n");
    for (const auto& [order, count] : orderCounts) {
        fmt::print("  {}  {} file(s)\n", order, count);
    }
    fmt::print("\nUP AXIS, from each file's own rest-pose extent\n");
    for (const auto& [axis, count] : upAxisCounts) {
        fmt::print("  {}  {} file(s){}\n", axis, count,
                   axis == '?' ? "   <- AMBIGUOUS: the two largest extents are within 25%, which is "
                                 "what a T-pose looks like. Read the extents below and decide."
                               : "");
    }
    if (!extents.empty()) {
        glm::vec3 lo = extents.front();
        glm::vec3 hi = extents.front();
        for (const glm::vec3& e : extents) {
            lo = glm::min(lo, e);
            hi = glm::max(hi, e);
        }
        fmt::print("  rest-pose extents, min..max over the corpus: x {:.3f}..{:.3f}  y {:.3f}..{:.3f}  "
                   "z {:.3f}..{:.3f}\n",
                   lo.x, hi.x, lo.y, hi.y, lo.z, hi.z);
    }
    fmt::print("\nSKELETON: {} distinct skeleton(s) across {} readable file(s)\n",
               skeletonCounts.size(), files.size() - failed);
    for (const auto& [digest, count] : skeletonCounts) {
        fmt::print("  {}  {:6} file(s)   e.g. {}\n", digest, count, skeletonExample[digest]);
    }
    fmt::print("\nJOINT COUNTS\n");
    for (const auto& [joints, count] : jointCounts) {
        fmt::print("  {:4} joints  {} file(s)\n", joints, count);
    }
    fmt::print("\nFRAME TIMES\n");
    for (const auto& [seconds, count] : frameTimes) {
        fmt::print("  {:.6f} s ({:.1f} Hz)  {} file(s)\n", seconds, 1.0f / seconds, count);
    }
    fmt::print("\nTOTAL: {} frames, {:.1f} s ({:.2f} hours), {} unreadable\n", totalFrames,
               totalSeconds, totalSeconds / 3600.0, failed);
    return failed > 0 ? 1 : 0;
}

int cmdBenchmark(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    const float scale = std::stof(args.option("scale", "1.0"));
    const int repeat = std::stoi(args.option("repeat", "5"));
    const std::string file = args.positional.front();

    // ADR-170: minima over repeats, never means. A mean includes whatever else the machine was
    // doing; a minimum is the closest this hardware got to the work itself.
    const auto timeIt = [&](const char* label, auto&& body) {
        double best = 1e30;
        std::size_t units = 0;
        for (int i = 0; i < repeat; ++i) {
            const auto begin = Clock::now();
            units = body();
            const double ms =
                std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
            best = std::min(best, ms);
        }
        fmt::print("  {:<22} {:9.3f} ms   {:8} unit(s)   {:10.1f}/s\n", label, best, units,
                   units > 0 ? static_cast<double>(units) / (best / 1000.0) : 0.0);
        return best;
    };

    fmt::print("{}  (minimum of {} runs)\n", file, repeat);
    Source source;
    timeIt("import", [&]() -> std::size_t {
        auto loaded = loadSource(file, scale);
        if (!loaded) {
            fmt::print(stderr, "{}\n", loaded.error().message);
            std::exit(1);
        }
        source = std::move(*loaded);
        std::size_t frames = 0;
        for (const scene::AnimationClip& clip : source.clips) {
            frames += static_cast<std::size_t>(clip.length() * 30.0f) + 1;
        }
        return frames;
    });

    std::vector<scene::ContactJoint> joints;
    for (const std::string& name : splitCommas(args.option("contacts"))) {
        joints.push_back(scene::ContactJoint{name, scene::ContactKind::Foot});
    }
    if (!joints.empty()) {
        timeIt("analyse", [&]() -> std::size_t {
            std::size_t frames = 0;
            for (const scene::AnimationClip& clip : source.clips) {
                const scene::ClipAnalysis a = scene::analyseClip(source.skeleton, clip, joints, 0, {});
                frames += a.phase.phase.size();
            }
            return frames;
        });
    }

    scene::Provenance provenance;
    provenance.source = "benchmark";
    provenance.license = "n/a";
    scene::PackBuildOptions options;
    options.contactJoints = joints;
    options.toolVersion = kToolVersion;
    timeIt("pack build", [&]() -> std::size_t {
        const auto built = scene::buildMotionPack("bench", source.skeleton, source.clips, provenance,
                                                  options);
        return built ? built->frameCount() : 0;
    });

    const auto built = scene::buildMotionPack("bench", source.skeleton, source.clips, provenance, options);
    if (built) {
        const fs::path out = fs::temp_directory_path() / "avgen-motion-benchmark";
        timeIt("pack write", [&]() -> std::size_t {
            (void)scene::writeMotionPack(*built, out);
            std::size_t bytes = 0;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(out, ec)) {
                bytes += static_cast<std::size_t>(fs::file_size(entry.path(), ec));
            }
            return bytes;
        });
        timeIt("pack read", [&]() -> std::size_t {
            const auto read = scene::readMotionPack(out);
            return read ? read->frameCount() : 0;
        });
        std::error_code ec;
        fs::remove_all(out, ec);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);
    if (args.command == "inspect") {
        return cmdInspect(args);
    }
    if (args.command == "analyse" || args.command == "analyze") {
        return cmdAnalyse(args);
    }
    if (args.command == "retarget") {
        return cmdRetarget(args);
    }
    if (args.command == "pack") {
        return cmdPack(args);
    }
    if (args.command == "validate") {
        return cmdValidate(args);
    }
    if (args.command == "database") {
        return cmdDatabase(args);
    }
    if (args.command == "reach") {
        return cmdReach(args);
    }
    if (args.command == "survey") {
        return cmdSurvey(args);
    }
    if (args.command == "benchmark") {
        return cmdBenchmark(args);
    }
    return usage();
}
