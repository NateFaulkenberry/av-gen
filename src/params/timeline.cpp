#include "params/timeline.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <utility>

namespace avgen::params {

using nlohmann::json;

namespace {

constexpr double kKeyTimeEpsilon = 1e-6;
constexpr std::size_t kMaxComponents = std::tuple_size_v<KeyValue>;

template <typename E>
struct EnumName {
    E value;
    std::string_view name;
};

constexpr std::array<EnumName<KeyInterp>, 7> kInterpNames{{{KeyInterp::Step, "step"},
                                                           {KeyInterp::Linear, "linear"},
                                                           {KeyInterp::Smooth, "smooth"},
                                                           {KeyInterp::EaseIn, "easeIn"},
                                                           {KeyInterp::EaseOut, "easeOut"},
                                                           {KeyInterp::EaseInOut, "easeInOut"},
                                                           {KeyInterp::Bezier, "bezier"}}};
constexpr std::array<EnumName<TimeBase>, 2> kTimeBaseNames{
    {{TimeBase::Seconds, "seconds"}, {TimeBase::Beats, "beats"}}};
constexpr std::array<EnumName<TrackMode>, 3> kModeNames{
    {{TrackMode::Replace, "replace"}, {TrackMode::Add, "add"}, {TrackMode::Multiply, "multiply"}}};

template <typename E, std::size_t N>
const char* enumToName(const std::array<EnumName<E>, N>& table, E value) {
    for (const auto& entry : table) {
        if (entry.value == value) {
            return entry.name.data();
        }
    }
    return table[0].name.data();
}

template <typename E, std::size_t N>
std::optional<E> enumFromName(const std::array<EnumName<E>, N>& table, std::string_view name) {
    for (const auto& entry : table) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

// ---- interpolation -------------------------------------------------------------------------

double lerp(double a, double b, double u) {
    return a + (b - a) * u;
}

// Cubic Hermite on [0,1] with end values p0/p1 and tangents m0/m1 already scaled to the unit span.
double hermite(double p0, double m0, double p1, double m1, double u) {
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
    const double h10 = u3 - 2.0 * u2 + u;
    const double h01 = -2.0 * u3 + 3.0 * u2;
    const double h11 = u3 - u2;
    return h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
}

// Catmull-Rom slope (value per time unit) at key `i`, one-sided at the ends.
double catmullRomSlope(const std::vector<Key>& keys, std::size_t i, std::size_t c) {
    const std::size_t prev = i == 0 ? i : i - 1;
    const std::size_t next = i + 1 < keys.size() ? i + 1 : i;
    const double dt = keys[next].time - keys[prev].time;
    if (dt <= 0.0) {
        return 0.0;
    }
    return (static_cast<double>(keys[next].value[c]) - static_cast<double>(keys[prev].value[c])) / dt;
}

float interpolateComponent(const std::vector<Key>& keys, std::size_t i, std::size_t c, double u) {
    const Key& left = keys[i];
    const Key& right = keys[i + 1];
    const double v0 = static_cast<double>(left.value[c]);
    const double v1 = static_cast<double>(right.value[c]);
    const double span = right.time - left.time;
    switch (left.interp) {
    case KeyInterp::Step:
        return left.value[c];
    case KeyInterp::Linear:
        return static_cast<float>(lerp(v0, v1, u));
    case KeyInterp::EaseIn:
        return static_cast<float>(lerp(v0, v1, u * u * u));
    case KeyInterp::EaseOut: {
        const double w = 1.0 - u;
        return static_cast<float>(lerp(v0, v1, 1.0 - w * w * w));
    }
    case KeyInterp::EaseInOut:
        return static_cast<float>(lerp(v0, v1, u * u * (3.0 - 2.0 * u)));
    case KeyInterp::Smooth: {
        const double m0 = catmullRomSlope(keys, i, c) * span;
        const double m1 = catmullRomSlope(keys, i + 1, c) * span;
        const double raw = hermite(v0, m0, v1, m1, u);
        return static_cast<float>(std::clamp(raw, std::min(v0, v1), std::max(v0, v1)));
    }
    case KeyInterp::Bezier: {
        const double m0 = static_cast<double>(left.tangentOut[c]) * span;
        const double m1 = static_cast<double>(right.tangentIn[c]) * span;
        return static_cast<float>(hermite(v0, m0, v1, m1, u));
    }
    }
    return left.value[c];
}

float applyTrackMode(TrackMode mode, float current, float value) {
    switch (mode) {
    case TrackMode::Replace:
        return value;
    case TrackMode::Add:
        return current + value;
    case TrackMode::Multiply:
        return current * value;
    }
    return current;
}

bool keyTimesMatch(double a, double b) {
    return std::abs(a - b) <= kKeyTimeEpsilon;
}

// Components written to JSON: the keyed count when bound; otherwise (component < 0 on an unbound
// track) every entry up to the last non-zero one, so nothing is lost across a save without a scene.
std::size_t serialisedComponents(const Track& track) {
    if (track.component >= 0 || track.param != nullptr) {
        return track.keyedComponents();
    }
    std::size_t count = 1;
    for (const Key& key : track.keys) {
        for (std::size_t c = count; c < kMaxComponents; ++c) {
            if (key.value[c] != 0.0f || key.tangentIn[c] != 0.0f || key.tangentOut[c] != 0.0f) {
                count = c + 1;
            }
        }
    }
    return count;
}

json valueToJson(const KeyValue& value, std::size_t count) {
    json array = json::array();
    for (std::size_t c = 0; c < count; ++c) {
        array.push_back(static_cast<double>(value[c]));
    }
    return array;
}

// ---- JSON readers (missing optional keys leave `out` unchanged) ---------------------------------

Result<void> readNumber(const json& j, const char* key, double& out, bool required = false) {
    const auto it = j.find(key);
    if (it == j.end()) {
        if (required) {
            return fail("missing required key '{}'", key);
        }
        return {};
    }
    if (!it->is_number()) {
        return fail("'{}' must be a number", key);
    }
    out = it->get<double>();
    return {};
}

Result<void> readInt(const json& j, const char* key, int& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_number_integer()) {
        return fail("'{}' must be an integer", key);
    }
    out = it->get<int>();
    return {};
}

Result<void> readBool(const json& j, const char* key, bool& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_boolean()) {
        return fail("'{}' must be a boolean", key);
    }
    out = it->get<bool>();
    return {};
}

Result<void> readString(const json& j, const char* key, std::string& out, bool required = false) {
    const auto it = j.find(key);
    if (it == j.end()) {
        if (required) {
            return fail("missing required key '{}'", key);
        }
        return {};
    }
    if (!it->is_string()) {
        return fail("'{}' must be a string", key);
    }
    out = it->get<std::string>();
    return {};
}

template <typename E, std::size_t N>
Result<void> readEnum(const json& j, const char* key, const std::array<EnumName<E>, N>& table, E& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_string()) {
        return fail("'{}' must be a string", key);
    }
    const auto name = it->get<std::string>();
    if (const auto value = enumFromName(table, name)) {
        out = *value;
        return {};
    }
    return fail("unknown value '{}' for '{}'", name, key);
}

