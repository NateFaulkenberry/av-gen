#include "assets/bvh_loader.hpp"

#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

namespace avgen::assets {
namespace {

enum class Channel : std::uint8_t { Xpos, Ypos, Zpos, Xrot, Yrot, Zrot, Unknown };

Channel channelFromName(std::string_view name) {
    if (name == "Xposition") { return Channel::Xpos; }
    if (name == "Yposition") { return Channel::Ypos; }
    if (name == "Zposition") { return Channel::Zpos; }
    if (name == "Xrotation") { return Channel::Xrot; }
    if (name == "Yrotation") { return Channel::Yrot; }
    if (name == "Zrotation") { return Channel::Zrot; }
    return Channel::Unknown;
}

struct JointChannels {
    std::vector<Channel> order; // as declared, which is the order they are applied in
};

// A whitespace tokeniser that keeps its place. BVH is line-oriented in its header and
// whitespace-delimited in its data, and treating both the same way is simpler than tracking lines.
class Tokens {
public:
    explicit Tokens(std::string_view text) : text_(text) {}

    [[nodiscard]] bool next(std::string_view& out) {
        while (at_ < text_.size() && isSpace(text_[at_])) {
            ++at_;
        }
        if (at_ >= text_.size()) {
            return false;
        }
        const std::size_t begin = at_;
        while (at_ < text_.size() && !isSpace(text_[at_])) {
            ++at_;
        }
        out = text_.substr(begin, at_ - begin);
        return true;
    }

    [[nodiscard]] bool nextFloat(float& out) {
        std::string_view token;
        if (!next(token)) {
            return false;
        }
        // `from_chars` for floats is exact and locale-independent, which `atof` is not -- a BVH
        // read on a machine with a comma decimal separator would otherwise truncate every value.
        const char* begin = token.data();
        const char* end = begin + token.size();
        const auto result = std::from_chars(begin, end, out);
        return result.ec == std::errc{} && result.ptr == end;
    }

