#include "world/effects/effect_instance.hpp"

#include "world/effects/effect_registry.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <utility>

namespace avgen::world {
namespace {

using json = nlohmann::json;

glm::vec3 vec3FromJson(const json& j, glm::vec3 fallback) {
    if (!j.is_array() || j.size() != 3) {
        return fallback;
    }
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}
json vec3ToJson(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

double readDouble(const json& j, const char* key, double fallback) {
    return j.contains(key) && j.at(key).is_number() ? j.at(key).get<double>() : fallback;
}
bool readBool(const json& j, const char* key, bool fallback) {
    return j.contains(key) && j.at(key).is_boolean() ? j.at(key).get<bool>() : fallback;
}
std::string readString(const json& j, const char* key, std::string fallback = {}) {
    return j.contains(key) && j.at(key).is_string() ? j.at(key).get<std::string>() : std::move(fallback);
}

json timingToJson(const Timing& t) {
    return json{{"delay", t.delay},           {"lifetime", t.lifetime},
                {"fadeIn", t.fadeIn},         {"fadeOut", t.fadeOut},
                {"windowStart", t.windowStart}, {"windowSeconds", t.windowSeconds},
                {"repeatSeconds", t.repeatSeconds}};
}
Timing timingFromJson(const json& j) {
    Timing t;
    if (!j.is_object()) { return t; }
    t.delay = readDouble(j, "delay", t.delay);
    t.lifetime = readDouble(j, "lifetime", t.lifetime);
    t.fadeIn = readDouble(j, "fadeIn", t.fadeIn);
    t.fadeOut = readDouble(j, "fadeOut", t.fadeOut);
    t.windowStart = readDouble(j, "windowStart", t.windowStart);
    t.windowSeconds = readDouble(j, "windowSeconds", t.windowSeconds);
    t.repeatSeconds = readDouble(j, "repeatSeconds", t.repeatSeconds);
    return t;
}

// ADR-500. The shared rows -- lifecycle, ground illumination, the field subscription -- are the
// effect's rather than a type's, so they are written at the document root rather than inside the
// `parameters` block. One helper pair, used by both directions: a reader and a writer that walk one
// list cannot drop a key from one of them.
//
// Timing rows are skipped: they register (so they are automatable, and the conformance round trip
// still covers them) but their file representation is `timingToJson`'s `double`, and reading them
// back as float would round every authored delay.
bool sharedRowIsTiming(const EffectField& field) {
    return std::string_view(field.jsonPath).starts_with("timing/");
}

// "ground/intensity" -> j["ground"]["intensity"]. One level, which is all a shared row needs.
std::pair<std::string, std::string> sharedGroupKey(const EffectField& field) {
    const std::string_view path(field.jsonPath);
    const std::size_t slash = path.find('/');
    if (slash == std::string_view::npos) {
        return {std::string(path), std::string()};
    }
    return {std::string(path.substr(0, slash)), std::string(path.substr(slash + 1))};
}

void writeSharedField(const EffectField& field, const EffectSchema& schema, const EffectInstance& e,
                      json& out) {
    if (sharedRowIsTiming(field) || !sharedFieldApplies(schema, field)) {
        return;
    }
    const auto [group, key] = sharedGroupKey(field);
    if (!out.contains(group) || !out.at(group).is_object()) {
        out[group] = json::object();
    }
    switch (field.type) {
    case FieldType::Float: out[group][key] = fieldFloat(field, schema, e); break;
    case FieldType::Color: out[group][key] = vec3ToJson(fieldColor(field, schema, e)); break;
    case FieldType::Bool: out[group][key] = fieldBool(field, schema, e); break;
    case FieldType::Choice: out[group][key] = std::string(field.choiceName(fieldFloat(field, schema, e))); break;
    }
}

void readSharedField(const EffectField& field, const EffectSchema& schema, const json& in, EffectInstance& e) {
    if (sharedRowIsTiming(field) || !sharedFieldApplies(schema, field)) {
        return;
    }
    const auto [group, key] = sharedGroupKey(field);
    if (!in.contains(group) || !in.at(group).is_object() || !in.at(group).contains(key)) {
        return; // absent leaves the default
    }
    const json& v = in.at(group).at(key);
    switch (field.type) {
    case FieldType::Float:
        if (v.is_number()) { setFieldFloat(field, schema, e, v.get<float>()); }
        break;
    case FieldType::Color: setFieldColor(field, schema, e, vec3FromJson(v, fieldColor(field, schema, e))); break;
    case FieldType::Bool:
        if (v.is_boolean()) { setFieldBool(field, schema, e, v.get<bool>()); }
        break;
    case FieldType::Choice:
        if (v.is_string()) {
            if (const int i = field.choiceIndex(v.get<std::string>()); i >= 0) {
                setFieldFloat(field, schema, e, static_cast<float>(i));
            }
        }
        break;
    }
}

} // namespace

// ---- names -------------------------------------------------------------------------------------

const char* effectTargetName(EffectTarget t) {
    switch (t) {
    case EffectTarget::World: return "world";
    case EffectTarget::Entity: return "entity";
    case EffectTarget::Camera: return "camera";
    case EffectTarget::Light: return "light";
    }
    return "world";
}

std::optional<EffectTarget> effectTargetFromName(std::string_view name) {
    if (name == "world") { return EffectTarget::World; }
    if (name == "entity") { return EffectTarget::Entity; }
    if (name == "camera") { return EffectTarget::Camera; }
    if (name == "light") { return EffectTarget::Light; }
    return std::nullopt;
}

const char* effectStatusName(EffectStatus s) {
    switch (s) {
    case EffectStatus::Disabled: return "disabled";
    case EffectStatus::Dormant: return "dormant";
    case EffectStatus::Drawn: return "drawn";
    case EffectStatus::Dropped: return "dropped";
    case EffectStatus::Orphaned: return "orphaned";
    case EffectStatus::Partial: return "partial";
    }
    return "dormant";
}

// ---- the owner ---------------------------------------------------------------------------------

std::string EffectOwner::label() const {
    switch (kind) {
    case EffectTarget::World: return "World";
    case EffectTarget::Camera: return name.empty() ? std::string("Camera") : name;
    case EffectTarget::Entity:
    case EffectTarget::Light: return name;
    }
    return name;
}

Result<void> EffectOwner::validate() const {
    switch (kind) {
    case EffectTarget::World:
        if (!name.empty()) {
            return fail("a World owner has no name (got '{}')", name);
        }
        return {};
    case EffectTarget::Entity:
    case EffectTarget::Light:
        if (name.empty()) {
            return fail("an {} owner needs a name", effectTargetName(kind));
        }
        return {};
    case EffectTarget::Camera: return {}; // empty is "the active camera"
    }
    return {};
}

json EffectOwner::toJson() const {
    json j;
    j["kind"] = effectTargetName(kind);
    if (!name.empty()) {
        j["name"] = name;
    }
    return j;
}

Result<EffectOwner> EffectOwner::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("an effect owner must be an object");
    }
    EffectOwner o;
    const std::string kind = readString(j, "kind", "world");
    const auto parsed = effectTargetFromName(kind);
    if (!parsed) {
        return fail("unknown effect owner kind '{}'", kind);
    }
    o.kind = *parsed;
    o.name = readString(j, "name");
    if (auto ok = o.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return o;
}

// ---- validation --------------------------------------------------------------------------------

Result<void> EffectInstance::validate() const {
    const std::string& who = id.empty() ? name : id;
    if (id.empty()) {
        return fail("effect '{}' has no id", name);
    }
    if (id.find('/') != std::string::npos) {
        // The id is half of a parameter path (`fx/<id>/intensity`); a slash in it would silently
        // invent a group nobody can address.
        return fail("effect '{}': an id may not contain '/'", id);
    }
    if (auto ok = owner.validate(); !ok) { return fail("effect '{}': {}", who, ok.error().message); }
    if (order < 0) { return fail("effect '{}': order {} is negative", who, order); }
    // Every typed payload is validated whatever the type, because a default-constructed payload of
    // another type must be valid too -- and a broken one is a broken file whether or not this
    // instance reads it.
    if (auto ok = comet.validate(); !ok) { return fail("effect '{}': {}", who, ok.error().message); }
    if (auto ok = aurora.validate(); !ok) { return fail("effect '{}': {}", who, ok.error().message); }
    if (auto ok = vortex.validate(); !ok) { return fail("effect '{}': {}", who, ok.error().message); }
    if (auto ok = wave.validate(); !ok) { return fail("effect '{}': {}", who, ok.error().message); }
    if (auto ok = ground.validate(); !ok) { return fail("effect '{}': {}", who, ok.error().message); }
    if (auto ok = timing.validate(); !ok) { return fail("effect '{}': {}", who, ok.error().message); }
    // ADR-702: an `Owner` source means "wherever I am attached", and the World is attached to no
    // place. Refused rather than left dormant, because a pulse that can never fire is a card in the
    // panel that silently does nothing.
    if (effectSchema(kind) != nullptr && effectSchema(kind)->getSource != nullptr &&
        effectSchema(kind)->getSource(*this).kind == SourceKind::Owner && owner.isWorld()) {
        return fail("effect '{}': its source is its owner, and the World has no position to lend", who);
    }
    // ADR-500: and the rows of whatever type this is.
    if (auto ok = validateEffectFields(*this); !ok) { return ok; }
    return {};
}

// ---- JSON --------------------------------------------------------------------------------------
//
// ADR-702's canonical form, and the only one read:
//
//   { "id": "aurora", "type": "aurora", "name": "Aurora",
//     "owner": {"kind": "world"}, "enabled": true, "order": 0,
//     "style": "...", "activation": "always", "timing": {...},
//     "ground": {...}, "flow": {...},          <- the shared rows a type uses
//     "parameters": {...} }                    <- this type's own rows
//
// Only the instance's OWN type is written. Before ADR-702 every entry carried every kind's payload,
// because a kind could be switched and a switch must not lose the settings of the kind left; an
// instance's type is fixed now (switching is removing one and adding another), so the other
// payloads were dead weight in every file.

json EffectInstance::toJson() const {
    json j;
    j["id"] = id;
    j["type"] = effectKindName(kind);
    j["name"] = name;
    j["owner"] = owner.toJson();
    j["enabled"] = enabled;
    j["order"] = order;
    if (!style.empty()) { j["style"] = style; }
    j["activation"] = activationName(activation);
    j["timing"] = timingToJson(timing);

    const EffectSchema* schema = effectSchema(kind);
    if (schema == nullptr) {
        return j;
    }
    if (schema->groundGlow) {
        // The mode is a word rather than a number, so it is not a row; the three numbers and the
        // colour beside it are shared rows and come from the walk below.
        j["ground"] = json{{"mode", groundGlowName(ground.mode)}};
    }
    if (isAtmosphericBucket(schema->resolve.bucket)) {
        // §68. Written unconditionally, both halves, even when the subscription is the default: a
        // round trip passes perfectly when a key is missing from BOTH directions.
        j["flow"] = json{{"field", flow.field}};
    }
    for (const EffectField& field : sharedEffectFields()) {
        writeSharedField(field, *schema, *this, j);
    }
    effectPayloadToJson(*schema, *this, j);
    return j;
}

Result<EffectInstance> EffectInstance::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("an effect must be an object");
    }
    EffectInstance e;
    e.id = readString(j, "id");
    e.name = readString(j, "name");
    const std::string who = e.id.empty() ? e.name : e.id;
    if (!j.contains("type")) {
        // ADR-441/702: the pre-ADR-702 `worldEffects` / `atmosphericEffects` entries have `kind`
        // (or no type at all). Every tracked file was converted, so this names the format rather
        // than guessing at it.
        return fail("effect '{}' has no 'type' (a pre-ADR-702 entry? see docs/decisions/ADR-702)", who);
    }
    const auto kind = effectKindFromName(readString(j, "type"));
    if (!kind) {
        return fail("effect '{}': unknown type '{}'", who, readString(j, "type"));
    }
    e.kind = *kind;
    if (e.name.empty()) {
        if (const EffectSchema* schema = effectSchema(e.kind)) {
            e.name = schema->displayName;
        }
    }
    if (j.contains("owner")) {
        auto owner = EffectOwner::fromJson(j.at("owner"));
        if (!owner) {
            return fail("effect '{}': {}", who, owner.error().message);
        }
        e.owner = std::move(*owner);
    }
    e.enabled = readBool(j, "enabled", true);
    if (j.contains("order") && j.at("order").is_number_integer()) {
        e.order = j.at("order").get<int>();
    }
    e.style = readString(j, "style");

    if (j.contains("activation")) {
        const auto act = activationFromName(readString(j, "activation"));
        if (!act) {
            return fail("effect '{}': unknown activation '{}'", who, readString(j, "activation"));
        }
        e.activation = *act;
    }
    if (j.contains("timing")) {
        e.timing = timingFromJson(j.at("timing"));
    }
    if (j.contains("ground") && j.at("ground").is_object()) {
        const json& g = j.at("ground");
        if (g.contains("mode")) {
            const auto mode = groundGlowFromName(readString(g, "mode"));
            if (!mode) {
                return fail("effect '{}': unknown ground glow '{}'", who, readString(g, "mode"));
            }
            e.ground.mode = *mode;
        }
    }
    // §68. An absent block leaves the default, the unsubscribed effect. A name the scene does not
    // publish is reported at resolve by `fields::FieldBus::unresolved`, where the answer is known.
    if (j.contains("flow") && j.at("flow").is_object()) {
        e.flow.field = readString(j.at("flow"), "field");
    }

    const EffectSchema* schema = effectSchema(e.kind);
    if (schema != nullptr) {
        // ADR-500: the reader is the same walk as the writer, over the same rows.
        for (const EffectField& field : sharedEffectFields()) {
            readSharedField(field, *schema, j, e);
        }
        if (auto ok = effectPayloadFromJson(*schema, j, e); !ok) {
            return std::unexpected(ok.error());
        }
    }

    if (auto ok = e.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return e;
}

} // namespace avgen::world