// A KeyValue: an array of 1..4 numbers (shorter arrays are zero-padded) or a bare number.
Result<void> readKeyValue(const json& j, const char* key, KeyValue& out, bool required) {
    const auto it = j.find(key);
    if (it == j.end()) {
        if (required) {
            return fail("missing required key '{}'", key);
        }
        return {};
    }
    out = KeyValue{};
    if (it->is_number()) {
        out[0] = it->get<float>();
        return {};
    }
    if (!it->is_array()) {
        return fail("'{}' must be an array of numbers", key);
    }
    if (it->empty() || it->size() > kMaxComponents) {
        return fail("'{}' must have 1 to {} entries", key, kMaxComponents);
    }
    std::size_t c = 0;
    for (const auto& element : *it) {
        if (!element.is_number()) {
            return fail("'{}' must be an array of numbers", key);
        }
        out[c++] = element.get<float>();
    }
    return {};
}

Result<Key> keyFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("key must be a JSON object");
    }
    Key key;
    if (auto r = readNumber(j, "time", key.time, true); !r) {
        return fail(std::move(r.error().message));
    }
    if (auto r = readKeyValue(j, "value", key.value, true); !r) {
        return fail(std::move(r.error().message));
    }
    if (auto r = readEnum(j, "interp", kInterpNames, key.interp); !r) {
        return fail(std::move(r.error().message));
    }
    if (auto r = readKeyValue(j, "tangentIn", key.tangentIn, false); !r) {
        return fail(std::move(r.error().message));
    }
    if (auto r = readKeyValue(j, "tangentOut", key.tangentOut, false); !r) {
        return fail(std::move(r.error().message));
    }
    return key;
}

