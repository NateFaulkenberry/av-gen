#include "directing/plan.hpp"

#include "directing/text.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace avgen::directing {
namespace {

using json = nlohmann::json;

// ---- name tables ------------------------------------------------------------------------------

template <typename E, std::size_t N>
using Table = std::array<std::pair<E, const char*>, N>;

template <typename E, std::size_t N>
const char* nameIn(const Table<E, N>& table, E value) {
    for (const auto& [e, n] : table) {
        if (e == value) {
            return n;
        }
    }
    return table[0].second;
}
template <typename E, std::size_t N>
std::optional<E> valueIn(const Table<E, N>& table, std::string_view name) {
    for (const auto& [e, n] : table) {
        if (name == n) {
            return e;
        }
    }
    return std::nullopt;
}
template <typename E, std::size_t N>
std::vector<std::string> namesIn(const Table<E, N>& table) {
    std::vector<std::string> out;
    for (const auto& [e, n] : table) {
        out.emplace_back(n);
    }
    return out;
}

constexpr Table<Tier, 3> kTiers{{{Tier::Baked, "baked"}, {Tier::Directed, "directed"}, {Tier::Goal, "goal"}}};
constexpr Table<SubjectKind, 8> kSubjectKinds{{
    {SubjectKind::Unresolved, "unresolved"}, {SubjectKind::Entity, "entity"}, {SubjectKind::Hero, "hero"},
    {SubjectKind::Node, "node"}, {SubjectKind::Camera, "camera"}, {SubjectKind::Effect, "effect"},
    {SubjectKind::Parameter, "parameter"}, {SubjectKind::World, "world"},
}};
constexpr Table<CameraMove, 13> kMoves{{
    {CameraMove::Chase, "chase"}, {CameraMove::Follow, "follow"}, {CameraMove::RiseOver, "rise_over"},
    {CameraMove::Pass, "pass"}, {CameraMove::Orbit, "orbit"}, {CameraMove::Hold, "hold"},
    {CameraMove::Reveal, "reveal"}, {CameraMove::PushIn, "push_in"}, {CameraMove::PullOut, "pull_out"},
    {CameraMove::Wide, "wide"}, {CameraMove::Close, "close"}, {CameraMove::TopDown, "top_down"},
    {CameraMove::LowAngle, "low_angle"},
}};
constexpr Table<PerformanceMode, 3> kModes{{
    {PerformanceMode::Scripted, "scripted"}, {PerformanceMode::Directed, "directed"}, {PerformanceMode::Goal, "goal"},
}};
constexpr Table<ContentDomain, 9> kDomains{{
    {ContentDomain::SequenceShot, "sequence.shot"}, {ContentDomain::SequenceMarker, "sequence.marker"},
    {ContentDomain::SequenceActor, "sequence.actor"}, {ContentDomain::SequenceEvent, "sequence.event"},
    {ContentDomain::SequenceTrack, "sequence.track"}, {ContentDomain::CameraRig, "camera.rig"},
    {ContentDomain::CameraShot, "camera.shot"}, {ContentDomain::TimelineTrack, "timeline.track"},
    {ContentDomain::EffectInstance, "effect.instance"},
}};
static_assert(kSubjectKinds.back().first == SubjectKind::World);
static_assert(kMoves.back().first == CameraMove::LowAngle);
static_assert(kDomains.back().first == ContentDomain::EffectInstance);

// ---- a reader that reports rather than throws ---------------------------------------------------
//
// Every field read goes through one of these, so a wrong type is a SCHEMA_INVALID naming the JSON
// pointer, a missing required field says which, and every key the reader did not consume is a
// SCHEMA_UNKNOWN_FIELD warning -- which is how a model that invents a field finds out, instead of the
// field silently doing nothing.
class Reader {
public:
    Reader(const json& object, std::string path, std::vector<Issue>& issues)
        : object_(object), path_(std::move(path)), issues_(issues) {}
    ~Reader() { reportUnknown(); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::string at(std::string_view key) const { return path_ + "/" + std::string(key); }
    [[nodiscard]] bool has(std::string_view key) {
        return object_.is_object() && object_.contains(std::string(key));
    }

    const json* raw(std::string_view key) {
        seen_.insert(std::string(key));
        if (!object_.is_object()) {
            return nullptr;
        }
        const auto it = object_.find(std::string(key));
        return it == object_.end() ? nullptr : &*it;
    }

    std::string string(std::string_view key, bool required = false) {
        const json* v = raw(key);
        if (v == nullptr) {
            if (required) {
                fail(key, "is required");
            }
            return {};
        }
        if (!v->is_string()) {
            fail(key, "must be a string");
            return {};
        }
        return v->get<std::string>();
    }
    template <typename T>
    std::optional<T> number(std::string_view key, bool required = false) {
        const json* v = raw(key);
        if (v == nullptr) {
            if (required) {
                fail(key, "is required");
            }
            return std::nullopt;
        }
        if (!v->is_number()) {
            fail(key, "must be a number");
            return std::nullopt;
        }
        return v->get<T>();
    }
    std::optional<bool> boolean(std::string_view key) {
        const json* v = raw(key);
        if (v == nullptr) {
            return std::nullopt;
        }
        if (!v->is_boolean()) {
            fail(key, "must be true or false");
            return std::nullopt;
        }
        return v->get<bool>();
    }
    std::optional<TimeRef> time(std::string_view key, bool required = false) {
        const json* v = raw(key);
        if (v == nullptr) {
            if (required) {
                fail(key, "is required");
            }
            return std::nullopt;
        }
        const std::size_t before = issues_.size();
        auto t = TimeRef::fromJson(*v, at(key), issues_);
        if (!t) {
            ok_ = false;
        }
        (void)before;
        return t;
    }
    template <typename E, std::size_t N>
    std::optional<E> choice(std::string_view key, const Table<E, N>& table, bool required = false) {
        const std::string name = string(key, required);
        if (name.empty()) {
            return std::nullopt;
        }
        if (auto v = valueIn(table, name)) {
            return v;
        }
        Issue issue = invalid(key, fmt::format("'{}' is not one of the allowed values", name));
        issue.details = {{"allowed", namesIn(table)}};
        issue.suggestions = text::nearest(name, namesIn(table));
        if (issue.suggestions.empty()) {
            issue.suggestions = namesIn(table);
        }
        issues_.push_back(std::move(issue));
        ok_ = false;
        return std::nullopt;
    }
    const json* array(std::string_view key) {
        const json* v = raw(key);
        if (v == nullptr) {
            return nullptr;
        }
        if (!v->is_array()) {
            fail(key, "must be an array");
            return nullptr;
        }
        return v;
    }

    void fail(std::string_view key, std::string_view why) {
        issues_.push_back(invalid(key, fmt::format("'{}' {}", key, why)));
        ok_ = false;
    }

private:
    Issue invalid(std::string_view key, std::string message) const {
        Issue issue;
        issue.code = IssueCode::SchemaInvalid;
        issue.location = at(key);
        issue.message = std::move(message);
        return issue;
    }
    void reportUnknown() {
        if (!object_.is_object()) {
            return;
        }
        for (const auto& [key, value] : object_.items()) {
            if (seen_.contains(key)) {
                continue;
            }
            Issue issue;
            issue.severity = Severity::Warning;
            issue.code = IssueCode::SchemaUnknownField;
            issue.location = at(key);
            issue.message = fmt::format("'{}' is not a field of this object in schema version {}; it is ignored",
                                        key, kPlanSchemaVersion);
            std::vector<std::string> known(seen_.begin(), seen_.end());
            issue.suggestions = text::nearest(key, known);
            issues_.push_back(std::move(issue));
        }
    }

    const json& object_;
    std::string path_;
    std::vector<Issue>& issues_;
    std::set<std::string> seen_;
    bool ok_ = true;
};

// Reads every element of an array field with `each(reader, element, index)`. Non-objects are refused
// by position.
template <typename F>
void forEach(Reader& parent, std::string_view key, std::vector<Issue>& issues, F&& each) {
    const json* list = parent.array(key);
    if (list == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < list->size(); ++i) {
        const std::string path = parent.at(key) + "/" + std::to_string(i);
        if (!(*list)[i].is_object()) {
            Issue issue;
            issue.code = IssueCode::SchemaInvalid;
            issue.location = path;
            issue.message = "must be an object";
            issues.push_back(std::move(issue));
            continue;
        }
        Reader r((*list)[i], path, issues);
        each(r, i);
    }
}

void put(json& j, const char* key, const std::optional<TimeRef>& t) {
    if (t) {
        j[key] = t->toJson();
    }
}
template <typename T>
void put(json& j, const char* key, const std::optional<T>& v) {
    if (v) {
        j[key] = *v;
    }
}
void putString(json& j, const char* key, const std::string& v) {
    if (!v.empty()) {
        j[key] = v;
    }
}

} // namespace

// ---- names ----------------------------------------------------------------------------------------

const char* tierName(Tier v) { return nameIn(kTiers, v); }
std::optional<Tier> tierFromName(std::string_view n) { return valueIn(kTiers, n); }
const char* subjectKindName(SubjectKind v) { return nameIn(kSubjectKinds, v); }
std::optional<SubjectKind> subjectKindFromName(std::string_view n) { return valueIn(kSubjectKinds, n); }
const char* cameraMoveName(CameraMove v) { return nameIn(kMoves, v); }
std::optional<CameraMove> cameraMoveFromName(std::string_view n) { return valueIn(kMoves, n); }
const char* performanceModeName(PerformanceMode v) { return nameIn(kModes, v); }
std::optional<PerformanceMode> performanceModeFromName(std::string_view n) { return valueIn(kModes, n); }
const char* contentDomainName(ContentDomain v) { return nameIn(kDomains, v); }
std::optional<ContentDomain> contentDomainFromName(std::string_view n) { return valueIn(kDomains, n); }
std::vector<std::string> cameraMoveNames() { return namesIn(kMoves); }
std::vector<std::string> subjectKindNames() { return namesIn(kSubjectKinds); }

// ---- the plan -------------------------------------------------------------------------------------

const Subject* Plan::subject(std::string_view alias) const {
    const auto it = std::find_if(subjects.begin(), subjects.end(), [&](const Subject& s) { return s.alias == alias; });
    return it == subjects.end() ? nullptr : &*it;
}
Subject* Plan::subject(std::string_view alias) {
    return const_cast<Subject*>(static_cast<const Plan*>(this)->subject(alias));
}

json Plan::toJson() const {
    json j;
    j["schemaVersion"] = schemaVersion;
    j["id"] = id;
    j["revision"] = revision;
    putString(j, "title", title);
    j["tier"] = tierName(tier);
    json prov = json::object();
    putString(prov, "author", provenance.author);
    putString(prov, "source", provenance.source);
    putString(prov, "request", provenance.request);
    if (!prov.empty()) {
        j["provenance"] = std::move(prov);
    }

    json subjectsJson = json::array();
    for (const Subject& s : subjects) {
        json o{{"alias", s.alias}};
        putString(o, "text", s.text);
        if (s.hint != SubjectKind::Unresolved) {
            o["hint"] = subjectKindName(s.hint);
        }
        if (s.kind != SubjectKind::Unresolved) {
            o["resolved"] = {{"kind", subjectKindName(s.kind)}, {"id", s.id}};
        }
        subjectsJson.push_back(std::move(o));
    }
    j["subjects"] = std::move(subjectsJson);

    json shotsJson = json::array();
    for (const PlanShot& s : shots) {
        json o{{"key", s.key}, {"name", s.name}, {"start", s.start.toJson()}, {"durationSeconds", s.durationSeconds},
               {"transition", s.transition}};
        putString(o, "subject", s.subject);
        putString(o, "rig", s.rig);
        if (s.locked) {
            o["locked"] = true;
        }
        json cam = json::array();
        for (const CameraBeat& b : s.camera) {
            json c{{"move", cameraMoveName(b.move)}};
            putString(c, "subject", b.subject);
            put(c, "at", b.at);
            put(c, "heightMetres", b.heightMetres);
            put(c, "distanceMetres", b.distanceMetres);
            put(c, "degrees", b.degrees);
            putString(c, "side", b.side);
            cam.push_back(std::move(c));
        }
        o["camera"] = std::move(cam);
        shotsJson.push_back(std::move(o));
    }
    j["shots"] = std::move(shotsJson);

    json markersJson = json::array();
    for (const PlanMarker& m : markers) {
        markersJson.push_back({{"key", m.key}, {"name", m.name}, {"at", m.at.toJson()}});
    }
    j["markers"] = std::move(markersJson);

    json perfJson = json::array();
    for (const PlanPerformance& p : performances) {
        json beats = json::array();
        for (const PerformanceBeat& b : p.beats) {
            json o{{"action", b.action}};
            putString(o, "target", b.target);
            put(o, "at", b.at);
            put(o, "seconds", b.seconds);
            putString(o, "emits", b.emits);
            putString(o, "moment", b.moment);
            put(o, "clearanceMetres", b.clearanceMetres);
            beats.push_back(std::move(o));
        }
        json perf{{"key", p.key}, {"subject", p.subject}, {"mode", performanceModeName(p.mode)},
                  {"beats", std::move(beats)}};
        if (p.entrySeconds != 0.0) {
            perf["entrySeconds"] = p.entrySeconds; // omitted at its default: canonical JSON
        }
        perfJson.push_back(std::move(perf));
    }
    j["performances"] = std::move(perfJson);

    json cuesJson = json::array();
    for (const PlanCue& c : cues) {
        json o{{"key", c.key}};
        putString(o, "parameter", c.parameter);
        if (c.effect) {
            json e{{"owner", c.effect->owner}, {"type", c.effect->type}};
            putString(e, "id", c.effect->id);
            o["effect"] = std::move(e);
        }
        putString(o, "field", c.field);
        put(o, "at", c.at);
        putString(o, "on", c.on);
        put(o, "until", c.until);
        if (c.holdSeconds != 0.0) {
            o["holdSeconds"] = c.holdSeconds;
        }
        put(o, "value", c.value);
        if (c.rampSeconds != 0.0) {
            o["rampSeconds"] = c.rampSeconds;
        }
        cuesJson.push_back(std::move(o));
    }
    j["cues"] = std::move(cuesJson);

    json retimesJson = json::array();
    for (const PlanRetime& r : retimes) {
        retimesJson.push_back({{"key", r.key}, {"performance", r.performance}, {"from", r.from.toJson()},
                               {"until", r.until.toJson()}, {"factor", r.factor}});
    }
    j["retimes"] = std::move(retimesJson);

    json producedJson = json::array();
    for (const ContentRef& c : produced) {
        json o{{"item", c.item}, {"domain", contentDomainName(c.domain)}, {"id", c.id}};
        putString(o, "fingerprint", c.fingerprint);
        producedJson.push_back(std::move(o));
    }
    j["produced"] = std::move(producedJson);
    return j;
}

PlanParse parsePlan(const json& document) {
    PlanParse out;
    std::vector<Issue>& issues = out.issues;
    if (!document.is_object()) {
        Issue issue;
        issue.code = IssueCode::SchemaInvalid;
        issue.location = "";
        issue.message = "a plan must be a JSON object";
        issues.push_back(std::move(issue));
        return out;
    }
    // The version first and alone: a newer document's other fields mean things this build cannot
    // know, so nothing else is read and nothing is reported about them.
    if (!document.contains("schemaVersion") || !document["schemaVersion"].is_number_integer()) {
        Issue issue;
        issue.code = IssueCode::SchemaInvalid;
        issue.location = "/schemaVersion";
        issue.message = fmt::format("'schemaVersion' is required (this build writes {})", kPlanSchemaVersion);
        issues.push_back(std::move(issue));
        return out;
    }
    const int version = document["schemaVersion"].get<int>();
    if (version > kPlanSchemaVersion || version < 1) {
        Issue issue;
        issue.code = IssueCode::SchemaVersionUnsupported;
        issue.location = "/schemaVersion";
        issue.recoverable = false;
        issue.message = fmt::format("this plan is schema version {}; this build reads versions 1-{}", version,
                                    kPlanSchemaVersion);
        issue.suggestions = {"open it in a newer build of AV Gen"};
        issues.push_back(std::move(issue));
        return out;
    }

    Plan plan;
    bool ok = true;
    {
        Reader r(document, "", issues);
        (void)r.raw("schemaVersion");
        plan.schemaVersion = kPlanSchemaVersion; // v1 is the only version; a migration would go here
        plan.id = r.string("id", true);
        if (plan.id.find('/') != std::string::npos) {
            r.fail("id", "may not contain '/'");
        }
        plan.revision = r.number<int>("revision").value_or(1);
        if (plan.revision < 1) {
            r.fail("revision", "counts from 1");
        }
        plan.title = r.string("title");
        plan.tier = r.choice("tier", kTiers).value_or(Tier::Baked);
        if (const json* prov = r.raw("provenance"); prov != nullptr) {
            Reader p(*prov, "/provenance", issues);
            plan.provenance.author = p.string("author");
            plan.provenance.source = p.string("source");
            plan.provenance.request = p.string("request");
            ok = ok && p.ok();
        }

        forEach(r, "subjects", issues, [&](Reader& s, std::size_t) {
            Subject subject;
            subject.alias = s.string("alias", true);
            subject.text = s.string("text");
            subject.hint = s.choice("hint", kSubjectKinds).value_or(SubjectKind::Unresolved);
            if (const json* res = s.raw("resolved"); res != nullptr) {
                Reader rr(*res, s.at("resolved"), issues);
                subject.kind = rr.choice("kind", kSubjectKinds, true).value_or(SubjectKind::Unresolved);
                subject.id = rr.string("id", true);
                ok = ok && rr.ok();
            }
            ok = ok && s.ok();
            plan.subjects.push_back(std::move(subject));
        });

        forEach(r, "shots", issues, [&](Reader& s, std::size_t) {
            PlanShot shot;
            shot.key = s.string("key", true);
            shot.name = s.string("name", true);
            shot.start = s.time("start", true).value_or(TimeRef{});
            shot.durationSeconds = s.number<double>("durationSeconds", true).value_or(0.0);
            if (s.has("durationSeconds") && shot.durationSeconds <= 0.0) {
                s.fail("durationSeconds", "must be positive");
            }
            shot.subject = s.string("subject");
            shot.rig = s.string("rig");
            shot.transition = s.has("transition") ? s.string("transition") : std::string("cut");
            shot.locked = s.boolean("locked").value_or(false);
            forEach(s, "camera", issues, [&](Reader& c, std::size_t) {
                CameraBeat beat;
                beat.move = c.choice("move", kMoves, true).value_or(CameraMove::Hold);
                beat.subject = c.string("subject");
                beat.at = c.time("at");
                beat.heightMetres = c.number<float>("heightMetres");
                beat.distanceMetres = c.number<float>("distanceMetres");
                beat.degrees = c.number<float>("degrees");
                beat.side = c.string("side");
                if (!beat.side.empty() && beat.side != "left" && beat.side != "right") {
                    c.fail("side", "must be \"left\" or \"right\"");
                }
                ok = ok && c.ok();
                shot.camera.push_back(std::move(beat));
            });
            ok = ok && s.ok();
            plan.shots.push_back(std::move(shot));
        });

        forEach(r, "markers", issues, [&](Reader& m, std::size_t) {
            PlanMarker marker;
            marker.key = m.string("key", true);
            marker.name = m.string("name", true);
            marker.at = m.time("at", true).value_or(TimeRef{});
            ok = ok && m.ok();
            plan.markers.push_back(std::move(marker));
        });

        forEach(r, "performances", issues, [&](Reader& p, std::size_t) {
            PlanPerformance perf;
            perf.key = p.string("key", true);
            perf.subject = p.string("subject", true);
            perf.mode = p.choice("mode", kModes).value_or(PerformanceMode::Scripted);
            perf.entrySeconds = p.number<double>("entrySeconds").value_or(0.0);
            forEach(p, "beats", issues, [&](Reader& b, std::size_t) {
                PerformanceBeat beat;
                beat.action = b.string("action", true);
                beat.target = b.string("target");
                beat.at = b.time("at");
                beat.seconds = b.number<double>("seconds");
                beat.emits = b.string("emits");
                beat.clearanceMetres = b.number<float>("clearanceMetres");
                beat.moment = b.string("moment");
                ok = ok && b.ok();
                perf.beats.push_back(std::move(beat));
            });
            ok = ok && p.ok();
            plan.performances.push_back(std::move(perf));
        });

        forEach(r, "cues", issues, [&](Reader& c, std::size_t) {
            PlanCue cue;
            cue.key = c.string("key", true);
            cue.parameter = c.string("parameter");
            if (const json* e = c.raw("effect"); e != nullptr) {
                Reader er(*e, c.at("effect"), issues);
                EffectRef ref;
                ref.id = er.string("id");
                ref.owner = er.string("owner", true);
                ref.type = er.string("type", true);
                ok = ok && er.ok();
                cue.effect = std::move(ref);
            }
            if (cue.parameter.empty() == !cue.effect.has_value()) {
                c.fail("parameter", "a cue names exactly one of 'parameter' or 'effect'");
            }
            cue.field = c.string("field");
            cue.at = c.time("at");
            cue.on = c.string("on");
            if (cue.at.has_value() == !cue.on.empty()) {
                c.fail("at", "a cue starts either 'at' a time or 'on' a plan event, not both and not neither");
            }
            cue.until = c.time("until");
            cue.holdSeconds = c.number<double>("holdSeconds").value_or(0.0);
            cue.value = c.number<float>("value");
            cue.rampSeconds = c.number<double>("rampSeconds").value_or(0.0);
            if (cue.holdSeconds < 0.0 || cue.rampSeconds < 0.0) {
                c.fail("rampSeconds", "ramp and hold are not negative");
            }
            ok = ok && c.ok();
            plan.cues.push_back(std::move(cue));
        });

        forEach(r, "retimes", issues, [&](Reader& t, std::size_t) {
            PlanRetime retime;
            retime.key = t.string("key", true);
            retime.performance = t.string("performance", true);
            retime.from = t.time("from", true).value_or(TimeRef{});
            retime.until = t.time("until", true).value_or(TimeRef{});
            retime.factor = t.number<double>("factor", true).value_or(1.0);
            if (retime.factor <= 0.0) {
                t.fail("factor", "must be positive (0.35 is slower, 2 is faster)");
            }
            ok = ok && t.ok();
            plan.retimes.push_back(std::move(retime));
        });

        forEach(r, "produced", issues, [&](Reader& p, std::size_t) {
            ContentRef ref;
            ref.item = p.string("item", true);
            ref.domain = p.choice("domain", kDomains, true).value_or(ContentDomain::SequenceShot);
            ref.id = p.string("id", true);
            ref.fingerprint = p.string("fingerprint");
            ok = ok && p.ok();
            plan.produced.push_back(std::move(ref));
        });
        ok = ok && r.ok();
    }

    // ---- cross-item shape: unique keys, declared aliases, references between items ----------------
    std::set<std::string> keys;
    const auto key = [&](const std::string& k, const std::string& where) {
        if (!k.empty() && !keys.insert(k).second) {
            Issue issue;
            issue.code = IssueCode::DuplicateKey;
            issue.location = where;
            issue.subject = k;
            issue.message = fmt::format("key '{}' is used by more than one item; keys identify items across revisions", k);
            issues.push_back(std::move(issue));
            ok = false;
        }
    };
    for (std::size_t i = 0; i < plan.shots.size(); ++i) {
        key(plan.shots[i].key, fmt::format("/shots/{}/key", i));
    }
    for (std::size_t i = 0; i < plan.markers.size(); ++i) {
        key(plan.markers[i].key, fmt::format("/markers/{}/key", i));
    }
    for (std::size_t i = 0; i < plan.performances.size(); ++i) {
        key(plan.performances[i].key, fmt::format("/performances/{}/key", i));
    }
    for (std::size_t i = 0; i < plan.cues.size(); ++i) {
        key(plan.cues[i].key, fmt::format("/cues/{}/key", i));
    }
    for (std::size_t i = 0; i < plan.retimes.size(); ++i) {
        key(plan.retimes[i].key, fmt::format("/retimes/{}/key", i));
    }
    std::set<std::string> aliases;
    for (std::size_t i = 0; i < plan.subjects.size(); ++i) {
        if (!aliases.insert(plan.subjects[i].alias).second) {
            Issue issue;
            issue.code = IssueCode::DuplicateKey;
            issue.location = fmt::format("/subjects/{}/alias", i);
            issue.subject = plan.subjects[i].alias;
            issue.message = fmt::format("subject alias '{}' is declared twice", plan.subjects[i].alias);
            issues.push_back(std::move(issue));
            ok = false;
        }
    }
    std::vector<std::string> aliasList(aliases.begin(), aliases.end());
    const auto declared = [&](const std::string& alias, const std::string& where, bool worldAllowed = false) {
        if (alias.empty() || aliases.contains(alias) || (worldAllowed && alias == "world")) {
            return;
        }
        Issue issue;
        issue.code = IssueCode::UndeclaredSubject;
        issue.location = where;
        issue.subject = alias;
        issue.message = fmt::format("'{}' is not declared in the plan's subjects", alias);
        issue.suggestions = text::nearest(alias, aliasList);
        issues.push_back(std::move(issue));
        ok = false;
    };
    for (std::size_t i = 0; i < plan.shots.size(); ++i) {
        declared(plan.shots[i].subject, fmt::format("/shots/{}/subject", i));
        for (std::size_t b = 0; b < plan.shots[i].camera.size(); ++b) {
            declared(plan.shots[i].camera[b].subject, fmt::format("/shots/{}/camera/{}/subject", i, b));
        }
    }
    for (std::size_t i = 0; i < plan.performances.size(); ++i) {
        declared(plan.performances[i].subject, fmt::format("/performances/{}/subject", i));
        for (std::size_t b = 0; b < plan.performances[i].beats.size(); ++b) {
            declared(plan.performances[i].beats[b].target, fmt::format("/performances/{}/beats/{}/target", i, b));
        }
    }
    for (std::size_t i = 0; i < plan.cues.size(); ++i) {
        if (plan.cues[i].effect) {
            declared(plan.cues[i].effect->owner, fmt::format("/cues/{}/effect/owner", i), true);
        }
    }
    for (std::size_t i = 0; i < plan.retimes.size(); ++i) {
        const std::string& perf = plan.retimes[i].performance;
        if (std::none_of(plan.performances.begin(), plan.performances.end(),
                         [&](const PlanPerformance& p) { return p.key == perf; })) {
            Issue issue;
            issue.code = IssueCode::SchemaInvalid;
            issue.location = fmt::format("/retimes/{}/performance", i);
            issue.subject = perf;
            issue.message = fmt::format("retime names performance '{}', which the plan does not have", perf);
            issues.push_back(std::move(issue));
            ok = false;
        }
    }
    if (ok && !hasErrors(issues)) {
        out.plan = std::move(plan);
    }
    return out;
}

json planSchema() {
    const json time = {
        {"oneOf", json::array({"a string: \"1:30\", \"90s\", \"bar 64 beat 3\", \"the second chorus\", \"end of the bridge\", \"chorus 2 + 1.5s\"",
                               json{{"seconds", "number"}},
                               json{{"bar", "integer >= 1"}, {"beat", "integer >= 1"}},
                               json{{"section", "type, e.g. chorus"}, {"occurrence", "1-based; -1 = last"}, {"anchor", "start|end"}}})},
        {"note", "never convert musical time to seconds yourself; write what was asked"}};
    return {
        {"schemaVersion", kPlanSchemaVersion},
        {"required", {"schemaVersion", "id"}},
        {"fields",
         {{"id", "stable, readable, no '/'; reuse an existing plan's id to revise it"},
          {"revision", "set by the engine"},
          {"title", "string"},
          {"tier", namesIn(kTiers)},
          {"provenance", {{"author", "person|assistant|script"}, {"source", "string"}, {"request", "the request, verbatim"}}},
          {"subjects", json::array({{{"alias", "how items name it"}, {"text", "as the request said it"}, {"hint", namesIn(kSubjectKinds)}}})},
          {"shots", json::array({{{"key", "unique"}, {"name", "unique in the sequence"}, {"start", time}, {"durationSeconds", "> 0"},
                                  {"subject", "alias"}, {"rig", "an existing camera's name, optional"},
                                  {"transition", "cut|fadeIn|fadeOut|matchCut"}, {"locked", "bool: no event camera may take it"},
                                  {"camera", json::array({{{"move", namesIn(kMoves)}, {"subject", "alias"}, {"at", time},
                                                           {"heightMetres", "number"}, {"distanceMetres", "number"},
                                                           {"degrees", "orbit"}, {"side", "left|right"}}})}}})},
          {"markers", json::array({{{"key", "unique"}, {"name", "string"}, {"at", time}}})},
          {"performances", json::array({{{"key", "unique"}, {"subject", "alias"}, {"mode", namesIn(kModes)},
                                         {"entrySeconds", "0 (default): take the body at the mark; > 0 is live"},
                                         {"beats", json::array({{{"action", "run_to|walk_to|run|walk|jump|land|fall|look_at|..."},
                                                                 {"target", "alias"}, {"at", time}, {"seconds", "number"},
                                                                 {"emits", "a plan event name, e.g. rook.jump_peak"},
                                                                 {"moment", "jump: takeoff|peak|touchdown, which moment emits names"},
                                                                 {"clearanceMetres", "number"}}})}}})},
          {"cues", json::array({{{"key", "unique"}, {"parameter", "a parameter path"},
                                 {"effect", {{"owner", "alias or \"world\""}, {"type", "effect type"}, {"id", "optional"}}},
                                 {"field", "the effect's field; omit to activate it"}, {"at", time}, {"on", "a plan event"},
                                 {"until", time}, {"holdSeconds", "number"}, {"value", "number"}, {"rampSeconds", "number"}}})},
          {"retimes", json::array({{{"key", "unique"}, {"performance", "key"}, {"from", time}, {"until", time}, {"factor", "> 0"}}})},
          {"produced", "set by the engine; never write it"}}},
        {"rules",
         {"a cue names exactly one of parameter or effect, and starts either at a time or on a plan event",
          "never ask for a capability the subject does not list; the validator refuses it and says what exists",
          "a baked plan may not depend on directed or goal performances"}}};
}

std::string mintPlanId(std::string_view title, const std::vector<std::string>& taken) {
    std::string base;
    for (const std::string& w : text::words(title)) {
        base += (base.empty() ? "" : "-") + w;
    }
    if (base.empty()) {
        base = "plan";
    }
    std::string id = base;
    for (int n = 2; std::find(taken.begin(), taken.end(), id) != taken.end(); ++n) {
        id = fmt::format("{}-{}", base, n);
    }
    return id;
}

} // namespace avgen::directing
