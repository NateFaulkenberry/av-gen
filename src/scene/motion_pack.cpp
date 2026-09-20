#include "scene/motion_pack.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace avgen::scene {
namespace {

using json = nlohmann::json;

constexpr char kPackFile[] = "pack.json";
constexpr char kSkeletonFile[] = "skeleton.bin";
constexpr char kClipsFile[] = "clips.bin";
constexpr char kMetaFile[] = "meta.bin";
constexpr std::uint32_t kBlobMagic = 0x4D505631; // "MPV1"

// FNV-1a over the bytes that decide whether a pack belongs to a rig. Not a cryptographic digest --
// the question is "is this the same skeleton", not "has someone tampered with it".
std::string digestOf(const std::string& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return fmt::format("{:016x}", hash);
}

void appendFloat(std::string& out, float value) {
    char buffer[sizeof(float)];
    std::memcpy(buffer, &value, sizeof(float));
    out.append(buffer, sizeof(float));
}

void appendU32(std::string& out, std::uint32_t value) {
    char buffer[sizeof(std::uint32_t)];
    std::memcpy(buffer, &value, sizeof(std::uint32_t));
    out.append(buffer, sizeof(std::uint32_t));
}

bool readU32(const std::string& data, std::size_t& at, std::uint32_t& out) {
    if (at + sizeof(std::uint32_t) > data.size()) {
        return false;
    }
    std::memcpy(&out, data.data() + at, sizeof(std::uint32_t));
    at += sizeof(std::uint32_t);
    return true;
}

bool readFloat(const std::string& data, std::size_t& at, float& out) {
    if (at + sizeof(float) > data.size()) {
        return false;
    }
    std::memcpy(&out, data.data() + at, sizeof(float));
    at += sizeof(float);
    return true;
}

Result<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail("cannot open '{}'", path.string());
    }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return data;
}

Result<void> writeFile(const std::filesystem::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return fail("cannot write '{}'", path.string());
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        return fail("failed while writing '{}'", path.string());
    }
    return {};
}

json provenanceToJson(const Provenance& p) {
    json j;
    j["source"] = p.source;
    j["sourceFile"] = p.sourceFile;
    j["creator"] = p.creator;
    j["license"] = p.license;
    j["licenseUrl"] = p.licenseUrl;
    j["attributionRequired"] = p.attributionRequired;
    j["redistribution"] = redistributionName(p.redistribution);
    j["derivedDataAllowed"] = p.derivedDataAllowed;
    j["trainingAllowed"] = p.trainingAllowed;
    j["processing"] = p.processing;
    j["toolVersion"] = p.toolVersion;
    if (!p.notes.empty()) {
        j["notes"] = p.notes;
    }
    return j;
}

Result<Provenance> provenanceFromJson(const json& j) {
    Provenance p;
    if (!j.is_object()) {
        return fail("provenance: expected an object");
    }
    p.source = j.value("source", std::string());
    p.sourceFile = j.value("sourceFile", std::string());
    p.creator = j.value("creator", std::string());
    p.license = j.value("license", std::string());
    p.licenseUrl = j.value("licenseUrl", std::string());
    p.attributionRequired = j.value("attributionRequired", true);
    // **An unknown or absent redistribution state is `RequiresReview`, never `Allowed`.** A reader
    // that defaulted the other way would turn a field nobody filled in into a shipping permission.
    p.redistribution = Redistribution::RequiresReview;
    if (j.contains("redistribution") && j.at("redistribution").is_string()) {
        Redistribution parsed = Redistribution::RequiresReview;
        if (redistributionFromName(j.at("redistribution").get<std::string>(), parsed)) {
            p.redistribution = parsed;
        }
    }
    p.derivedDataAllowed = j.value("derivedDataAllowed", false);
    p.trainingAllowed = j.value("trainingAllowed", false);
    if (j.contains("processing") && j.at("processing").is_array()) {
        for (const json& step : j.at("processing")) {
            if (step.is_string()) {
                p.processing.push_back(step.get<std::string>());
            }
        }
    }
    p.toolVersion = j.value("toolVersion", std::string());
    p.notes = j.value("notes", std::string());
    return p;
}