Result<Track> trackFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("track must be a JSON object");
    }
    Track track;
    if (auto r = readString(j, "target", track.target, true); !r) {
        return fail(std::move(r.error().message));
    }
    std::array<Result<void>, 5> results{
        readInt(j, "component", track.component),    readEnum(j, "timeBase", kTimeBaseNames, track.timeBase),
        readEnum(j, "mode", kModeNames, track.mode), readNumber(j, "loopLength", track.loopLength),
        readBool(j, "enabled", track.enabled),
    };
    // Absent in every project written before tracks carried ownership, and absent for every
    // hand-authored track since: "" is the right answer in both cases.
    if (auto r = readString(j, "source", track.source); !r) {
        return fail(std::move(r.error().message));
    }
    for (auto& r : results) {
        if (!r) {
            return fail(std::move(r.error().message));
        }
    }
    if (track.component < -1) {
        return fail("'component' must be -1 or a component index");
    }
    if (!(track.loopLength >= 0.0)) {
        return fail("'loopLength' must be >= 0");
    }
    if (const auto keys = j.find("keys"); keys != j.end()) {
        if (!keys->is_array()) {
            return fail("'keys' must be an array");
        }
        track.keys.reserve(keys->size());
        for (const auto& entry : *keys) {
            auto key = keyFromJson(entry);
            if (!key) {
                return fail("keys[{}]: {}", track.keys.size(), key.error().message);
            }
            track.keys.push_back(*key);
        }
        track.sortKeys();
    }
    return track;
}

Result<Cue> cueFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("cue must be a JSON object");
    }
    Cue cue;
    std::array<Result<void>, 5> results{
        readNumber(j, "time", cue.time, true),
        readString(j, "name", cue.name),
        readString(j, "preset", cue.preset),
        readNumber(j, "morphSeconds", cue.morphSeconds),
        readEnum(j, "timeBase", kTimeBaseNames, cue.timeBase),
    };
    for (auto& r : results) {
        if (!r) {
            return fail(std::move(r.error().message));
        }
    }
    if (!(cue.morphSeconds >= 0.0)) {
        return fail("'morphSeconds' must be >= 0");
    }
    return cue;
}

} // namespace

// ---- names ------------------------------------------------------------------------------------

const char* keyInterpName(KeyInterp interp) {
    return enumToName(kInterpNames, interp);
}

std::optional<KeyInterp> keyInterpFromName(std::string_view name) {
    return enumFromName(kInterpNames, name);
}

const char* timeBaseName(TimeBase base) {
    return enumToName(kTimeBaseNames, base);
}

std::optional<TimeBase> timeBaseFromName(std::string_view name) {
    return enumFromName(kTimeBaseNames, name);
}

const char* trackModeName(TrackMode mode) {
    return enumToName(kModeNames, mode);
}

std::optional<TrackMode> trackModeFromName(std::string_view name) {
    return enumFromName(kModeNames, name);
}

// ---- Track -------------------------------------------------------------------------------------

