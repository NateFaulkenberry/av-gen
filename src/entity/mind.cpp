#include "entity/mind.hpp"

#include "entity/entity.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::entity {

// ---- identity ----------------------------------------------------------------------------------

namespace {
constexpr int kKindShift = 56;
constexpr std::uint64_t kIndexMask = (std::uint64_t{1} << kKindShift) - 1;
} // namespace

SubjectId bodySubject(std::size_t entityIndex) {
    return (std::uint64_t{1} << kKindShift) | (static_cast<std::uint64_t>(entityIndex) & kIndexMask);
}
SubjectId pointSubject(std::size_t interestIndex) {
    return (std::uint64_t{2} << kKindShift) | (static_cast<std::uint64_t>(interestIndex) & kIndexMask);
}
SubjectId eventSubject(std::uint64_t sequence) {
    return (std::uint64_t{3} << kKindShift) | (sequence & kIndexMask);
}
SubjectId subjectOf(const Percept& p) {
    return p.kind == InterestKind::Character ? bodySubject(p.source) : pointSubject(p.source);
}
SubjectKind subjectKind(SubjectId id) {
    switch (id >> kKindShift) {
    case 1: return SubjectKind::Body;
    case 2: return SubjectKind::Point;
    case 3: return SubjectKind::Event;
    default: return SubjectKind::None;
    }
}
std::uint64_t subjectIndex(SubjectId id) { return id & kIndexMask; }

// ---- semantic tags -----------------------------------------------------------------------------

std::uint64_t SemanticTags::intern(std::string_view name) {
    if (name.empty()) {
        return 0;
    }
    if (const std::uint64_t b = bit(name); b != 0) {
        return b;
    }
    if (names_.size() >= kCapacity) {
        overflowed_ = true;
        return 0;
    }
    names_.emplace_back(name);
    return std::uint64_t{1} << (names_.size() - 1);
}

std::uint64_t SemanticTags::bit(std::string_view name) const {
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) {
            return std::uint64_t{1} << i;
        }
    }
    return 0;
}

std::uint64_t SemanticTags::mask(std::span<const std::string> names) const {
    std::uint64_t m = 0;
    for (const std::string& n : names) {
        m |= bit(n);
    }
    return m;
}

std::string_view SemanticTags::first(std::uint64_t mask) const {
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if ((mask >> i) & 1u) {
            return names_[i];
        }
    }
    return {};
}

std::string SemanticTags::describe(std::uint64_t mask) const {
    std::string out;
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if ((mask >> i) & 1u) {
            if (!out.empty()) {
                out += ",";
            }
            out += names_[i];
        }
    }
    return out;
}

// ---- personality -------------------------------------------------------------------------------

namespace {
constexpr std::string_view kTraitNames[Personality::kCount] = {
    "curiosity",     "sociability",     "caution",       "aggression",       "eventSensitivity",
    "attentionSpan", "wanderFrequency", "movementSpeed", "preferredDistance"};
} // namespace

float Personality::get(std::size_t i) const {
    switch (i) {
    case 0: return curiosity;
    case 1: return sociability;
    case 2: return caution;
    case 3: return aggression;
    case 4: return eventSensitivity;
    case 5: return attentionSpan;
    case 6: return wanderFrequency;
    case 7: return movementSpeed;
    case 8: return preferredDistance;
    default: return 0.5f;
    }
}

void Personality::set(std::size_t i, float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    switch (i) {
    case 0: curiosity = value; break;
    case 1: sociability = value; break;
    case 2: caution = value; break;
    case 3: aggression = value; break;
    case 4: eventSensitivity = value; break;
    case 5: attentionSpan = value; break;
    case 6: wanderFrequency = value; break;
    case 7: movementSpeed = value; break;
    case 8: preferredDistance = value; break;
    default: break;
    }
}

std::span<const std::string_view> personalityTraitNames() { return kTraitNames; }

