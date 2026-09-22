#include "scene/motion_database_io.hpp"

#include <nlohmann/json.hpp>

#include <fmt/format.h>

#include <bit>
#include <cstring>
#include <fstream>
#include <span>
#include <system_error>

namespace avgen::scene {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

static_assert(std::endian::native == std::endian::little,
              "the .motiondb payload is written as raw little-endian arrays");

constexpr std::uint32_t kMagic = 0x444D5641u; // "AVMD"

// FNV-1a, 64-bit, consuming eight bytes per step on the bulk arrays. **Not a cryptographic hash and
// not meant to be one**: it identifies content for a cache and catches corruption and mismatched
// inputs, and at a million samples it has 145 MB to get through, so byte-at-a-time would cost more
// than the load it protects.
class Digest {
public:
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        std::size_t i = 0;
        for (; i + 8 <= size; i += 8) {
            std::uint64_t word = 0;
            std::memcpy(&word, p + i, 8);
            hash_ = (hash_ ^ word) * kPrime;
        }
        for (; i < size; ++i) {
            hash_ = (hash_ ^ p[i]) * kPrime;
        }
        // Length-terminated, so ["ab","c"] and ["a","bc"] digest differently.
        hash_ = (hash_ ^ static_cast<std::uint64_t>(size)) * kPrime;
    }
    void text(std::string_view s) { bytes(s.data(), s.size()); }
    template <typename T>
    void value(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        bytes(&v, sizeof(T));
    }
    template <typename T>
    void array(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        bytes(v.data(), v.size() * sizeof(T));
    }
    [[nodiscard]] std::uint64_t result() const { return hash_; }
    [[nodiscard]] std::string hex() const { return fmt::format("{:016x}", hash_); }

private:
    static constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash_ = 1469598103934665603ULL;
};

void digestSchema(Digest& d, const MotionFeatureConfig& config) {
    // How each dimension is computed, not only which exist (§36).
    d.value(kMotionFeatureExtractionVersion);
    d.value(static_cast<std::uint32_t>(config.joints.size()));
    for (const std::string& j : config.joints) {
        d.text(j);
    }
    // Only when the contact block exists does which joints carry it change a dimension's meaning.
    const bool contacts = config.contactWeight > 0.0f;
    d.value(static_cast<std::uint8_t>(contacts));
    if (contacts) {
        for (const std::string& j : config.contactJointNames()) {
            d.text(j);
        }
    }
    d.array(config.trajectoryTimes);
    d.value(config.facingWindow);
    d.value(static_cast<std::uint8_t>(config.phaseWeight > 0.0f));
    d.value(config.dimension());
}

void digestWeights(Digest& d, const MotionFeatureConfig& config) {
    d.value(config.jointPositionWeight);
    d.value(config.jointVelocityWeight);
    d.value(config.trajectoryPositionWeight);
    d.value(config.trajectoryFacingWeight);
    d.value(config.rootVelocityWeight);
    d.value(config.phaseWeight);
    d.value(config.contactWeight);
}

// ---- JSON for the header -------------------------------------------------------------------------

json configToJson(const MotionFeatureConfig& c) {
    json j;
    j["joints"] = c.joints;
    j["contactJoints"] = c.contactJoints;
    j["trajectoryTimes"] = c.trajectoryTimes;
    j["facingWindow"] = c.facingWindow;
    j["jointPositionWeight"] = c.jointPositionWeight;
    j["jointVelocityWeight"] = c.jointVelocityWeight;
    j["trajectoryPositionWeight"] = c.trajectoryPositionWeight;
    j["trajectoryFacingWeight"] = c.trajectoryFacingWeight;
    j["rootVelocityWeight"] = c.rootVelocityWeight;
    j["phaseWeight"] = c.phaseWeight;
    j["contactWeight"] = c.contactWeight;
    return j;
}

