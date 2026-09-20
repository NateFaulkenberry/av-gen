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
#include "scene/motion_pack.hpp"
#include "scene/retarget.hpp"
#include "scene/scene.hpp"

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
               "  benchmark <file> [--repeat n]           what each stage costs here\n\n"
               "  --scale <f>       BVH units to metres (0.01 for centimetres)\n"
               "  --contacts a,b    joints to analyse; the first is the phase reference\n"
               "  --license <id>    SPDX identifier, REQUIRED by `pack`\n"
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
    fmt::print("{:<28} {:>7} {:>8} {:>8} {:>7} {:>8}  contacts\n", "clip", "len", "travel", "speed",
               "cyclic", "cycle");
    for (const scene::AnimationClip& clip : source->clips) {
        const scene::ClipAnalysis a = scene::analyseClip(source->skeleton, clip, joints, 0, {});
        std::string spans;
        for (const scene::ContactTrack& track : a.contacts) {
            spans += fmt::format(" {}:{}", track.joint, track.spans.size());
        }
        fmt::print("{:<28} {:7.3f} {:8.3f} {:8.3f} {:>7} {:8.3f} {}\n", clip.name, a.length,
                   glm::length(glm::vec3(a.rootTravel.x, 0.0f, a.rootTravel.z)), a.groundSpeed,
                   a.phase.cyclic ? "yes" : "no", a.phase.cycleSeconds, spans);
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

    const auto built = scene::buildMotionPack(args.option("name", first->label), first->skeleton, clips,
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
    if (args.command == "benchmark") {
        return cmdBenchmark(args);
    }
    return usage();
}