std::optional<std::size_t> personalityTrait(std::string_view name) {
    for (std::size_t i = 0; i < Personality::kCount; ++i) {
        if (kTraitNames[i] == name) {
            return i;
        }
    }
    return std::nullopt;
}

Result<Personality> personalityFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'personality' must be an object keyed by trait");
    }
    Personality p;
    for (const auto& [key, value] : j.items()) {
        const auto trait = personalityTrait(key);
        if (!trait) {
            std::string known;
            for (const std::string_view n : kTraitNames) {
                known += known.empty() ? "" : ", ";
                known += n;
            }
            return fail("personality: unknown trait '{}' (known: {})", key, known);
        }
        if (!value.is_number()) {
            return fail("personality: trait '{}' must be a number", key);
        }
        const float v = value.get<float>();
        if (v < 0.0f || v > 1.0f) {
            return fail("personality: trait '{}' ({}) must be between 0 and 1", key, v);
        }
        p.set(*trait, v);
    }
    return p;
}

nlohmann::json personalityToJson(const Personality& p) {
    // Every trait, always: a personality block is present only when authored (the key's presence
    // is the opt-in, as `perception`'s is), and a block that dropped the neutral traits would read
    // back identically but would not say what the author saw in the inspector.
    nlohmann::json j = nlohmann::json::object();
    for (std::size_t i = 0; i < Personality::kCount; ++i) {
        j[std::string(kTraitNames[i])] = p.get(i);
    }
    return j;
}

const Personality& neutralPersonality() {
    static const Personality neutral{};
    return neutral;
}

TraitWeights traitWeightsFromJson(const nlohmann::json* settings, const char* key) {
    TraitWeights w;
    if (settings == nullptr || !settings->is_object() || !settings->contains(key) ||
        !(*settings)[key].is_object()) {
        return w;
    }
    for (const auto& [name, value] : (*settings)[key].items()) {
        const auto trait = personalityTrait(name);
        if (!trait || !value.is_number()) {
            continue;
        }
        w.exponent[*trait] = value.get<float>();
        w.any = w.any || w.exponent[*trait] != 0.0f;
    }
    return w;
}

float traitFactor(float trait, float exponent) {
    if (exponent == 0.0f) {
        return 1.0f;
    }
    return std::pow(0.5f + std::clamp(trait, 0.0f, 1.0f), exponent);
}

float traitFactor(const Personality& p, const TraitWeights& w) {
    if (!w.any) {
        return 1.0f;
    }
    float f = 1.0f;
    for (std::size_t i = 0; i < Personality::kCount; ++i) {
        f *= traitFactor(p.get(i), w.exponent[i]);
    }
    return f;
}

// ---- memory ------------------------------------------------------------------------------------

void ObjectMemory::setSettings(const MemorySettings& settings) {
    settings_ = settings;
    // Sized once, so a noticing tick never grows the vector (§46).
    entries_.reserve(settings_.capacity);
    events_.reserve(settings_.eventCapacity);
}