std::size_t Track::addKey(Key key) {
    const auto pos = std::upper_bound(keys.begin(), keys.end(), key.time,
                                      [](double time, const Key& k) { return time < k.time; });
    // The key before the insertion point is the closest one at or before `time`; the one at the
    // insertion point is the closest after it. Either may be within the replace epsilon. The
    // existing key's time is kept so repeated records at "the same" time never drift.
    auto replace = [&](auto it) {
        key.time = it->time;
        *it = key;
        return static_cast<std::size_t>(it - keys.begin());
    };
    if (pos != keys.begin() && keyTimesMatch((pos - 1)->time, key.time)) {
        return replace(pos - 1);
    }
    if (pos != keys.end() && keyTimesMatch(pos->time, key.time)) {
        return replace(pos);
    }
    return static_cast<std::size_t>(keys.insert(pos, key) - keys.begin());
}

void Track::sortKeys() {
    std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
}

std::size_t Track::keyedComponents() const {
    if (component >= 0 || param == nullptr) {
        return 1;
    }
    return std::min(param->componentCount(), kMaxComponents);
}

double Track::localTime(double time) const {
    if (loopLength <= 0.0) {
        return time;
    }
    double local = std::fmod(time, loopLength);
    if (local < 0.0) {
        local += loopLength;
    }
    if (local >= loopLength) { // -epsilon + loopLength can round up to loopLength
        local = 0.0;
    }
    return local;
}

double Track::firstKeyTime() const {
    return keys.empty() ? 0.0 : keys.front().time;
}

double Track::lastKeyTime() const {
    return keys.empty() ? 0.0 : keys.back().time;
}

KeyValue Track::evaluate(double time) const {
    if (keys.empty()) {
        return KeyValue{};
    }
    const double t = localTime(time);
    if (t <= keys.front().time) {
        return keys.front().value;
    }
    if (t >= keys.back().time) {
        return keys.back().value;
    }
    // Last key with key.time <= t starts the span; its interp shapes the whole span. Because
    // keys.front().time < t < keys.back().time the span is strictly inside the vector and has a
    // positive duration (keys with equal times collapse to a step at that instant).
    const auto next = std::upper_bound(keys.begin(), keys.end(), t,
                                       [](double time_, const Key& k) { return time_ < k.time; });
    const auto i = static_cast<std::size_t>(next - 1 - keys.begin());
    const double span = keys[i + 1].time - keys[i].time;
    const double u = span > 0.0 ? std::clamp((t - keys[i].time) / span, 0.0, 1.0) : 1.0;
    KeyValue out{};
    for (std::size_t c = 0; c < kMaxComponents; ++c) {
        out[c] = interpolateComponent(keys, i, c, u);
    }
    return out;
}

// ---- Timeline: tracks ---------------------------------------------------------------------------

// The returned reference is only valid until the next addTrack()/removeTrack(): tracks live in a
// vector that may reallocate.
Track& Timeline::addTrack(Track track) {
    if (findTrack(track.target, track.component) != nullptr) {
        log::warn("timeline: a track for '{}' (component {}) already exists; adding another", track.target,
                  track.component);
    }
    tracks_.push_back(std::move(track));
    return tracks_.back();
}