MotionFeatureConfig configFromJson(const json& j) {
    MotionFeatureConfig c;
    c.joints = j.at("joints").get<std::vector<std::string>>();
    c.contactJoints = j.at("contactJoints").get<std::vector<std::string>>();
    c.trajectoryTimes = j.at("trajectoryTimes").get<std::vector<float>>();
    // Absent from a file written before extraction version 3. Such a file is refused by the
    // schema check, which says why; reading it must not throw first and say something vaguer.
    c.facingWindow = j.value("facingWindow", 0.0f);
    c.jointPositionWeight = j.at("jointPositionWeight").get<float>();
    c.jointVelocityWeight = j.at("jointVelocityWeight").get<float>();
    c.trajectoryPositionWeight = j.at("trajectoryPositionWeight").get<float>();
    c.trajectoryFacingWeight = j.at("trajectoryFacingWeight").get<float>();
    c.rootVelocityWeight = j.at("rootVelocityWeight").get<float>();
    c.phaseWeight = j.at("phaseWeight").get<float>();
    c.contactWeight = j.at("contactWeight").get<float>();
    return c;
}

json headerToJson(const MotionDatabase& db) {
    json j;
    j["version"] = db.version;
    j["name"] = db.name;
    j["skeletonDigest"] = db.skeletonDigest;
    j["config"] = configToJson(db.config);
    j["dimension"] = db.dimension;
    j["samples"] = db.sampleCount();
    j["clipNames"] = db.clipNames;
    // As a hex string: JSON numbers are doubles and a 64-bit digest does not survive one.
    j["identity"] = fmt::format("{:016x}", db.identity);
    json s;
    s["clips"] = db.stats.clips;
    s["samples"] = db.stats.samples;
    s["dimension"] = db.stats.dimension;
    s["featureBytes"] = db.stats.featureBytes;
    s["metadataBytes"] = db.stats.metadataBytes;
    s["deadDimensions"] = db.stats.deadDimensions;
    s["costSpread"] = db.stats.costSpread;
    s["jointRadiusSpread"] = db.stats.jointRadiusSpread;
    s["jointNames"] = db.stats.jointNames;
    j["stats"] = s;
    json b;
    b["sourcePackDigest"] = db.build.sourcePackDigest;
    b["featureSchema"] = db.build.featureSchema;
    b["sampleRate"] = db.build.sampleRate;
    b["toolVersion"] = db.build.toolVersion;
    b["buildKey"] = db.build.buildKey;
    j["build"] = b;
    return j;
}

Result<MotionDatabase> headerFromJson(const json& j, const fs::path& file) {
    MotionDatabase db;
    try {
        db.version = j.at("version").get<std::uint32_t>();
        if (db.version != MotionDatabase::kVersion) {
            return fail("motion database '{}': database version {} cannot be read by this build, "
                        "which knows {}",
                        file.string(), db.version, MotionDatabase::kVersion);
        }
        db.name = j.at("name").get<std::string>();
        db.skeletonDigest = j.at("skeletonDigest").get<std::string>();
        db.config = configFromJson(j.at("config"));
        db.dimension = j.at("dimension").get<std::uint32_t>();
        db.clipNames = j.at("clipNames").get<std::vector<std::string>>();
        db.identity = std::stoull(j.at("identity").get<std::string>(), nullptr, 16);
        const json& s = j.at("stats");
        db.stats.clips = s.at("clips").get<std::uint32_t>();
        db.stats.samples = s.at("samples").get<std::uint32_t>();
        db.stats.dimension = s.at("dimension").get<std::uint32_t>();
        db.stats.featureBytes = s.at("featureBytes").get<std::size_t>();
        db.stats.metadataBytes = s.at("metadataBytes").get<std::size_t>();
        db.stats.deadDimensions = s.at("deadDimensions").get<std::uint32_t>();
        db.stats.costSpread = s.at("costSpread").get<float>();
        db.stats.jointRadiusSpread = s.at("jointRadiusSpread").get<std::vector<float>>();
        db.stats.jointNames = s.at("jointNames").get<std::vector<std::string>>();
        const json& b = j.at("build");
        db.build.sourcePackDigest = b.at("sourcePackDigest").get<std::string>();
        db.build.featureSchema = b.at("featureSchema").get<std::string>();
        db.build.sampleRate = b.at("sampleRate").get<float>();
        db.build.toolVersion = b.at("toolVersion").get<std::string>();
        db.build.buildKey = b.at("buildKey").get<std::string>();
    } catch (const std::exception& e) {
        return fail("motion database '{}': header is malformed: {}", file.string(), e.what());
    }
    // §36: the schema is recomputed from the stored config rather than trusted, so a file whose
    // config and recorded schema disagree -- hand-edited, or written by a builder whose schema
    // definition has since changed -- is refused instead of searched with a misread layout.
    const std::string schema = motionFeatureSchemaDigest(db.config);
    if (!db.build.featureSchema.empty() && db.build.featureSchema != schema) {
        return fail("motion database '{}': feature schema {} was recorded, but its config describes "
                    "{}; the features were extracted under a different definition",
                    file.string(), db.build.featureSchema, schema);
    }
    if (db.dimension != db.config.dimension()) {
        return fail("motion database '{}': dimension {} does not match its config's {}", file.string(),
                    db.dimension, db.config.dimension());
    }
    return db;
}