MemoryEntry* ObjectMemory::findMutable(SubjectId id) {
    for (MemoryEntry& e : entries_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

const MemoryEntry* ObjectMemory::find(SubjectId id) const {
    for (const MemoryEntry& e : entries_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

void ObjectMemory::noticed(SubjectId id, const glm::vec3& position, double time) {
    if (id == kNoSubject || settings_.capacity == 0) {
        return;
    }
    if (MemoryEntry* e = findMutable(id)) {
        e->position = position;
        e->lastSeen = time;
        return;
    }
    MemoryEntry fresh;
    fresh.id = id;
    fresh.position = position;
    fresh.firstNoticed = time;
    fresh.lastSeen = time;
    if (entries_.size() < settings_.capacity) {
        entries_.push_back(fresh);
        return;
    }
    // Full: forget the thing seen longest ago. **Never one with a live consequence** -- a failed
    // target still inside its suppression window, or one investigated inside its recovery window,
    // is exactly the fact the bound exists to keep, and evicting it would re-arm the loop §21 is
    // there to stop. Such entries are preferred last; ties on the lower id so the choice is free of
    // the order entries happened to arrive in.
    std::size_t victim = entries_.size();
    bool victimHot = true;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const MemoryEntry& e = entries_[i];
        const bool hot = (time - e.failedAt) < settings_.failSeconds ||
                         (time - e.investigatedAt) < settings_.recoverSeconds;
        if (victim == entries_.size()) {
            victim = i;
            victimHot = hot;
            continue;
        }
        const MemoryEntry& v = entries_[victim];
        const bool better = (victimHot && !hot) ||
                            (victimHot == hot && (e.lastSeen < v.lastSeen ||
                                                  (e.lastSeen == v.lastSeen && e.id < v.id)));
        if (better) {
            victim = i;
            victimHot = hot;
        }
    }
    entries_[victim] = fresh;
}

float ObjectMemory::attendedAt(const MemoryEntry& e, double time) const {
    if (!(e.attended > 0.0f)) {
        return 0.0f;
    }
    if (!(settings_.recoverSeconds > 0.0f)) {
        return e.attended;
    }
    const double idle = std::max(0.0, time - e.attendedAt);
    return e.attended * static_cast<float>(std::exp(-idle / settings_.recoverSeconds));
}

void ObjectMemory::attend(SubjectId id, float seconds, double time) {
    if (MemoryEntry* e = findMutable(id)) {
        e->attended = attendedAt(*e, time) + std::max(0.0f, seconds);
        e->attendedAt = time;
    }
}

void ObjectMemory::investigated(SubjectId id, double time) {
    if (MemoryEntry* e = findMutable(id)) {
        e->investigatedAt = time;
        e->attended = 0.0f;
        e->attendedAt = time;
        ++e->investigations;
    }
}

void ObjectMemory::failed(SubjectId id, double time) {
    if (MemoryEntry* e = findMutable(id)) {
        e->failedAt = time;
        ++e->failures;
    }
}

float ObjectMemory::novelty(SubjectId id, double time, float spanScale) const {
    const MemoryEntry* e = find(id);
    if (e == nullptr) {
        return 1.0f;
    }
    float base = 1.0f;
    if (std::isfinite(e->investigatedAt) && settings_.recoverSeconds > 0.0f) {
        // Quadratic: nearly nothing for the first third of the window, then a return. A linear
        // ramp left a mushroom investigated 34 s ago at 0.23 of new -- enough, measured on the
        // autonomy demo, for the scout to walk straight back to it when nothing else was near,
        // which is the "walk to mushroom, leave, walk back" §21 names.
        const double r =
            std::clamp((time - e->investigatedAt) / static_cast<double>(settings_.recoverSeconds),
                       0.0, 1.0);
        base = static_cast<float>(r * r);
    } else if (std::isfinite(e->investigatedAt)) {
        base = 1.0f;
    }
    const float span = std::max(settings_.habituationSeconds * std::max(spanScale, 0.05f), 1e-3f);
    return base * std::exp(-attendedAt(*e, time) / span);
}

bool ObjectMemory::suppressed(SubjectId id, double time) const {
    const MemoryEntry* e = find(id);
    return e != nullptr && std::isfinite(e->failedAt) &&
           (time - e->failedAt) < static_cast<double>(settings_.failSeconds);
}

void ObjectMemory::hear(const PerceivedEvent& event) {
    if (event.sequence <= lastEvent_ || settings_.eventCapacity == 0) {
        return;
    }
    lastEvent_ = event.sequence;
    if (events_.size() >= settings_.eventCapacity) {
        events_.erase(events_.begin()); // the oldest; events arrive in sequence order
    }
    events_.push_back(event);
}

void ObjectMemory::forgetEventsBefore(double time) {
    std::erase_if(events_, [&](const PerceivedEvent& e) { return e.time < time; });
}

void ObjectMemory::reset() {
    entries_.clear();
    events_.clear();
    lastEvent_ = 0;
}

// ---- naming ------------------------------------------------------------------------------------

std::string describeSubject(const EntityWorld& world, SubjectId id) {
    const std::uint64_t index = subjectIndex(id);
    const auto at = [](const glm::vec3& p) {
        return fmt::format("{},{}", static_cast<int>(std::lround(p.x)),
                           static_cast<int>(std::lround(p.z)));
    };
    switch (subjectKind(id)) {
    case SubjectKind::Body:
        if (index < world.entities().size()) {
            return world.entities()[index]->name();
        }
        break;
    case SubjectKind::Point: {
        const std::span<const InterestPoint> points = world.interestPoints();
        if (index < points.size()) {
            const InterestPoint& p = points[index];
            return p.name.empty() ? fmt::format("{}@{}", interestKindName(p.kind), at(p.position))
                                  : p.name;
        }
        break;
    }
    case SubjectKind::Event:
        for (const WorldEvent& e : world.worldEvents()) {
            if (e.sequence == index) {
                return fmt::format("{}@{}", world.eventName(e.type), at(e.position));
            }
        }
        return fmt::format("event#{}", index);
    case SubjectKind::None:
        break;
    }
    return {};
}

std::string formatFactors(std::span<const ScoreFactor> factors) {
    std::string out;
    for (const ScoreFactor& f : factors) {
        if (f.name.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += ", ";
        }
        out += fmt::format("{} {:+.2f}", f.name, f.value);
    }
    return out;
}

// ---- attention ---------------------------------------------------------------------------------

namespace {

float readF(const nlohmann::json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

} // namespace

Result<AttentionModelSettings> attentionModelFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'attention' must be an object");
    }
    AttentionModelSettings s;
    if (j.contains("weights")) {
        const nlohmann::json& w = j["weights"];
        if (!w.is_object()) {
            return fail("attention: 'weights' must be an object");
        }
        s.weights.salience = readF(w, "salience", s.weights.salience);
        s.weights.novelty = readF(w, "novelty", s.weights.novelty);
        s.weights.semantic = readF(w, "semantic", s.weights.semantic);
        s.weights.movement = readF(w, "movement", s.weights.movement);
        s.weights.sound = readF(w, "sound", s.weights.sound);
        s.weights.relevance = readF(w, "relevance", s.weights.relevance);
    }
    if (j.contains("tags")) {
        if (!j["tags"].is_object()) {
            return fail("attention: 'tags' must be an object of tag -> weight");
        }
        for (const auto& [tag, value] : j["tags"].items()) {
            if (!value.is_number()) {
                return fail("attention: tag weight '{}' must be a number", tag);
            }
            s.tagWeights.emplace_back(tag, value.get<float>());
        }
    }
    s.hysteresis.switchMargin = readF(j, "switchMargin", s.hysteresis.switchMargin);
    s.hysteresis.minDwellSeconds = readF(j, "minDwellSeconds", s.hysteresis.minDwellSeconds);
    s.hysteresis.maxHoldSeconds = readF(j, "maxHoldSeconds", s.hysteresis.maxHoldSeconds);
    s.hysteresis.refractorySeconds = readF(j, "refractorySeconds", s.hysteresis.refractorySeconds);
    s.hysteresis.acquireThreshold = readF(j, "acquireThreshold", s.hysteresis.acquireThreshold);
    s.hysteresis.releaseThreshold = readF(j, "releaseThreshold", s.hysteresis.releaseThreshold);
    if (j.contains("glance") && j["glance"].is_boolean()) {
        s.glance = j["glance"].get<bool>();
    }
    return s;
}

nlohmann::json attentionModelToJson(const AttentionModelSettings& s) {
    nlohmann::json j = nlohmann::json::object();
    j["weights"] = {{"salience", s.weights.salience}, {"novelty", s.weights.novelty},
                    {"semantic", s.weights.semantic}, {"movement", s.weights.movement},
                    {"sound", s.weights.sound},       {"relevance", s.weights.relevance}};
    if (!s.tagWeights.empty()) {
        nlohmann::json tags = nlohmann::json::object();
        for (const auto& [tag, w] : s.tagWeights) {
            tags[tag] = w;
        }
        j["tags"] = std::move(tags);
    }
    j["switchMargin"] = s.hysteresis.switchMargin;
    j["minDwellSeconds"] = s.hysteresis.minDwellSeconds;
    j["maxHoldSeconds"] = s.hysteresis.maxHoldSeconds;
    j["refractorySeconds"] = s.hysteresis.refractorySeconds;
    j["acquireThreshold"] = s.hysteresis.acquireThreshold;
    j["releaseThreshold"] = s.hysteresis.releaseThreshold;
    j["glance"] = s.glance;
    return j;
}

Result<MemorySettings> memorySettingsFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'memory' must be an object");
    }
    MemorySettings s;
    const float capacity = readF(j, "capacity", static_cast<float>(s.capacity));
    if (capacity < 0.0f || capacity > 256.0f) {
        return fail("memory: 'capacity' ({}) must be between 0 and 256", capacity);
    }
    s.capacity = static_cast<std::uint16_t>(capacity);
    s.recoverSeconds = readF(j, "recoverSeconds", s.recoverSeconds);
    s.habituationSeconds = readF(j, "habituationSeconds", s.habituationSeconds);
    s.failSeconds = readF(j, "failSeconds", s.failSeconds);
    const float events = readF(j, "eventCapacity", static_cast<float>(s.eventCapacity));
    if (events < 0.0f || events > 64.0f) {
        return fail("memory: 'eventCapacity' ({}) must be between 0 and 64", events);
    }
    s.eventCapacity = static_cast<std::uint16_t>(events);
    s.eventSeconds = readF(j, "eventSeconds", s.eventSeconds);
    return s;
}