json contactsToJson(const std::vector<ContactTrack>& tracks) {
    json out = json::array();
    for (const ContactTrack& track : tracks) {
        json t;
        t["joint"] = track.joint;
        t["kind"] = contactKindName(track.kind);
        t["dutyCycle"] = track.dutyCycle;
        t["lowest"] = track.lowest;
        json spans = json::array();
        for (const ContactSpan& span : track.spans) {
            spans.push_back(json::array({span.start, span.end, span.clipLength}));
        }
        t["spans"] = std::move(spans);
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<ContactTrack> contactsFromJson(const json& j) {
    std::vector<ContactTrack> out;
    if (!j.is_array()) {
        return out;
    }
    for (const json& t : j) {
        ContactTrack track;
        track.joint = t.value("joint", std::string());
        ContactKind kind = ContactKind::Foot;
        if (t.contains("kind") && t.at("kind").is_string()) {
            (void)contactKindFromName(t.at("kind").get<std::string>(), kind);
        }
        track.kind = kind;
        track.dutyCycle = t.value("dutyCycle", 0.0f);
        track.lowest = t.value("lowest", 0.0f);
        if (t.contains("spans") && t.at("spans").is_array()) {
            for (const json& s : t.at("spans")) {
                if (s.is_array() && s.size() == 3) {
                    track.spans.push_back(ContactSpan{s[0].get<float>(), s[1].get<float>(), s[2].get<float>()});
                }
            }
        }
        out.push_back(std::move(track));
    }
    return out;
}

} // namespace

const char* redistributionName(Redistribution value) {
    switch (value) {
    case Redistribution::Allowed: return "allowed";
    case Redistribution::Forbidden: return "forbidden";
    case Redistribution::RequiresReview: return "REQUIRES_REVIEW";
    }
    return "REQUIRES_REVIEW";
}

bool redistributionFromName(std::string_view name, Redistribution& out) {
    if (name == "allowed") { out = Redistribution::Allowed; return true; }
    if (name == "forbidden") { out = Redistribution::Forbidden; return true; }
    if (name == "REQUIRES_REVIEW") { out = Redistribution::RequiresReview; return true; }
    return false;
}

std::string skeletonDigest(const Skeleton& skeleton) {
    std::string bytes;
    bytes.reserve(skeleton.joints.size() * 48);
    for (const Joint& joint : skeleton.joints) {
        bytes += joint.name;
        bytes += '\0';
        appendU32(bytes, static_cast<std::uint32_t>(joint.parent + 1));
        appendFloat(bytes, joint.rest.position.x);
        appendFloat(bytes, joint.rest.position.y);
        appendFloat(bytes, joint.rest.position.z);
        appendFloat(bytes, joint.rest.rotation.x);
        appendFloat(bytes, joint.rest.rotation.y);
        appendFloat(bytes, joint.rest.rotation.z);
        appendFloat(bytes, joint.rest.rotation.w);
    }
    return digestOf(bytes);
}

std::uint32_t MotionPack::frameCount() const {
    std::uint32_t total = 0;
    for (const PackClip& clip : clips) {
        total += clip.frames;
    }
    return total;
}

Redistribution MotionPack::redistribution() const {
    // The worst state wins: a pack is shippable only when every clip in it is.
    Redistribution worst = Redistribution::Allowed;
    for (const PackClip& clip : clips) {
        if (clip.provenance >= provenance.size()) {
            return Redistribution::RequiresReview;
        }
        const Redistribution state = provenance[clip.provenance].redistribution;
        if (state == Redistribution::Forbidden) {
            return Redistribution::Forbidden;
        }
        if (state == Redistribution::RequiresReview) {
            worst = Redistribution::RequiresReview;
        }
    }
    return clips.empty() ? Redistribution::RequiresReview : worst;
}

int MotionPack::findClip(std::string_view clipName) const {
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == clipName) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Result<MotionPack> buildMotionPack(std::string name, const Skeleton& skeleton,
                                   const std::vector<AnimationClip>& clips, const Provenance& provenance,
                                   const PackBuildOptions& options) {
    // **The licence check is a refusal, not a warning.** ADR-542: a warning in a build log is how
    // research assets ship.
    if (provenance.license.empty()) {
        return fail("motion pack '{}': provenance has no licence. A pack without one does not build "
                    "-- set it, or mark the source REQUIRES_REVIEW and say why in `notes`",
                    name);
    }
    if (provenance.source.empty()) {
        return fail("motion pack '{}': provenance has no source. A derived database whose ancestry "
                    "cannot be printed is one nobody can clear",
                    name);
    }
    if (!skeleton.valid()) {
        return fail("motion pack '{}': the skeleton is not valid", name);
    }

    MotionPack pack;
    pack.version = kMotionPackVersion;
    pack.name = std::move(name);
    pack.skeleton = skeleton;
    pack.skeletonDigest = skeletonDigest(skeleton);
    pack.provenance.push_back(provenance);
    pack.provenance.front().toolVersion =
        options.toolVersion.empty() ? provenance.toolVersion : options.toolVersion;

    ContactSettings settings = options.contacts;
    settings.sampleRate = options.sampleRate;
    for (const AnimationClip& clip : clips) {
        PackClip entry;
        entry.name = clip.name;
        entry.length = clip.length();
        entry.sampleRate = options.sampleRate;
        entry.frames = static_cast<std::uint32_t>(
            std::max(2.0f, std::floor(entry.length * options.sampleRate + 0.5f) + 1.0f));
        entry.provenance = 0;
        if (!options.contactJoints.empty()) {
            ClipAnalysis analysis = analyseClip(skeleton, clip, options.contactJoints, 0, settings);
            entry.contacts = std::move(analysis.contacts);
            entry.phase = std::move(analysis.phase);
            entry.rootTravel = analysis.rootTravel;
            entry.groundSpeed = analysis.groundSpeed;
        }
        pack.clips.push_back(std::move(entry));
        pack.animation.push_back(clip);
    }
    return pack;
}

PackValidation validateMotionPack(const MotionPack& pack) {
    PackValidation out;
    out.clips = static_cast<std::uint32_t>(pack.clips.size());
    out.frames = pack.frameCount();

    if (pack.version != kMotionPackVersion) {
        out.errors.push_back(fmt::format("pack version {} is not {}", pack.version, kMotionPackVersion));
    }
    if (!pack.skeleton.valid()) {
        out.errors.push_back("the skeleton is not valid");
    }
    if (pack.skeletonDigest != skeletonDigest(pack.skeleton)) {
        out.errors.push_back("the recorded skeleton digest does not match the skeleton in the pack");
    }
    if (pack.clips.size() != pack.animation.size()) {
        out.errors.push_back(fmt::format("{} clip entries against {} animations", pack.clips.size(),
                                         pack.animation.size()));
    }
    if (pack.provenance.empty()) {
        out.errors.push_back("no provenance: a pack must say where its motion came from");
    }
    for (std::size_t i = 0; i < pack.provenance.size(); ++i) {
        const Provenance& p = pack.provenance[i];
        if (p.license.empty()) {
            out.errors.push_back(fmt::format("provenance {}: no licence", i));
        }
        if (p.source.empty()) {
            out.errors.push_back(fmt::format("provenance {}: no source", i));
        }
    }

    for (const PackClip& clip : pack.clips) {
        if (clip.provenance >= pack.provenance.size()) {
            out.errors.push_back(fmt::format("clip '{}': provenance index {} is out of range",
                                             clip.name, clip.provenance));
            continue;
        }
        switch (pack.provenance[clip.provenance].redistribution) {
        case Redistribution::Allowed: break;
        case Redistribution::Forbidden: ++out.clipsForbidden; break;
        case Redistribution::RequiresReview: ++out.clipsRequiringReview; break;
        }
        if (clip.contacts.empty()) {
            ++out.clipsWithoutContacts;
        }
        if (clip.phase.empty()) {
            ++out.clipsWithoutPhase;
        } else if (!clip.phase.cyclic) {
            ++out.clipsNotCyclic;
        }
    }
    if (out.clipsForbidden > 0) {
        out.errors.push_back(fmt::format(
            "{} clip(s) come from a source that forbids redistribution; this pack must not ship",
            out.clipsForbidden));
    }
    if (out.clipsRequiringReview > 0) {
        out.warnings.push_back(fmt::format(
            "{} clip(s) have an unresolved licence (REQUIRES_REVIEW); treat as not shippable until a "
            "human says otherwise",
            out.clipsRequiringReview));
    }
    if (out.clipsWithoutContacts > 0) {
        out.warnings.push_back(fmt::format("{} clip(s) have no contact tracks", out.clipsWithoutContacts));
    }
    if (out.clipsNotCyclic > 0) {
        out.warnings.push_back(
            fmt::format("{} clip(s) have a phase but no cycle; a phase-matched transition into one "
                        "will start at frame zero",
                        out.clipsNotCyclic));
    }
    return out;
}

std::string PackValidation::report() const {
    std::string out = "MotionPack validation\n";
    out += fmt::format("Clips: {}   Frames: {}\n", clips, frames);
    out += fmt::format("Without contacts: {}   Without phase: {}   Not cyclic: {}\n",
                       clipsWithoutContacts, clipsWithoutPhase, clipsNotCyclic);
    out += fmt::format("Licence: {} forbidden, {} require review\n", clipsForbidden, clipsRequiringReview);
    for (const std::string& error : errors) {
        out += "  ERROR   " + error + "\n";
    }
    for (const std::string& warning : warnings) {
        out += "  warning " + warning + "\n";
    }
    if (!errors.empty()) {
        out += "Status: FAIL\n";
    } else if (!warnings.empty()) {
        out += "Status: PASS WITH WARNINGS\n";
    } else {
        out += "Status: PASS\n";
    }
    return out;
}

Result<void> writeMotionPack(const MotionPack& pack, const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return fail("cannot create '{}': {}", directory.string(), ec.message());
    }

    // ---- skeleton.bin -------------------------------------------------------------------------
    std::string skeletonBlob;
    appendU32(skeletonBlob, kBlobMagic);
    appendU32(skeletonBlob, static_cast<std::uint32_t>(pack.skeleton.joints.size()));
    for (const Joint& joint : pack.skeleton.joints) {
        appendU32(skeletonBlob, static_cast<std::uint32_t>(joint.name.size()));
        skeletonBlob += joint.name;
        appendU32(skeletonBlob, static_cast<std::uint32_t>(joint.parent + 1));
        for (const float v : {joint.rest.position.x, joint.rest.position.y, joint.rest.position.z,
                              joint.rest.rotation.x, joint.rest.rotation.y, joint.rest.rotation.z,
                              joint.rest.rotation.w, joint.rest.scale.x, joint.rest.scale.y,
                              joint.rest.scale.z}) {
            appendFloat(skeletonBlob, v);
        }
    }

    // ---- clips.bin ----------------------------------------------------------------------------
    // Channels as they are, rather than a resampled pose matrix. A pack's clips have already been
    // through a resampling (the retarget, or the import), and resampling again to store them would
    // be a second lossy step for no gain.
    std::string clipsBlob;
    appendU32(clipsBlob, kBlobMagic);
    appendU32(clipsBlob, static_cast<std::uint32_t>(pack.animation.size()));
    for (const AnimationClip& clip : pack.animation) {
        appendU32(clipsBlob, static_cast<std::uint32_t>(clip.name.size()));
        clipsBlob += clip.name;
        appendFloat(clipsBlob, clip.start);
        appendFloat(clipsBlob, clip.duration);
        appendU32(clipsBlob, static_cast<std::uint32_t>(clip.channels.size()));
        for (const AnimationChannel& channel : clip.channels) {
            appendU32(clipsBlob, channel.joint);
            appendU32(clipsBlob, static_cast<std::uint32_t>(channel.path));
            appendU32(clipsBlob, static_cast<std::uint32_t>(channel.interpolation));
            appendU32(clipsBlob, static_cast<std::uint32_t>(channel.times.size()));
            for (const float t : channel.times) {
                appendFloat(clipsBlob, t);
            }
            appendU32(clipsBlob, static_cast<std::uint32_t>(channel.values.size()));
            for (const glm::vec4& v : channel.values) {
                appendFloat(clipsBlob, v.x);
                appendFloat(clipsBlob, v.y);
                appendFloat(clipsBlob, v.z);
                appendFloat(clipsBlob, v.w);
            }
        }
    }

    // ---- meta.bin -----------------------------------------------------------------------------
    std::string metaBlob;
    appendU32(metaBlob, kBlobMagic);
    appendU32(metaBlob, static_cast<std::uint32_t>(pack.clips.size()));
    for (const PackClip& clip : pack.clips) {
        appendFloat(metaBlob, clip.phase.sampleRate);
        appendU32(metaBlob, clip.phase.cyclic ? 1u : 0u);
        appendFloat(metaBlob, clip.phase.cycleSeconds);
        appendFloat(metaBlob, clip.phase.cycleVariance);
        appendU32(metaBlob, static_cast<std::uint32_t>(clip.phase.phase.size()));
        for (const float p : clip.phase.phase) {
            appendFloat(metaBlob, p);
        }
    }

    // ---- pack.json ----------------------------------------------------------------------------
    json doc;
    doc["format"] = "avgen-motionpack";
    doc["version"] = pack.version;
    doc["name"] = pack.name;
    doc["skeletonDigest"] = pack.skeletonDigest;
    doc["joints"] = pack.skeleton.joints.size();
    json prov = json::array();
    for (const Provenance& p : pack.provenance) {
        prov.push_back(provenanceToJson(p));
    }
    doc["provenance"] = std::move(prov);
    json clipIndex = json::array();
    for (const PackClip& clip : pack.clips) {
        json c;
        c["name"] = clip.name;
        c["length"] = clip.length;
        c["sampleRate"] = clip.sampleRate;
        c["frames"] = clip.frames;
        c["loop"] = clip.loop;
        c["provenance"] = clip.provenance;
        c["groundSpeed"] = clip.groundSpeed;
        c["rootTravel"] = json::array({clip.rootTravel.x, clip.rootTravel.y, clip.rootTravel.z});
        if (!clip.tags.empty()) {
            c["tags"] = clip.tags;
        }
        c["contacts"] = contactsToJson(clip.contacts);
        clipIndex.push_back(std::move(c));
    }
    doc["clips"] = std::move(clipIndex);

    if (auto ok = writeFile(directory / kSkeletonFile, skeletonBlob); !ok) {
        return ok;
    }
    if (auto ok = writeFile(directory / kClipsFile, clipsBlob); !ok) {
        return ok;
    }
    if (auto ok = writeFile(directory / kMetaFile, metaBlob); !ok) {
        return ok;
    }
    return writeFile(directory / kPackFile, doc.dump(1));
}