// ---- raw payload ---------------------------------------------------------------------------------

template <typename T>
void appendArray(std::string& out, const std::vector<T>& v) {
    const auto count = static_cast<std::uint64_t>(v.size());
    out.append(reinterpret_cast<const char*>(&count), sizeof(count));
    out.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
}

template <typename T>
bool takeArray(const char*& at, const char* end, std::vector<T>& v) {
    std::uint64_t count = 0;
    if (static_cast<std::size_t>(end - at) < sizeof(count)) {
        return false;
    }
    std::memcpy(&count, at, sizeof(count));
    at += sizeof(count);
    if (count > static_cast<std::uint64_t>(end - at) / sizeof(T)) {
        return false;
    }
    v.resize(static_cast<std::size_t>(count));
    std::memcpy(v.data(), at, v.size() * sizeof(T));
    at += v.size() * sizeof(T);
    return true;
}

struct FilePrefix {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t headerBytes = 0;
};

Result<json> readHeader(std::ifstream& in, const fs::path& file) {
    FilePrefix prefix;
    in.read(reinterpret_cast<char*>(&prefix), sizeof(prefix));
    if (!in || prefix.magic != kMagic) {
        return fail("motion database '{}': not a motion database (wrong magic)", file.string());
    }
    if (prefix.version != kMotionDatabaseFileVersion) {
        return fail("motion database '{}': file version {} cannot be read by this build, which "
                    "knows {}",
                    file.string(), prefix.version, kMotionDatabaseFileVersion);
    }
    if (prefix.headerBytes > (64u << 20)) {
        return fail("motion database '{}': header of {} bytes is implausible", file.string(),
                    prefix.headerBytes);
    }
    std::string text(static_cast<std::size_t>(prefix.headerBytes), '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!in) {
        return fail("motion database '{}': truncated in the header", file.string());
    }
    try {
        return json::parse(text);
    } catch (const json::exception& e) {
        return fail("motion database '{}': header is not JSON: {}", file.string(), e.what());
    }
}

} // namespace

// ---- digests -----------------------------------------------------------------------------------------

std::string motionPackContentDigest(const MotionPack& pack) {
    Digest d;
    d.text(pack.skeletonDigest);
    d.text(skeletonDigest(pack.skeleton));
    d.value(static_cast<std::uint32_t>(pack.clips.size()));
    for (std::size_t c = 0; c < pack.clips.size(); ++c) {
        const PackClip& meta = pack.clips[c];
        d.text(meta.name);
        d.value(meta.loop);
        d.value(meta.sampleRate);
        d.array(meta.heading);
        for (const std::string& tag : meta.tags) {
            d.text(tag);
        }
        d.array(meta.phase.phase);
        d.value(meta.phase.sampleRate);
        d.value(meta.phase.cyclic);
        for (const ContactTrack& track : meta.contacts) {
            d.text(track.joint);
            for (const ContactSpan& span : track.spans) {
                d.value(span.start);
                d.value(span.end);
                d.value(span.clipLength);
            }
        }
        if (meta.provenance < pack.provenance.size()) {
            for (const std::string& step : pack.provenance[meta.provenance].processing) {
                d.text(step);
            }
        }
        if (c < pack.animation.size()) {
            const AnimationClip& clip = pack.animation[c];
            d.value(clip.start);
            d.value(clip.duration);
            for (const AnimationChannel& ch : clip.channels) {
                d.value(ch.joint);
                d.value(ch.path);
                d.value(ch.interpolation);
                d.array(ch.times);
                d.array(ch.values);
            }
        }
    }
    return d.hex();
}