nlohmann::json memorySettingsToJson(const MemorySettings& s) {
    return nlohmann::json{{"capacity", s.capacity},
                          {"recoverSeconds", s.recoverSeconds},
                          {"habituationSeconds", s.habituationSeconds},
                          {"failSeconds", s.failSeconds},
                          {"eventCapacity", s.eventCapacity},
                          {"eventSeconds", s.eventSeconds}};
}

void AttentionModel::reset() {
    state_.reset();
    focus_ = AttentionFocus{};
    candidates_.clear();
    details_.clear();
    factorCount_ = 0;
    scored_ = 0;
    resolvedFor_ = static_cast<std::size_t>(-1);
}

AttentionFocus AttentionModel::update(const Inputs& in) {
    const AttentionWeights& w = settings_.weights;
    const Personality& personality =
        in.personality != nullptr ? *in.personality : neutralPersonality();
    const float spanScale = 0.5f + personality.attentionSpan; // 1 at neutral

    // Resolve the authored tag weights against the world's words once per change of vocabulary.
    // `size()` only grows while a world is loaded, so it is a cheap and honest cache key.
    const std::size_t vocabulary = in.tags != nullptr ? in.tags->size() : 0;
    if (resolvedFor_ != vocabulary) {
        resolvedTags_.clear();
        for (const auto& [tag, weight] : settings_.tagWeights) {
            const std::uint64_t b = in.tags != nullptr ? in.tags->bit(tag) : 0;
            if (b != 0) {
                resolvedTags_.emplace_back(b, weight);
            }
        }
        resolvedFor_ = vocabulary;
    }

    candidates_.clear();
    details_.clear();

    const auto addCandidate = [&](const Detail& d, float score) {
        AttentionCandidate c;
        c.id = d.id;
        c.position = d.position;
        c.salience = score;
        candidates_.push_back(c);
        details_.push_back(d);
    };

    for (const Percept& p : in.percepts) {
        Detail d;
        d.id = subjectOf(p);
        d.position = p.position;
        d.lastSeen = p.seenAt;
        const float age = static_cast<float>(std::max(0.0, in.time - p.seenAt));
        d.confidence = p.visibility * std::exp(-age / 3.0f);

        float semantic = 0.0f;
        for (const auto& [bit, weight] : resolvedTags_) {
            if ((p.tags & bit) != 0) {
                semantic = std::max(semantic, weight);
            }
        }
        float movement = 0.0f;
        if (p.kind == InterestKind::Character && in.world != nullptr &&
            p.source < in.world->entities().size()) {
            movement = std::min(in.world->entities()[p.source]->state().groundSpeed() / 3.0f, 1.0f);
        }
        const float novelty =
            in.memory != nullptr ? in.memory->novelty(d.id, in.time, spanScale) : 1.0f;
        const float relevance = d.id == in.relevant ? 1.0f : 0.0f;

        d.factors[0] = {"salience", w.salience * p.salience};
        d.factors[1] = {"novelty", w.novelty * novelty};
        d.factors[2] = {"semantic", w.semantic * semantic};
        d.factors[3] = {"movement", w.movement * movement};
        d.factors[4] = {"relevance", w.relevance * relevance};
        d.factors[5] = {"sound", 0.0f};
        float score = 0.0f;
        for (const ScoreFactor& f : d.factors) {
            score += f.value;
        }
        // Freshness scales the whole thing: a remembered percept pulls less than a live one, which
        // is what lets attention drift off something the body has walked away from.
        score *= 0.25f + 0.75f * d.confidence;
        addCandidate(d, score);
    }

    const float sensitivity = 0.5f + personality.eventSensitivity; // 1 at neutral
    for (const PerceivedEvent& e : in.events) {
        const float age = static_cast<float>(std::max(0.0, in.time - e.time));
        const float fade = std::max(0.0f, 1.0f - age / std::max(in.eventSeconds, 1e-3f));
        if (fade <= 0.0f) {
            continue;
        }
        Detail d;
        d.id = eventSubject(e.sequence);
        d.position = e.position;
        d.lastSeen = e.time;
        d.isEvent = true;
        d.confidence = fade;
        const float sound = e.intensity * fade * sensitivity;
        const float novelty =
            in.memory != nullptr ? in.memory->novelty(d.id, in.time, spanScale) : 1.0f;
        d.factors[0] = {"sound", w.sound * sound};
        d.factors[1] = {"novelty", w.novelty * novelty * fade};
        d.factors[2] = {"relevance", d.id == in.relevant ? w.relevance : 0.0f};
        float score = 0.0f;
        for (const ScoreFactor& f : d.factors) {
            score += f.value;
        }
        addCandidate(d, score);
    }
    scored_ += candidates_.size();

    AttentionState next;
    const AttentionResult result =
        chooseAttention(settings_.hysteresis, state_, candidates_, in.time, next);
    state_ = next;

    focus_ = AttentionFocus{};
    factorCount_ = 0;
    if (result.hasTarget) {
        for (std::size_t i = 0; i < details_.size(); ++i) {
            if (details_[i].id != result.target) {
                continue;
            }
            const Detail& d = details_[i];
            focus_.id = d.id;
            focus_.position = d.position;
            focus_.score = candidates_[i].salience;
            focus_.confidence = d.confidence;
            focus_.lastSeen = d.lastSeen;
            focus_.isEvent = d.isEvent;
            focus_.weight = result.weight;
            focus_.dwell = result.dwell;
            // The reason is the largest contribution: "sound", "novelty", "semantic"...
            float best = 0.0f;
            for (const ScoreFactor& f : d.factors) {
                if (f.name.empty()) {
                    continue;
                }
                if (f.value > best) {
                    best = f.value;
                    focus_.reason = f.name;
                }
                if (f.value != 0.0f && factorCount_ < factors_.size()) {
                    factors_[factorCount_++] = f;
                }
            }
            std::sort(factors_.begin(), factors_.begin() + static_cast<std::ptrdiff_t>(factorCount_),
                      [](const ScoreFactor& a, const ScoreFactor& b) { return a.value > b.value; });
            break;
        }
    }
    return focus_;
}

} // namespace avgen::entity