Result<MotionPack> readMotionPack(const std::filesystem::path& directory) {
    auto packText = readFile(directory / kPackFile);
    if (!packText) {
        return std::unexpected(packText.error());
    }
    json doc;
    try {
        doc = json::parse(*packText);
    } catch (const json::exception& e) {
        return fail("motion pack '{}': {}", directory.string(), e.what());
    }
    if (doc.value("format", std::string()) != "avgen-motionpack") {
        return fail("motion pack '{}': not an avgen motion pack", directory.string());
    }
    MotionPack pack;
    pack.version = doc.value("version", 0u);
    if (pack.version != kMotionPackVersion) {
        // Refused rather than guessed. A pack half-read is a character that animates wrongly
        // instead of not at all, which is the harder failure to notice.
        return fail("motion pack '{}': version {} cannot be read by this build, which knows {}",
                    directory.string(), pack.version, kMotionPackVersion);
    }
    pack.name = doc.value("name", std::string());
    pack.skeletonDigest = doc.value("skeletonDigest", std::string());
    if (doc.contains("provenance") && doc.at("provenance").is_array()) {
        for (const json& p : doc.at("provenance")) {
            auto parsed = provenanceFromJson(p);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            pack.provenance.push_back(std::move(*parsed));
        }
    }

    auto skeletonBlob = readFile(directory / kSkeletonFile);
    if (!skeletonBlob) {
        return std::unexpected(skeletonBlob.error());
    }
    std::size_t at = 0;
    std::uint32_t magic = 0;
    std::uint32_t count = 0;
    if (!readU32(*skeletonBlob, at, magic) || magic != kBlobMagic) {
        return fail("motion pack '{}': skeleton.bin has the wrong magic", directory.string());
    }
    if (!readU32(*skeletonBlob, at, count)) {
        return fail("motion pack '{}': skeleton.bin is truncated", directory.string());
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t nameLength = 0;
        if (!readU32(*skeletonBlob, at, nameLength) || at + nameLength > skeletonBlob->size()) {
            return fail("motion pack '{}': skeleton.bin is truncated at joint {}", directory.string(), i);
        }
        Joint joint;
        joint.name = skeletonBlob->substr(at, nameLength);
        at += nameLength;
        std::uint32_t parent = 0;
        if (!readU32(*skeletonBlob, at, parent)) {
            return fail("motion pack '{}': skeleton.bin is truncated at joint {}", directory.string(), i);
        }
        joint.parent = static_cast<int>(parent) - 1;
        float v[10]{};
        for (float& f : v) {
            if (!readFloat(*skeletonBlob, at, f)) {
                return fail("motion pack '{}': skeleton.bin is truncated at joint {}", directory.string(), i);
            }
        }
        joint.rest.position = {v[0], v[1], v[2]};
        joint.rest.rotation = glm::quat(v[6], v[3], v[4], v[5]);
        joint.rest.scale = {v[7], v[8], v[9]};
        pack.skeleton.joints.push_back(std::move(joint));
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        pack.skeleton.palette.push_back(i);
        pack.skeleton.inverseBind.emplace_back(1.0f);
    }

    auto clipsBlob = readFile(directory / kClipsFile);
    if (!clipsBlob) {
        return std::unexpected(clipsBlob.error());
    }
    at = 0;
    if (!readU32(*clipsBlob, at, magic) || magic != kBlobMagic) {
        return fail("motion pack '{}': clips.bin has the wrong magic", directory.string());
    }
    std::uint32_t clipCount = 0;
    if (!readU32(*clipsBlob, at, clipCount)) {
        return fail("motion pack '{}': clips.bin is truncated", directory.string());
    }
    for (std::uint32_t c = 0; c < clipCount; ++c) {
        AnimationClip clip;
        std::uint32_t nameLength = 0;
        if (!readU32(*clipsBlob, at, nameLength) || at + nameLength > clipsBlob->size()) {
            return fail("motion pack '{}': clips.bin is truncated at clip {}", directory.string(), c);
        }
        clip.name = clipsBlob->substr(at, nameLength);
        at += nameLength;
        if (!readFloat(*clipsBlob, at, clip.start) || !readFloat(*clipsBlob, at, clip.duration)) {
            return fail("motion pack '{}': clips.bin is truncated at clip {}", directory.string(), c);
        }
        std::uint32_t channelCount = 0;
        if (!readU32(*clipsBlob, at, channelCount)) {
            return fail("motion pack '{}': clips.bin is truncated at clip {}", directory.string(), c);
        }
        for (std::uint32_t k = 0; k < channelCount; ++k) {
            AnimationChannel channel;
            std::uint32_t path = 0;
            std::uint32_t interpolation = 0;
            std::uint32_t times = 0;
            std::uint32_t values = 0;
            if (!readU32(*clipsBlob, at, channel.joint) || !readU32(*clipsBlob, at, path) ||
                !readU32(*clipsBlob, at, interpolation) || !readU32(*clipsBlob, at, times)) {
                return fail("motion pack '{}': clips.bin is truncated in clip '{}'", directory.string(),
                            clip.name);
            }
            channel.path = static_cast<AnimationPath>(path);
            channel.interpolation = static_cast<Interpolation>(interpolation);
            channel.times.resize(times);
            for (float& t : channel.times) {
                if (!readFloat(*clipsBlob, at, t)) {
                    return fail("motion pack '{}': clips.bin is truncated in clip '{}'",
                                directory.string(), clip.name);
                }
            }
            if (!readU32(*clipsBlob, at, values)) {
                return fail("motion pack '{}': clips.bin is truncated in clip '{}'", directory.string(),
                            clip.name);
            }
            channel.values.resize(values);
            for (glm::vec4& v : channel.values) {
                if (!readFloat(*clipsBlob, at, v.x) || !readFloat(*clipsBlob, at, v.y) ||
                    !readFloat(*clipsBlob, at, v.z) || !readFloat(*clipsBlob, at, v.w)) {
                    return fail("motion pack '{}': clips.bin is truncated in clip '{}'",
                                directory.string(), clip.name);
                }
            }
            clip.channels.push_back(std::move(channel));
        }
        pack.animation.push_back(std::move(clip));
    }

    auto metaBlob = readFile(directory / kMetaFile);
    if (!metaBlob) {
        return std::unexpected(metaBlob.error());
    }
    at = 0;
    std::uint32_t metaCount = 0;
    if (!readU32(*metaBlob, at, magic) || magic != kBlobMagic || !readU32(*metaBlob, at, metaCount)) {
        return fail("motion pack '{}': meta.bin has the wrong magic or is truncated", directory.string());
    }

    const json& clipIndex = doc.contains("clips") ? doc.at("clips") : json::array();
    if (!clipIndex.is_array()) {
        return fail("motion pack '{}': 'clips' must be an array", directory.string());
    }
    if (clipIndex.size() != metaCount) {
        return fail("motion pack '{}': {} clip entries against {} phase records", directory.string(),
                    clipIndex.size(), metaCount);
    }
    for (std::size_t i = 0; i < clipIndex.size(); ++i) {
        const json& c = clipIndex[i];
        PackClip clip;
        clip.name = c.value("name", std::string());
        clip.length = c.value("length", 0.0f);
        clip.sampleRate = c.value("sampleRate", 30.0f);
        clip.frames = c.value("frames", 0u);
        clip.loop = c.value("loop", true);
        clip.provenance = c.value("provenance", 0u);
        clip.groundSpeed = c.value("groundSpeed", 0.0f);
        if (c.contains("rootTravel") && c.at("rootTravel").is_array() && c.at("rootTravel").size() == 3) {
            clip.rootTravel = {c.at("rootTravel")[0].get<float>(), c.at("rootTravel")[1].get<float>(),
                               c.at("rootTravel")[2].get<float>()};
        }
        if (c.contains("tags") && c.at("tags").is_array()) {
            for (const json& tag : c.at("tags")) {
                if (tag.is_string()) {
                    clip.tags.push_back(tag.get<std::string>());
                }
            }
        }
        if (c.contains("contacts")) {
            clip.contacts = contactsFromJson(c.at("contacts"));
        }
        std::uint32_t cyclic = 0;
        std::uint32_t samples = 0;
        if (!readFloat(*metaBlob, at, clip.phase.sampleRate) || !readU32(*metaBlob, at, cyclic) ||
            !readFloat(*metaBlob, at, clip.phase.cycleSeconds) ||
            !readFloat(*metaBlob, at, clip.phase.cycleVariance) || !readU32(*metaBlob, at, samples)) {
            return fail("motion pack '{}': meta.bin is truncated at clip {}", directory.string(), i);
        }
        clip.phase.cyclic = cyclic != 0;
        clip.phase.phase.resize(samples);
        for (float& p : clip.phase.phase) {
            if (!readFloat(*metaBlob, at, p)) {
                return fail("motion pack '{}': meta.bin is truncated at clip {}", directory.string(), i);
            }
        }
        pack.clips.push_back(std::move(clip));
    }
    return pack;
}

} // namespace avgen::scene