std::string motionFeatureSchemaDigest(const MotionFeatureConfig& config) {
    Digest d;
    digestSchema(d, config);
    return d.hex();
}

std::string motionDatabaseBuildKey(const std::string& sourcePackDigest,
                                   const MotionFeatureConfig& config, float sampleRate,
                                   const std::string& toolVersion) {
    Digest d;
    d.text(sourcePackDigest);
    digestSchema(d, config);
    digestWeights(d, config);
    d.value(sampleRate);
    d.text(toolVersion);
    d.value(MotionDatabase::kVersion);
    return d.hex();
}

std::uint64_t motionDatabaseIdentity(const MotionDatabase& db) {
    Digest d;
    d.value(db.version);
    d.value(db.dimension);
    digestSchema(d, db.config);
    digestWeights(d, db.config);
    d.text(db.skeletonDigest);
    d.array(db.features);
    d.array(db.mean);
    d.array(db.scale);
    d.array(db.sampleClip);
    d.array(db.sampleTime);
    d.array(db.samplePhase);
    d.array(db.sampleTags);
    d.array(db.sampleNext);
    d.array(db.sampleRoot);
    d.array(db.clipTravels);
    for (const std::string& name : db.clipNames) {
        d.text(name);
    }
    // Never 0, so 0 can keep meaning "nobody stamped this".
    return d.result() == 0 ? 1 : d.result();
}

void stampMotionDatabase(MotionDatabase& db, const MotionPack& pack,
                         const MotionDatabaseOptions& options) {
    db.build.sourcePackDigest = motionPackContentDigest(pack);
    db.build.featureSchema = motionFeatureSchemaDigest(db.config);
    db.build.sampleRate = options.sampleRate;
    db.build.toolVersion = options.toolVersion;
    db.build.buildKey = motionDatabaseBuildKey(db.build.sourcePackDigest, db.config,
                                               options.sampleRate, options.toolVersion);
    db.identity = motionDatabaseIdentity(db);
}

// ---- the file ----------------------------------------------------------------------------------------

fs::path motionDatabasePath(const fs::path& packDirectory, const std::string& name) {
    return packDirectory / "databases" / (name + kMotionDatabaseExtension);
}

Result<void> writeMotionDatabase(const MotionDatabase& db, const fs::path& file) {
    // The identity is recomputed rather than trusted. A database stamped at build and then changed
    // -- a weight retuned in place, an array edited -- carries an identity that describes somebody
    // else, and writing it would make the file fail its own load check. Refused here instead, where
    // the cause is still in view.
    const std::uint64_t identity = motionDatabaseIdentity(db);
    if (db.identity != 0 && db.identity != identity) {
        return fail("motion database '{}': changed since it was stamped (identity {:016x}, content "
                    "{:016x}); call stampMotionDatabase before writing it",
                    db.name, db.identity, identity);
    }
    json headerJson = headerToJson(db);
    headerJson["identity"] = fmt::format("{:016x}", identity);
    const std::string header = headerJson.dump(1);
    std::string payload;
    payload.reserve((db.features.size() + db.mean.size() + db.scale.size()) * sizeof(float) +
                    db.sampleCount() * 20u + 64u);
    appendArray(payload, db.features);
    appendArray(payload, db.mean);
    appendArray(payload, db.scale);
    appendArray(payload, db.sampleClip);
    appendArray(payload, db.sampleTime);
    appendArray(payload, db.samplePhase);
    appendArray(payload, db.sampleTags);
    appendArray(payload, db.sampleNext);
    appendArray(payload, db.sampleRoot);
    appendArray(payload, db.clipTravels);

    std::error_code ec;
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path(), ec);
    }
    // §40 on disk: written beside the destination and renamed over it, which is atomic on one
    // filesystem. A reader -- or a background loader racing a rebuild -- sees the old file or the
    // new one.
    fs::path temp = file;
    temp += ".partial";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail("motion database: cannot write '{}'", temp.string());
        }
        const FilePrefix prefix{kMagic, kMotionDatabaseFileVersion,
                                static_cast<std::uint64_t>(header.size())};
        out.write(reinterpret_cast<const char*>(&prefix), sizeof(prefix));
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
        const auto payloadBytes = static_cast<std::uint64_t>(payload.size());
        out.write(reinterpret_cast<const char*>(&payloadBytes), sizeof(payloadBytes));
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        if (!out) {
            return fail("motion database: short write to '{}'", temp.string());
        }
    }
    fs::rename(temp, file, ec);
    if (ec) {
        return fail("motion database: cannot publish '{}': {}", file.string(), ec.message());
    }
    return {};
}