bool Timeline::removeTrack(std::size_t index) {
    if (index >= tracks_.size()) {
        return false;
    }
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

Track* Timeline::findTrack(const std::string& target, int component) {
    for (Track& track : tracks_) {
        if (track.target == target && track.component == component) {
            return &track;
        }
    }
    return nullptr;
}

const Track* Timeline::findTrack(const std::string& target, int component) const {
    for (const Track& track : tracks_) {
        if (track.target == target && track.component == component) {
            return &track;
        }
    }
    return nullptr;
}

Track* Timeline::recordKey(ParameterSet& params, const std::string& target, int component, double time,
                           KeyInterp interp, TimeBase base) {
    IParameter* param = params.find(target);
    if (param == nullptr) {
        return nullptr;
    }
    if (component >= 0 && static_cast<std::size_t>(component) >= param->componentCount()) {
        log::warn("timeline: component {} out of range for '{}' ({} components)", component, target,
                  param->componentCount());
        return nullptr;
    }
    Track* track = findTrack(target, component);
    if (track == nullptr) {
        Track fresh;
        fresh.target = target;
        fresh.component = component;
        fresh.timeBase = base;
        fresh.param = param;
        track = &addTrack(std::move(fresh));
    } else if (track->param == nullptr) {
        track->param = param;
    }
    Key key;
    key.time = time;
    key.interp = interp;
    if (component >= 0) {
        key.value[0] = param->baseComponent(static_cast<std::size_t>(component));
    } else {
        const std::size_t count = std::min(param->componentCount(), kMaxComponents);
        for (std::size_t c = 0; c < count; ++c) {
            key.value[c] = param->baseComponent(c);
        }
    }
    track->addKey(key);
    return track;
}

bool Timeline::isAutomated(const std::string& path, int component) const {
    for (const Track& track : tracks_) {
        if (!track.enabled || track.param == nullptr || track.target != path) {
            continue;
        }
        if (component < 0 || track.component < 0 || track.component == component) {
            return true;
        }
    }
    return false;
}

// ---- Timeline: cues -----------------------------------------------------------------------------

// The returned reference is only valid until the next addCue()/removeCue().
Cue& Timeline::addCue(Cue cue) {
    const auto pos = std::upper_bound(cues_.begin(), cues_.end(), cue.time,
                                      [](double time, const Cue& c) { return time < c.time; });
    return *cues_.insert(pos, std::move(cue));
}

bool Timeline::removeCue(std::size_t index) {
    if (index >= cues_.size()) {
        return false;
    }
    cues_.erase(cues_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void Timeline::sortCues() {
    std::stable_sort(cues_.begin(), cues_.end(), [](const Cue& a, const Cue& b) { return a.time < b.time; });
}

// The latest (highest index, cues being sorted by time) cue whose time has been reached on its
// own base. Morph progress is measured on that same base: elapsed = clock.at(base) - cue.time, so
// for a beat-based cue `morphSeconds` is read as a length in beats (a morph then follows tempo
// changes like the cue itself does).
Timeline::CueState Timeline::cueAt(const TimelineClock& clock) const {
    CueState state;
    for (std::size_t i = cues_.size(); i-- > 0;) {
        const Cue& cue = cues_[i];
        const double now = clock.at(cue.timeBase);
        if (cue.time > now) {
            continue;
        }
        state.index = static_cast<int>(i);
        if (cue.morphSeconds > 0.0) {
            state.progress = static_cast<float>(std::clamp((now - cue.time) / cue.morphSeconds, 0.0, 1.0));
        } else {
            state.progress = 1.0f;
        }
        break;
    }
    return state;
}

// ---- Timeline: evaluation -----------------------------------------------------------------------

Result<void> Timeline::bind(ParameterSet& params) {
    std::vector<std::string> unknown;
    for (Track& track : tracks_) {
        track.param = params.find(track.target);
        if (track.param == nullptr &&
            std::find(unknown.begin(), unknown.end(), track.target) == unknown.end()) {
            unknown.push_back(track.target);
        }
    }
    unbound_ = unknown;
    if (unknown.empty()) {
        return {};
    }
    std::string names;
    for (const std::string& name : unknown) {
        if (!names.empty()) {
            names += ", ";
        }
        names += name;
    }
    log::warn("timeline: unknown targets: {}", names);
    return fail("timeline: unknown targets: {}", names);
}

void Timeline::unbind() {
    for (Track& track : tracks_) {
        track.param = nullptr;
    }
    unbound_.clear();
}

void Timeline::apply(const TimelineClock& clock) const {
    if (!enabled) {
        return;
    }
    for (const Track& track : tracks_) {
        if (!track.enabled || track.param == nullptr) {
            continue;
        }
        IParameter& param = *track.param;
        const KeyValue value = track.evaluate(clock.at(track.timeBase));
        const std::size_t count = param.componentCount();
        if (track.component >= 0) {
            const auto c = static_cast<std::size_t>(track.component);
            if (c < count) {
                param.setFinalComponent(c, applyTrackMode(track.mode, param.finalComponent(c), value[0]));
            }
            continue;
        }
        const std::size_t n = std::min(count, kMaxComponents);
        for (std::size_t c = 0; c < n; ++c) {
            param.setFinalComponent(c, applyTrackMode(track.mode, param.finalComponent(c), value[c]));
        }
    }
}

double Timeline::durationSeconds() const {
    double duration = 0.0;
    for (const Track& track : tracks_) {
        if (track.timeBase == TimeBase::Seconds && !track.keys.empty()) {
            duration = std::max(duration, track.lastKeyTime());
        }
    }
    for (const Cue& cue : cues_) {
        if (cue.timeBase == TimeBase::Seconds) {
            duration = std::max(duration, cue.time);
        }
    }
    return duration;
}

void Timeline::clear() {
    tracks_.clear();
    cues_.clear();
}

// ---- Timeline: JSON -----------------------------------------------------------------------------

json Timeline::toJson() const {
    json tracks = json::array();
    for (const Track& track : tracks_) {
        const std::size_t count = serialisedComponents(track);
        json keys = json::array();
        for (std::size_t i = 0; i < track.keys.size(); ++i) {
            const Key& key = track.keys[i];
            json k;
            k["time"] = key.time;
            k["value"] = valueToJson(key.value, count);
            k["interp"] = keyInterpName(key.interp);
            // A Bezier span reads tangentOut of its first key and tangentIn of its second, so
            // tangents are written for Bezier keys and for the key following one.
            if (key.interp == KeyInterp::Bezier || (i > 0 && track.keys[i - 1].interp == KeyInterp::Bezier)) {
                k["tangentIn"] = valueToJson(key.tangentIn, count);
                k["tangentOut"] = valueToJson(key.tangentOut, count);
            }
            keys.push_back(std::move(k));
        }
        json t;
        t["target"] = track.target;
        t["component"] = track.component;
        t["timeBase"] = timeBaseName(track.timeBase);
        t["mode"] = trackModeName(track.mode);
        t["loopLength"] = track.loopLength;
        t["enabled"] = track.enabled;
        if (!track.source.empty()) {
            t["source"] = track.source; // omitted for hand-authored tracks, which are most of them
        }
        t["keys"] = std::move(keys);
        tracks.push_back(std::move(t));
    }
    json cues = json::array();
    for (const Cue& cue : cues_) {
        json c;
        c["time"] = cue.time;
        c["name"] = cue.name;
        c["preset"] = cue.preset;
        c["morphSeconds"] = cue.morphSeconds;
        c["timeBase"] = timeBaseName(cue.timeBase);
        cues.push_back(std::move(c));
    }
    return json{{"enabled", enabled}, {"tracks", std::move(tracks)}, {"cues", std::move(cues)}};
}

Result<void> Timeline::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("timeline must be a JSON object");
    }
    bool parsedEnabled = true;
    if (auto r = readBool(j, "enabled", parsedEnabled); !r) {
        return fail("timeline: {}", r.error().message);
    }
    std::vector<Track> tracks;
    if (const auto it = j.find("tracks"); it != j.end()) {
        if (!it->is_array()) {
            return fail("timeline: 'tracks' must be an array");
        }
        tracks.reserve(it->size());
        for (const auto& entry : *it) {
            auto track = trackFromJson(entry);
            if (!track) {
                return fail("timeline: tracks[{}]: {}", tracks.size(), track.error().message);
            }
            tracks.push_back(std::move(*track));
        }
    }
    std::vector<Cue> cues;
    if (const auto it = j.find("cues"); it != j.end()) {
        if (!it->is_array()) {
            return fail("timeline: 'cues' must be an array");
        }
        cues.reserve(it->size());
        for (const auto& entry : *it) {
            auto cue = cueFromJson(entry);
            if (!cue) {
                return fail("timeline: cues[{}]: {}", cues.size(), cue.error().message);
            }
            cues.push_back(std::move(*cue));
        }
    }
    std::stable_sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.time < b.time; });
    enabled = parsedEnabled;
    tracks_ = std::move(tracks);
    cues_ = std::move(cues);
    return {};
}

} // namespace avgen::params