    // The rest of the current line, trimmed. BVH joint names may contain spaces in the wild.
    [[nodiscard]] std::string restOfLine() {
        const std::size_t begin = at_;
        std::size_t end = text_.find('\n', at_);
        if (end == std::string_view::npos) {
            end = text_.size();
        }
        at_ = end;
        std::string_view line = text_.substr(begin, end - begin);
        while (!line.empty() && isSpace(line.front())) {
            line.remove_prefix(1);
        }
        while (!line.empty() && isSpace(line.back())) {
            line.remove_suffix(1);
        }
        return std::string(line);
    }

private:
    static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    std::string_view text_;
    std::size_t at_ = 0;
};

} // namespace

Result<BvhClip> parseBvh(std::string_view text, const BvhLoadOptions& options) {
    BvhClip out;
    Tokens tokens(text);
    std::string_view token;

    if (!tokens.next(token) || token != "HIERARCHY") {
        return fail("not a BVH file: expected HIERARCHY");
    }

    std::vector<JointChannels> channels;
    std::vector<int> stack;
    std::uint32_t unnamed = 0;

    // ---- the hierarchy --------------------------------------------------------------------------
    while (tokens.next(token)) {
        if (token == "MOTION") {
            break;
        }
        if (token == "ROOT" || token == "JOINT") {
            const std::string name = options.namePrefix + tokens.restOfLine();
            scene::Joint joint;
            joint.name = name.empty() ? fmt::format("{}joint{}", options.namePrefix, unnamed++) : name;
            joint.parent = stack.empty() ? -1 : stack.back();
            out.skeleton.joints.push_back(std::move(joint));
            channels.emplace_back();
            stack.push_back(static_cast<int>(out.skeleton.joints.size()) - 1);
            continue;
        }
        if (token == "End") {
            // `End Site`: an offset and no channels. Kept as a joint, because a foot's end site is
            // where the toe is, and a contact detector has nothing else to watch on a BVH rig.
            (void)tokens.restOfLine();
            scene::Joint joint;
            const std::string parentName =
                stack.empty() ? std::string() : out.skeleton.joints[static_cast<std::size_t>(stack.back())].name;
            joint.name = parentName.empty() ? fmt::format("{}end{}", options.namePrefix, unnamed++)
                                            : parentName + "_End";
            joint.parent = stack.empty() ? -1 : stack.back();
            out.skeleton.joints.push_back(std::move(joint));
            channels.emplace_back();
            stack.push_back(static_cast<int>(out.skeleton.joints.size()) - 1);
            ++out.endSites;
            continue;
        }
        if (token == "{") {
            continue;
        }
        if (token == "}") {
            if (stack.empty()) {
                return fail("BVH hierarchy: a '}' with no matching '{'");
            }
            stack.pop_back();
            continue;
        }
        if (token == "OFFSET") {
            if (stack.empty()) {
                return fail("BVH hierarchy: OFFSET outside any joint");
            }
            glm::vec3 offset(0.0f);
            if (!tokens.nextFloat(offset.x) || !tokens.nextFloat(offset.y) || !tokens.nextFloat(offset.z)) {
                return fail("BVH hierarchy: OFFSET needs three numbers");
            }
            out.skeleton.joints[static_cast<std::size_t>(stack.back())].rest.position = offset * options.scale;
            continue;
        }
        if (token == "CHANNELS") {
            if (stack.empty()) {
                return fail("BVH hierarchy: CHANNELS outside any joint");
            }
            float countf = 0.0f;
            if (!tokens.nextFloat(countf)) {
                return fail("BVH hierarchy: CHANNELS needs a count");
            }
            const auto count = static_cast<int>(countf);
            JointChannels& jc = channels[static_cast<std::size_t>(stack.back())];
            for (int i = 0; i < count; ++i) {
                if (!tokens.next(token)) {
                    return fail("BVH hierarchy: CHANNELS declared {} and the file ended", count);
                }
                const Channel channel = channelFromName(token);
                if (channel == Channel::Unknown) {
                    out.warnings.push_back(
                        fmt::format("joint '{}': unsupported channel '{}'; it is read and discarded",
                                    out.skeleton.joints[static_cast<std::size_t>(stack.back())].name, token));
                }
                jc.order.push_back(channel);
            }
            continue;
        }
        return fail("BVH hierarchy: unexpected token '{}'", token);
    }

    if (out.skeleton.joints.empty()) {
        return fail("BVH hierarchy: no joints");
    }
    if (!stack.empty()) {
        return fail("BVH hierarchy: {} unclosed '{{'", stack.size());
    }

    // ---- MOTION ---------------------------------------------------------------------------------
    if (!tokens.next(token) || token != "Frames:") {
        return fail("BVH motion: expected 'Frames:'");
    }
    float framesf = 0.0f;
    if (!tokens.nextFloat(framesf) || framesf < 0.0f) {
        return fail("BVH motion: 'Frames:' needs a non-negative count");
    }
    out.frames = static_cast<std::uint32_t>(framesf);
    if (!tokens.next(token) || token != "Frame") {
        return fail("BVH motion: expected 'Frame Time:'");
    }
    if (!tokens.next(token) || token != "Time:") {
        return fail("BVH motion: expected 'Frame Time:'");
    }
    if (!tokens.nextFloat(out.frameSeconds) || !(out.frameSeconds > 0.0f)) {
        return fail("BVH motion: 'Frame Time:' must be a positive number of seconds");
    }

    std::size_t total = 0;
    for (const JointChannels& jc : channels) {
        total += jc.order.size();
        // The rotation order this joint declares, as a three-letter word. Collected per file
        // because an assumed order is a different pose, not a rounding error.
        std::string order;
        for (const Channel c : jc.order) {
            if (c == Channel::Xrot) { order += 'X'; }
            if (c == Channel::Yrot) { order += 'Y'; }
            if (c == Channel::Zrot) { order += 'Z'; }
        }
        if (order.size() == 3 &&
            std::find(out.rotationOrders.begin(), out.rotationOrders.end(), order) ==
                out.rotationOrders.end()) {
            out.rotationOrders.push_back(order);
        }
    }
    out.channels = static_cast<std::uint32_t>(total);
    if (total == 0) {
        return fail("BVH motion: the hierarchy declares no channels, so there is nothing to read");
    }

    // One channel per joint per path, built as we go; empty ones are dropped at the end.
    struct Track {
        std::vector<glm::vec3> translation;
        std::vector<glm::quat> rotation;
        bool hasTranslation = false;
        bool hasRotation = false;
    };
    std::vector<Track> tracks(out.skeleton.joints.size());
    std::vector<float> values(total, 0.0f);

    for (std::uint32_t frame = 0; frame < out.frames; ++frame) {
        for (std::size_t i = 0; i < total; ++i) {
            if (!tokens.nextFloat(values[i])) {
                return fail("BVH motion: frame {} of {} ended after {} of {} values", frame + 1,
                            out.frames, i, total);
            }
        }
        std::size_t at = 0;
        for (std::size_t j = 0; j < channels.size(); ++j) {
            const JointChannels& jc = channels[j];
            if (jc.order.empty()) {
                continue;
            }
            Track& track = tracks[j];
            glm::vec3 position = out.skeleton.joints[j].rest.position;
            // **Intrinsic, in the order the file declares.** Composing on the right applies each
            // rotation in the frame left by the ones before it, which is what BVH means. Assuming
            // ZXY, or composing on the left, gives a rig that is right wherever only one axis is
            // non-zero and wrong everywhere else -- the kind of error that looks like bad mocap.
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            for (const Channel channel : jc.order) {
                const float v = values[at++];
                switch (channel) {
                case Channel::Xpos: position.x = v * options.scale; track.hasTranslation = true; break;
                case Channel::Ypos: position.y = v * options.scale; track.hasTranslation = true; break;
                case Channel::Zpos: position.z = v * options.scale; track.hasTranslation = true; break;
                case Channel::Xrot:
                    rotation = rotation * glm::angleAxis(glm::radians(v), glm::vec3(1, 0, 0));
                    track.hasRotation = true;
                    break;
                case Channel::Yrot:
                    rotation = rotation * glm::angleAxis(glm::radians(v), glm::vec3(0, 1, 0));
                    track.hasRotation = true;
                    break;
                case Channel::Zrot:
                    rotation = rotation * glm::angleAxis(glm::radians(v), glm::vec3(0, 0, 1));
                    track.hasRotation = true;
                    break;
                case Channel::Unknown: break;
                }
            }
            track.translation.push_back(position);
            track.rotation.push_back(glm::normalize(rotation));
        }
        if (at != total) {
            return fail("BVH motion: frame {} consumed {} of {} channels", frame + 1, at, total);
        }
    }

    // ---- build the clip ---------------------------------------------------------------------------
    out.clip.name = options.clipName.empty() ? "motion" : options.clipName;
    out.clip.start = 0.0f;
    out.clip.duration = out.frames > 0 ? static_cast<float>(out.frames - 1) * out.frameSeconds : 0.0f;
    for (std::size_t j = 0; j < tracks.size(); ++j) {
        const Track& track = tracks[j];
        if (track.rotation.size() < 2) {
            continue;
        }
        const auto times = [&]() {
            std::vector<float> t;
            t.reserve(track.rotation.size());
            for (std::size_t f = 0; f < track.rotation.size(); ++f) {
                t.push_back(static_cast<float>(f) * out.frameSeconds);
            }
            return t;
        };
        if (track.hasRotation) {
            scene::AnimationChannel channel;
            channel.joint = static_cast<std::uint32_t>(j);
            channel.path = scene::AnimationPath::Rotation;
            channel.interpolation = scene::Interpolation::Linear;
            channel.times = times();
            for (const glm::quat& q : track.rotation) {
                channel.values.emplace_back(q.x, q.y, q.z, q.w);
            }
            out.clip.channels.push_back(std::move(channel));
        }
        if (track.hasTranslation) {
            scene::AnimationChannel channel;
            channel.joint = static_cast<std::uint32_t>(j);
            channel.path = scene::AnimationPath::Translation;
            channel.interpolation = scene::Interpolation::Linear;
            channel.times = times();
            for (const glm::vec3& p : track.translation) {
                channel.values.emplace_back(p.x, p.y, p.z, 0.0f);
            }
            out.clip.channels.push_back(std::move(channel));
        }
    }

    // The skeleton's palette is every joint, because a BVH has no skin to name a subset.
    for (std::uint32_t i = 0; i < out.skeleton.joints.size(); ++i) {
        out.skeleton.palette.push_back(i);
        out.skeleton.inverseBind.emplace_back(1.0f);
    }
    if (!out.skeleton.valid()) {
        return fail("BVH: the parsed skeleton is not valid (a parent does not precede its child)");
    }
    return out;
}

Result<BvhClip> loadBvh(const std::filesystem::path& path, const BvhLoadOptions& options) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail("cannot open BVH '{}'", path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    BvhLoadOptions resolved = options;
    if (resolved.clipName.empty()) {
        resolved.clipName = path.stem().string();
    }
    auto result = parseBvh(text, resolved);
    if (!result) {
        return fail("BVH '{}': {}", path.string(), result.error().message);
    }
    return result;
}

} // namespace avgen::assets