Result<MotionDatabase> readMotionDatabaseHeader(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return fail("motion database '{}': cannot open", file.string());
    }
    auto header = readHeader(in, file);
    if (!header) {
        return std::unexpected(header.error());
    }
    return headerFromJson(*header, file);
}

Result<MotionDatabase> readMotionDatabase(const fs::path& file, const MotionDatabaseExpectations& expect) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return fail("motion database '{}': cannot open", file.string());
    }
    auto header = readHeader(in, file);
    if (!header) {
        return std::unexpected(header.error());
    }
    auto parsed = headerFromJson(*header, file);
    if (!parsed) {
        return parsed;
    }
    MotionDatabase db = std::move(*parsed);

    // The caller's expectations before the payload, so a wrong database is refused without reading
    // a hundred megabytes of it.
    if (!expect.skeletonDigest.empty() && expect.skeletonDigest != db.skeletonDigest) {
        return fail("motion database '{}': built for skeleton {}, but this rig is {}", file.string(),
                    db.skeletonDigest, expect.skeletonDigest);
    }
    if (!expect.sourcePackDigest.empty() && expect.sourcePackDigest != db.build.sourcePackDigest) {
        return fail("motion database '{}': built from pack content {}, but the pack beside it is {}; "
                    "the database is stale and must be rebuilt",
                    file.string(), db.build.sourcePackDigest, expect.sourcePackDigest);
    }
    if (!expect.featureSchema.empty() && expect.featureSchema != db.build.featureSchema) {
        return fail("motion database '{}': feature schema {} where {} was required", file.string(),
                    db.build.featureSchema, expect.featureSchema);
    }

    std::uint64_t payloadBytes = 0;
    in.read(reinterpret_cast<char*>(&payloadBytes), sizeof(payloadBytes));
    if (!in || payloadBytes > (std::uint64_t{1} << 40)) {
        return fail("motion database '{}': truncated before the payload", file.string());
    }
    std::string payload(static_cast<std::size_t>(payloadBytes), '\0');
    in.read(payload.data(), static_cast<std::streamsize>(payload.size()));
    if (!in) {
        return fail("motion database '{}': payload truncated", file.string());
    }
    const char* at = payload.data();
    const char* end = at + payload.size();
    if (!takeArray(at, end, db.features) || !takeArray(at, end, db.mean) ||
        !takeArray(at, end, db.scale) || !takeArray(at, end, db.sampleClip) ||
        !takeArray(at, end, db.sampleTime) || !takeArray(at, end, db.samplePhase) ||
        !takeArray(at, end, db.sampleTags) || !takeArray(at, end, db.sampleNext) ||
        !takeArray(at, end, db.sampleRoot) || !takeArray(at, end, db.clipTravels) || at != end) {
        return fail("motion database '{}': payload is malformed", file.string());
    }

    // Shape: every array the length the header says, every index in range. A search trusts all of
    // these without a check in its inner loop, so they are checked once here.
    const std::size_t n = db.sampleClip.size();
    const std::size_t dim = db.dimension;
    if (n != db.stats.samples || db.features.size() != n * dim || db.mean.size() != dim ||
        db.scale.size() != dim || db.sampleTime.size() != n || db.samplePhase.size() != n ||
        db.sampleTags.size() != n || db.sampleNext.size() != n || db.sampleRoot.size() != 3u * n ||
        db.clipTravels.size() != db.clipNames.size()) {
        return fail("motion database '{}': array lengths disagree with {} samples x {} dimensions",
                    file.string(), db.stats.samples, dim);
    }
    // Corruption first, so a damaged file is reported as damaged rather than as whichever index
    // the damage happened to push out of range.
    const std::uint64_t identity = motionDatabaseIdentity(db);
    if (identity != db.identity) {
        return fail("motion database '{}': content digest {:016x} does not match the recorded "
                    "{:016x}; the file is corrupt or was edited",
                    file.string(), identity, db.identity);
    }
    for (std::size_t s = 0; s < n; ++s) {
        if (db.sampleClip[s] >= db.clipNames.size()) {
            return fail("motion database '{}': sample {} names clip {} of {}", file.string(), s,
                        db.sampleClip[s], db.clipNames.size());
        }
        if (db.sampleNext[s] != MotionDatabase::kInvalid && db.sampleNext[s] >= n) {
            return fail("motion database '{}': sample {} continues to {} of {}", file.string(), s,
                        db.sampleNext[s], n);
        }
    }
    return db;
}

namespace {

template <typename T>
bool sameBits(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

bool sameBits(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

} // namespace

std::string firstMotionDatabaseDifference(const MotionDatabase& a, const MotionDatabase& b) {
    if (a.version != b.version) return "version";
    if (a.name != b.name) return "name";
    if (a.skeletonDigest != b.skeletonDigest) return "skeletonDigest";
    if (!(a.config == b.config)) return "config";
    if (a.dimension != b.dimension) return "dimension";
    if (!sameBits(a.features, b.features)) return "features";
    if (!sameBits(a.sampleClip, b.sampleClip)) return "sampleClip";
    if (!sameBits(a.sampleTime, b.sampleTime)) return "sampleTime";
    if (!sameBits(a.samplePhase, b.samplePhase)) return "samplePhase";
    if (!sameBits(a.sampleTags, b.sampleTags)) return "sampleTags";
    if (!sameBits(a.sampleNext, b.sampleNext)) return "sampleNext";
    if (!sameBits(a.sampleRoot, b.sampleRoot)) return "sampleRoot";
    if (!sameBits(a.clipTravels, b.clipTravels)) return "clipTravels";
    if (!sameBits(a.mean, b.mean)) return "mean";
    if (!sameBits(a.scale, b.scale)) return "scale";
    if (a.clipNames != b.clipNames) return "clipNames";
    const MotionDatabaseStats& s = a.stats;
    const MotionDatabaseStats& t = b.stats;
    if (s.clips != t.clips || s.samples != t.samples || s.dimension != t.dimension) return "stats.counts";
    if (s.featureBytes != t.featureBytes || s.metadataBytes != t.metadataBytes) return "stats.bytes";
    if (s.deadDimensions != t.deadDimensions) return "stats.deadDimensions";
    if (!sameBits(s.costSpread, t.costSpread)) return "stats.costSpread";
    if (!sameBits(s.jointRadiusSpread, t.jointRadiusSpread)) return "stats.jointRadiusSpread";
    if (s.jointNames != t.jointNames) return "stats.jointNames";
    if (!(a.build == b.build)) return "build";
    if (a.identity != b.identity) return "identity";
    return {};
}

Result<CachedBuildResult> buildMotionDatabaseCached(const MotionPack& pack,
                                                    const MotionDatabaseOptions& options,
                                                    const fs::path& file) {
    const std::string source = motionPackContentDigest(pack);
    const std::string key =
        motionDatabaseBuildKey(source, options.config, options.sampleRate, options.toolVersion);
    std::error_code ec;
    if (fs::exists(file, ec)) {
        const auto header = readMotionDatabaseHeader(file);
        if (header && header->build.buildKey == key) {
            // A key hit is still loaded with every check: the key says the inputs match, and the
            // load's digest says the bytes on disk are the ones that build produced.
            auto loaded = readMotionDatabase(file, {pack.skeletonDigest, source, {}});
            if (loaded) {
                return CachedBuildResult{std::move(*loaded), true};
            }
            // A corrupt cache entry is rebuilt rather than reported: the inputs are in hand.
        }
    }
    auto built = buildMotionDatabase(pack, options);
    if (!built) {
        return std::unexpected(built.error());
    }
    if (auto written = writeMotionDatabase(*built, file); !written) {
        return std::unexpected(written.error());
    }
    return CachedBuildResult{std::move(*built), false};
}

} // namespace avgen::scene
