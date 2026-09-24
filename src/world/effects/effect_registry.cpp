#include "world/effects/effect_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_set>

namespace avgen::world {
namespace {

using json = nlohmann::json;

constexpr const char* kParametersKey = "parameters";

// ---- the list ------------------------------------------------------------------------------------
//
// ADR-500. **These are the two lines outside its own file that a new effect costs in this file**,
// and neither is a line anybody can forget quietly: `checkRegistry` requires a schema for every
// enumerator in `kEffectKinds`, and `tests/unit/test_effect_conformance.cpp` requires that
// array to cover exactly the enumerators declared in `atmospherics.hpp` -- by name, in both
// directions.
//
// An explicit list rather than static self-registration through a global constructor, deliberately.
// These files compile into a static library; a translation unit nothing references is dropped by
// the linker, and a registry that is correct in a debug build and empty in a release one is the
// worst failure this repository knows how to have. A declaration here IS a reference.
//
// Declared here rather than in a header of their own, because a header of their own was a third
// file to edit and bought nothing: nothing outside this function calls them.

} // namespace

const EffectSchema& cometSchema();
const EffectSchema& auroraSchema();
const EffectSchema& vortexSchema();
const EffectSchema& meteorShowerSchema();
const EffectSchema& volumetricFogSchema();
const EffectSchema& tornadoSchema();
const EffectSchema& groundPulseSchema(); // ADR-702
const EffectSchema& travelBeamSchema();  // ADR-702
const EffectSchema& glowSchema();        // ADR-703 (FXL)
const EffectSchema& pulseSchema();       // ADR-703 (FXL)
const EffectSchema& bloomSourceSchema(); // ADR-703 (FXL)
const EffectSchema& trailSchema();       // ADR-703 (Wave 1)
const EffectSchema& spaceWarpSchema();   // ADR-703 (DF)
const EffectSchema& particleEmitterSchema(); // ADR-703
const EffectSchema& orbitSchema();       // Wave 2 (XFORM)
const EffectSchema& spiralSchema();      // Wave 2 (XFORM)
const EffectSchema& floatSchema();       // Wave 2 (XFORM)
const EffectSchema& shakeSchema();       // Wave 2 (XFORM)
const EffectSchema& bounceSchema();      // Wave 2 (XFORM)
const EffectSchema& dissolveSchema();    // Wave 2 (FXL surface)
const EffectSchema& growthSchema();      // Wave 2 (FXL surface)
const EffectSchema& breathingSchema();   // Wave 2 (FXL surface)
const EffectSchema& organicPulsationSchema(); // Wave 2 (FXL surface)
const EffectSchema& bioluminescenceSchema();  // Wave 2 (FXL surface)
const EffectSchema& pulsingVeinsSchema(); // Wave 2 (FXL surface)
const EffectSchema& fresnelSchema();     // Wave 2 (FXL surface)
const EffectSchema& rimLightSchema();    // Wave 2 (FXL surface)
const EffectSchema& colorCyclingSchema(); // Wave 2 (FXL surface)
const EffectSchema& motionSmearSchema(); // Wave 2 (FXL surface)

namespace {

const std::vector<const EffectSchema*>& builtinSchemas() {
    static const std::vector<const EffectSchema*> kSchemas = {
        &cometSchema(),        //
        &auroraSchema(),       //
        &vortexSchema(),       //
        &meteorShowerSchema(), //
        &volumetricFogSchema(),
        &tornadoSchema(),
        &groundPulseSchema(),
        &travelBeamSchema(),
        &glowSchema(),
        &pulseSchema(),
        &bloomSourceSchema(),
        &trailSchema(),
        &spaceWarpSchema(),
        &particleEmitterSchema(),
        &orbitSchema(),
        &spiralSchema(),
        &floatSchema(),
        &shakeSchema(),
        &bounceSchema(),
        &dissolveSchema(),
        &growthSchema(),
        &breathingSchema(),
        &organicPulsationSchema(),
        &bioluminescenceSchema(),
        &pulsingVeinsSchema(),
        &fresnelSchema(),
        &rimLightSchema(),
        &colorCyclingSchema(),
        &motionSmearSchema(),
    };
    return kSchemas;
}

[[nodiscard]] bool numeric(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Splits "appearance/coreColor" into its segments. No allocation per call beyond the vector, which
// is fine: serialisation is not a per-frame path.
std::vector<std::string_view> segments(std::string_view path) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        if (end > start) {
            out.push_back(path.substr(start, end - start));
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return out;
}

json vec3ToJson(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

glm::vec3 vec3FromJson(const json& j, glm::vec3 fallback) {
    if (j.is_array() && j.size() >= 3 && j[0].is_number() && j[1].is_number() && j[2].is_number()) {
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    }
    return fallback;
}

// Walks `root` to the container holding the last segment, creating objects and arrays as it goes,
// and writes `value` there. The array case is what lets the vortex's three `centerX/Y/Z` rows share
// the one `"center": [x, y, z]` its file already has.
void writeAt(json& root, std::string_view path, json value) {
    const std::vector<std::string_view> parts = segments(path);
    if (parts.empty()) {
        return;
    }
    json* node = &root;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string key(parts[i]);
        if (numeric(parts[i])) {
            const std::size_t index = static_cast<std::size_t>(std::stoul(key));
            if (!node->is_array()) {
                *node = json::array();
            }
            while (node->size() <= index) {
                node->push_back(json());
            }
            node = &(*node)[index];
            continue;
        }
        if (!node->is_object()) {
            *node = json::object();
        }
        node = &(*node)[key];
    }
    const std::string last(parts.back());
    if (numeric(parts.back())) {
        const std::size_t index = static_cast<std::size_t>(std::stoul(last));
        if (!node->is_array()) {
            *node = json::array();
        }
        while (node->size() <= index) {
            node->push_back(0.0f);
        }
        (*node)[index] = std::move(value);
        return;
    }
    if (!node->is_object()) {
        *node = json::object();
    }
    (*node)[last] = std::move(value);
}

// The value at `path`, or null when any step of the way is absent. Absent leaves the caller's
// default in place, which is what every file written before a row existed describes.
const json* readAt(const json& root, std::string_view path) {
    const std::vector<std::string_view> parts = segments(path);
    const json* node = &root;
    for (const std::string_view part : parts) {
        if (numeric(part)) {
            const std::size_t index = static_cast<std::size_t>(std::stoul(std::string(part)));
            if (!node->is_array() || node->size() <= index) {
                return nullptr;
            }
            node = &(*node)[index];
            continue;
        }
        const std::string key(part);
        if (!node->is_object() || !node->contains(key)) {
            return nullptr;
        }
        node = &node->at(key);
    }
    return node;
}

// Where a row lives in the effect's JSON document.
//
// Three forms, and the third is what lets two kinds share one representation:
//   * declared and relative -- `"appearance/coreColor"` under this kind's own block;
//   * not declared -- the flat `<key>/<leaf>`, which is what a kind written after ADR-500 gets
//     without saying anything;
//   * declared and ABSOLUTE (a leading `/`) -- taken from the effect's root instead. A meteor
//     shower's streak and a fog bank's medium are the same structs the comet and the vortex use, so
//     they are the same keys: `"/comet/appearance/coreColor"`. The alternative is a second copy of
//     thirteen numbers in every saved file that has to agree with the first by hand, which is the
//     defect this whole ADR is about.
std::string jsonPathOf(const EffectSchema& schema, const EffectField& field) {
    const std::string_view declared(field.jsonPath);
    if (declared.starts_with('/')) {
        // A row that lives in ANOTHER payload's block -- a meteor shower's arc is a `Comet`, a fog
        // bank's medium is a `Vortex` -- names that block absolutely. Before ADR-702 that meant the
        // document root, beside the type's own block; with one payload per instance it means inside
        // `parameters`, beside the type's own rows.
        std::string path = kParametersKey;
        path.append(declared);
        return path;
    }
    // ADR-702: an instance writes ITS OWN type's rows only (a type never changes), under one
    // `parameters` block rather than a block named after the type. The block used to be keyed by
    // type because every instance wrote every type's payload, and the key was how a reader told
    // them apart; with one payload per instance the key said the same thing as `type` twice.
    std::string path = kParametersKey;
    path.push_back('/');
    path.append(declared.empty() ? std::string_view(field.leaf) : declared);
    return path;
}

} // namespace

// ---- the store -----------------------------------------------------------------------------------

namespace {
template <typename Vec>
auto findEntry(Vec& v, std::string_view key) {
    return std::find_if(v.begin(), v.end(), [key](const auto& p) { return p.first == key; });
}
} // namespace

float EffectValueStore::getFloat(std::string_view key, float fallback) const {
    const auto it = findEntry(floats, key);
    return it == floats.end() ? fallback : it->second;
}
void EffectValueStore::setFloat(std::string_view key, float value) {
    const auto it = findEntry(floats, key);
    if (it == floats.end()) {
        floats.emplace_back(std::string(key), value);
        std::sort(floats.begin(), floats.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return;
    }
    it->second = value;
}
glm::vec3 EffectValueStore::getColor(std::string_view key, glm::vec3 fallback) const {
    const auto it = findEntry(colors, key);
    return it == colors.end() ? fallback : it->second;
}
void EffectValueStore::setColor(std::string_view key, glm::vec3 value) {
    const auto it = findEntry(colors, key);
    if (it == colors.end()) {
        colors.emplace_back(std::string(key), value);
        std::sort(colors.begin(), colors.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return;
    }
    it->second = value;
}
bool EffectValueStore::getBool(std::string_view key, bool fallback) const {
    const auto it = findEntry(flags, key);
    return it == flags.end() ? fallback : it->second != 0;
}
void EffectValueStore::setBool(std::string_view key, bool value) {
    const auto it = findEntry(flags, key);
    if (it == flags.end()) {
        flags.emplace_back(std::string(key), static_cast<std::uint8_t>(value ? 1 : 0));
        std::sort(flags.begin(), flags.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return;
    }
    it->second = static_cast<std::uint8_t>(value ? 1 : 0);
}

// ---- lookup ----------------------------------------------------------------------------------------

std::span<const EffectSchema* const> effectSchemas() { return builtinSchemas(); }

const char* renderStageName(RenderStage s) {
    switch (s) {
    case RenderStage::Geometry: return "geometry";
    case RenderStage::Material: return "material";
    case RenderStage::Lighting: return "lighting";
    case RenderStage::Sky: return "sky";
    case RenderStage::Volumetric: return "volumetric";
    case RenderStage::Particles: return "particles";
    case RenderStage::ScreenSpace: return "screenSpace";
    case RenderStage::PostProcess: return "postProcess";
    }
    return "sky";
}

const char* performanceClassName(PerformanceClass c) {
    switch (c) {
    case PerformanceClass::Unset: return "unset";
    case PerformanceClass::VeryLow: return "very low";
    case PerformanceClass::Low: return "low";
    case PerformanceClass::Medium: return "medium";
    case PerformanceClass::High: return "high";
    case PerformanceClass::VeryHigh: return "very high";
    }
    return "unset";
}

const char* effectCategoryName(EffectCategory c) {
    switch (c) {
    case EffectCategory::Atmosphere: return "Atmosphere";
    case EffectCategory::Sky: return "Sky";
    case EffectCategory::Lighting: return "Lighting";
    case EffectCategory::Distortion: return "Distortion";
    case EffectCategory::Motion: return "Motion";
    case EffectCategory::Particles: return "Particles";
    }
    return "Atmosphere";
}

const EffectSchema* effectSchema(EffectKind kind) {
    for (const EffectSchema* s : builtinSchemas()) {
        if (s->kind == kind) {
            return s;
        }
    }
    return nullptr;
}

const EffectSchema* effectSchema(std::string_view key) {
    for (const EffectSchema* s : builtinSchemas()) {
        if (key == s->key) {
            return s;
        }
    }
    return nullptr;
}

const EffectSchema* effectSchema(const EffectInstance& effect) { return effectSchema(effect.kind); }

// ---- one row ---------------------------------------------------------------------------------------

std::string storeKey(const EffectSchema& schema, const EffectField& field) {
    std::string key = schema.key;
    key.push_back('/');
    key.append(field.leaf);
    return key;
}

namespace {
// ADR-703. The store key of a stored row, built into a reused per-thread buffer. `storeKey`
// returns a fresh string, and the accessors below run for every stored row of every instance on
// every frame (`applyEffectParameters`) -- which made each fog bank allocate a dozen strings a frame.
// The buffer keeps its capacity, so after the first frame this allocates nothing; the store only
// allocates when a key is inserted for the first time.
const std::string& scratchKey(const EffectSchema& schema, const EffectField& field) {
    thread_local std::string key;
    key.assign(schema.key);
    key.push_back('/');
    key.append(field.leaf);
    return key;
}
} // namespace

float fieldFloat(const EffectField& field, const EffectSchema& schema, const EffectInstance& effect) {
    if (field.stored) {
        return effect.values.getFloat(scratchKey(schema, field), field.storedDefault);
    }
    return field.getFloat != nullptr ? field.getFloat(effect) : 0.0f;
}

void setFieldFloat(const EffectField& field, const EffectSchema& schema, EffectInstance& effect, float v) {
    if (field.stored) {
        effect.values.setFloat(scratchKey(schema, field), v);
        return;
    }
    if (field.setFloat != nullptr) {
        field.setFloat(effect, v);
    }
}

glm::vec3 fieldColor(const EffectField& field, const EffectSchema& schema, const EffectInstance& effect) {
    if (field.stored) {
        return effect.values.getColor(scratchKey(schema, field), field.storedColor);
    }
    return field.getColor != nullptr ? field.getColor(effect) : glm::vec3(0.0f);
}

void setFieldColor(const EffectField& field, const EffectSchema& schema, EffectInstance& effect,
                   glm::vec3 v) {
    if (field.stored) {
        effect.values.setColor(scratchKey(schema, field), v);
        return;
    }
    if (field.setColor != nullptr) {
        field.setColor(effect, v);
    }
}

bool fieldBool(const EffectField& field, const EffectSchema& schema, const EffectInstance& effect) {
    if (field.stored) {
        return effect.values.getBool(scratchKey(schema, field), field.storedDefault >= 0.5f);
    }
    return field.getBool != nullptr && field.getBool(effect);
}

void setFieldBool(const EffectField& field, const EffectSchema& schema, EffectInstance& effect, bool v) {
    if (field.stored) {
        effect.values.setBool(scratchKey(schema, field), v);
        return;
    }
    if (field.setBool != nullptr) {
        field.setBool(effect, v);
    }
}

// ---- the shared rows -------------------------------------------------------------------------------

namespace {

using E = EffectInstance;

#define GET(expr) +[](const E& e) { return (expr); }
#define SETF(lhs) +[](E& e, float v) { (lhs) = v; }
#define SETC(lhs) +[](E& e, glm::vec3 v) { (lhs) = v; }

// Lifecycle, ground illumination and the field subscription: the same questions whatever the effect
// is, so the same rows. They are registered for EVERY kind, including the kinds whose panel does not
// offer the ground pool -- the set of paths a saved project may already name must not shrink.
//
// Their JSON is written by `EffectInstance::toJson` beside the name and the kind, not through a
// schema block, because they are the effect's rather than the kind's. The `jsonPath` on each row is
// what the reader and writer both use, so the two still cannot disagree.
constexpr EffectField kShared[] = {
    floatField("groundIntensity", "Glow amount", 0.0f, 12.0f, 0.0f, 3.0f, GET(e.ground.intensity),
               SETF(e.ground.intensity)).json("ground/intensity").main(),
    floatField("groundRadius", "Radius", 1.0f, 4000.0f, 20.0f, 800.0f, GET(e.ground.radius),
               SETF(e.ground.radius)).json("ground/radius").fmt("%.0f m").log()
        .sec("Ground illumination").floorAt(1.0f),
    floatField("groundFalloff", "Falloff", 0.05f, 8.0f, 0.5f, 4.0f, GET(e.ground.falloff),
               SETF(e.ground.falloff)).json("ground/falloff").floorAt(0.05f),
    // Timing is float here and double on the effect: ADR-011 says a parameter is float components.
    floatField("delay", "Delay", 0.0f, 300.0f, 0.0f, 20.0f, GET(static_cast<float>(e.timing.delay)),
               +[](E& e, float v) { e.timing.delay = v; }).json("timing/delay").fmt("%.2f s"),
    floatField("lifetime", "Lifetime", 0.0f, 900.0f, 0.0f, 60.0f,
               GET(static_cast<float>(e.timing.lifetime)),
               +[](E& e, float v) { e.timing.lifetime = v; }).json("timing/lifetime").fmt("%.2f s"),
    floatField("fadeIn", "Fade in", 0.0f, 60.0f, 0.0f, 8.0f, GET(static_cast<float>(e.timing.fadeIn)),
               +[](E& e, float v) { e.timing.fadeIn = v; }).json("timing/fadeIn").fmt("%.2f s"),
    floatField("fadeOut", "Fade out", 0.0f, 60.0f, 0.0f, 8.0f, GET(static_cast<float>(e.timing.fadeOut)),
               +[](E& e, float v) { e.timing.fadeOut = v; }).json("timing/fadeOut").fmt("%.2f s"),
    floatField("windowStart", "Window start", 0.0f, 3600.0f, 0.0f, 240.0f,
               GET(static_cast<float>(e.timing.windowStart)),
               +[](E& e, float v) { e.timing.windowStart = v; }).json("timing/windowStart").fmt("%.2f s"),
    floatField("windowSeconds", "Window length", 0.0f, 3600.0f, 0.0f, 120.0f,
               GET(static_cast<float>(e.timing.windowSeconds)),
               +[](E& e, float v) { e.timing.windowSeconds = v; }).json("timing/windowSeconds").fmt("%.2f s"),
    floatField("repeat", "Repeat every", 0.0f, 600.0f, 0.0f, 30.0f,
               GET(static_cast<float>(e.timing.repeatSeconds)),
               +[](E& e, float v) { e.timing.repeatSeconds = v; }).json("timing/repeatSeconds").fmt("%.2f s"),
    // §68. How much of the subscribed field's motion this effect takes. ONE row here gives every
    // kind a registered parameter, a modulation target, a timeline key, a preset member, a save
    // entry and apply and capture in both directions -- and a new kind gets it on the day it is
    // added, without anybody remembering to. 0 is off and is the default, which is what makes every
    // scene written before this existed render unchanged.
    floatField("flowInfluence", "Follows the field", 0.0f, 8.0f, 0.0f, 2.0f, GET(e.flow.influence),
               SETF(e.flow.influence)).json("flow/influence").fmt("%.2f").floorAt(0.0f)
        .tooltip("How much of the subscribed field's motion this effect takes. 0 is off: the\n"
                 "effect moves at its own authored speed and does not know the field exists.\n"
                 "At 1 a gust front crossing the valley reaches this effect when it reaches\n"
                 "that place, and two effects subscribed to one field agree without anybody\n"
                 "typing the same number into both."),
    colorField("groundColor", "Glow colour", GET(e.ground.color), SETC(e.ground.color))
        .json("ground/color").main(),
};

#undef GET
#undef SETF
#undef SETC

} // namespace

std::span<const EffectField> sharedEffectFields() { return kShared; }

bool sharedFieldApplies(const EffectSchema& schema, const EffectField& field) {
    // The sky and medium types have always registered every shared row (ground illumination, the
    // field subscription); every other type registers only the timing rows, because nothing reads
    // a ground glow or a flow influence on a wave, a glow or a trail.
    if (isAtmosphericBucket(schema.resolve.bucket)) {
        return true;
    }
    return std::string_view(field.jsonPath).starts_with("timing/");
}

// ---- sanitise --------------------------------------------------------------------------------------

void sanitiseEffect(EffectInstance& effect) {
    // Every kind's clamps, not only the one this effect is. That is deliberate and it is what the
    // hand-written `sanitise` did: it floored the comet's `travelSeconds` whatever the effect's
    // kind, because the struct carries all the payloads and `validate` is asked of all of them.
    // Doing it per-kind here would let an effect that changed kind carry an unclamped divisor into
    // the kind it came back to.
    const EffectSchema* any = builtinSchemas().front();
    for (const EffectField& field : sharedEffectFields()) {
        if (field.type != FieldType::Float) {
            continue;
        }
        float v = fieldFloat(field, *any, effect);
        v = std::clamp(v, std::max(field.floorValue, -3.0e38f), std::min(field.ceilValue, 3.0e38f));
        setFieldFloat(field, *any, effect, v);
    }
    for (const EffectSchema* schema : builtinSchemas()) {
        for (const EffectField& field : schema->fields) {
            if (field.type != FieldType::Float) {
                continue;
            }
            const bool hasFloor = field.floorValue > -3.0e38f;
            const bool hasCeil = field.ceilValue < 3.0e38f;
            if (!hasFloor && !hasCeil) {
                continue;
            }
            float v = fieldFloat(field, *schema, effect);
            if (hasFloor) {
                v = std::max(v, field.floorValue);
            }
            if (hasCeil) {
                v = std::min(v, field.ceilValue);
            }
            setFieldFloat(field, *schema, effect, v);
        }
    }
}

// ---- JSON ------------------------------------------------------------------------------------------

void effectPayloadToJson(const EffectSchema& schema, const EffectInstance& effect, json& out) {
    if (!out.is_object()) {
        out = json::object();
    }
    // The kind's own block exists even when every row of it is absolute, so a reader can tell "this
    // kind wrote nothing here" from "this file predates the kind".
    if (!out.contains(kParametersKey) || !out.at(kParametersKey).is_object()) {
        out[kParametersKey] = json::object();
    }
    if (schema.anchorJson != nullptr && schema.getAnchor != nullptr) {
        const EffectField anchorRow = [&] {
            EffectField f;
            f.jsonPath = schema.anchorJson;
            return f;
        }();
        std::string base = jsonPathOf(schema, anchorRow);
        base.push_back('/');
        writeAt(out, base + "anchor", json(skyAnchorName(schema.getAnchor(effect))));
        if (schema.getAnchorPosition != nullptr) {
            writeAt(out, base + "anchorPosition", vec3ToJson(schema.getAnchorPosition(effect)));
        }
    }
    for (const EffectField& field : schema.fields) {
        const std::string path = jsonPathOf(schema, field);
        switch (field.type) {
        case FieldType::Float: writeAt(out, path, json(fieldFloat(field, schema, effect))); break;
        case FieldType::Color: writeAt(out, path, vec3ToJson(fieldColor(field, schema, effect))); break;
        case FieldType::Bool: writeAt(out, path, json(fieldBool(field, schema, effect))); break;
        // ADR-566: the NAME, not the index. A scene file outlives an enum's order.
        case FieldType::Choice:
            writeAt(out, path, json(std::string(field.choiceName(fieldFloat(field, schema, effect)))));
            break;
        }
    }
    if (schema.writeExtra != nullptr) {
        schema.writeExtra(effect, out[kParametersKey]);
    }
}

Result<void> effectPayloadFromJson(const EffectSchema& schema, const json& document,
                                   EffectInstance& effect) {
    if (!document.is_object()) {
        return {};
    }
    if (schema.anchorJson != nullptr && schema.setAnchor != nullptr) {
        EffectField anchorRow;
        anchorRow.jsonPath = schema.anchorJson;
        std::string base = jsonPathOf(schema, anchorRow);
        base.push_back('/');
        if (const json* a = readAt(document, base + "anchor"); a != nullptr && a->is_string()) {
            const auto anchor = skyAnchorFromName(a->get<std::string>());
            if (!anchor) {
                return fail("effect '{}': unknown anchor '{}'", effect.name,
                            a->get<std::string>());
            }
            schema.setAnchor(effect, *anchor);
        }
        if (schema.setAnchorPosition != nullptr && schema.getAnchorPosition != nullptr) {
            if (const json* p = readAt(document, base + "anchorPosition"); p != nullptr) {
                schema.setAnchorPosition(effect, vec3FromJson(*p, schema.getAnchorPosition(effect)));
            }
        }
    }
    for (const EffectField& field : schema.fields) {
        const json* v = readAt(document, jsonPathOf(schema, field));
        if (v == nullptr) {
            continue; // absent leaves the default, exactly as the hand-written reader did
        }
        switch (field.type) {
        case FieldType::Float:
            if (v->is_number()) {
                setFieldFloat(field, schema, effect, v->get<float>());
            }
            break;
        case FieldType::Color:
            setFieldColor(field, schema, effect, vec3FromJson(*v, fieldColor(field, schema, effect)));
            break;
        case FieldType::Bool:
            if (v->is_boolean()) {
                setFieldBool(field, schema, effect, v->get<bool>());
            }
            break;
        case FieldType::Choice:
            // An unknown name leaves the default. A file written by a build with one more
            // primitive than this one must not quietly become primitive 0.
            if (v->is_string()) {
                if (const int i = field.choiceIndex(v->get<std::string>()); i >= 0) {
                    setFieldFloat(field, schema, effect, static_cast<float>(i));
                }
            } else if (v->is_number()) {
                setFieldFloat(field, schema, effect, v->get<float>()); // pre-ADR-566 files
            }
            break;
        }
    }
    if (schema.readExtra != nullptr && document.contains(kParametersKey)) {
        if (auto ok = schema.readExtra(effect, document.at(kParametersKey)); !ok) {
            return fail("effect '{}': {}", effect.name, ok.error().message);
        }
    }
    return {};
}

// ---- validation --------------------------------------------------------------------------------------

Result<void> validateEffectFields(const EffectInstance& effect) {
    const EffectSchema* schema = effectSchema(effect.kind);
    if (schema == nullptr) {
        return fail("effect '{}': kind {} has no registered schema", effect.name,
                    static_cast<int>(effect.kind));
    }
    for (const EffectField& field : schema->fields) {
        if (field.type != FieldType::Float) {
            continue;
        }
        const float v = fieldFloat(field, *schema, effect);
        if (!std::isfinite(v)) {
            return fail("effect '{}': {} is not finite", effect.name, field.leaf);
        }
    }
    if (schema->validate != nullptr) {
        if (auto ok = schema->validate(effect); !ok) {
            return ok;
        }
    }
    return {};
}

// ---- the registry's own contract -------------------------------------------------------------------

std::vector<RegistryFinding> checkRegistry() {
    std::vector<RegistryFinding> out;
    const auto say = [&out](std::string subject, std::string rule, std::string detail) {
        out.push_back(RegistryFinding{std::move(subject), std::move(rule), std::move(detail)});
    };

    // Every enumerator has exactly one schema. This is the check that makes the one line in
    // `builtinSchemas` impossible to forget quietly: adding `EffectKind::Whatever` and nothing
    // else fails here, by the name the kind serialises under -- or, when the name is what is
    // missing too, by its enumerator value.
    for (const EffectKind kind : declaredEffectKinds()) {
        std::size_t found = 0;
        for (const EffectSchema* s : builtinSchemas()) {
            found += (s->kind == kind) ? 1u : 0u;
        }
        if (found == 0) {
            say(std::string("kind ") + std::to_string(static_cast<int>(kind)), "schema",
                "this enumerator of EffectKind has no schema in builtinSchemas() -- it cannot "
                "be registered, saved, drawn or resolved");
        } else if (found > 1) {
            say(std::string("kind ") + std::to_string(static_cast<int>(kind)), "schema",
                std::to_string(found) + " schemas claim this enumerator");
        }
    }

    std::unordered_set<std::string> keys;
    for (const EffectSchema* schema : builtinSchemas()) {
        const std::string subject = schema->key[0] != '\0' ? schema->key : "<unnamed>";

        if (schema->key[0] == '\0') {
            say(subject, "key", "this kind has no serialisation key, so it cannot be written to a file");
        } else if (!keys.insert(schema->key).second) {
            say(subject, "key", "two kinds serialise under this key; a scene naming it loads as "
                                "whichever comes first");
        } else {
            const auto back = effectKindFromName(schema->key);
            if (!back.has_value() || *back != schema->kind) {
                say(subject, "key", "the key does not round-trip through effectKindFromName");
            }
        }
        if (schema->displayName[0] == '\0') {
            say(subject, "key", "no display name");
        }
        if (schema->enumName[0] == '\0') {
            say(subject, "key", "no enumName, so the header reader cannot hold this schema against "
                                "the enumerator it claims");
        }
        if (schema->factory == nullptr) {
            say(subject, "factory", "no ready-made effect, so 'Add' and the conformance probe have "
                                    "nothing to make");
        }
        // The surface waves resolve through `resolveWave`, which is the wave technique's own
        // evaluator rather than a per-record fill into a shared bucket; every other bucket needs one.
        if (schema->resolve.fill == nullptr && isAtmosphericBucket(schema->resolve.bucket)) {
            say(subject, "resolve", "no resolve, so an effect of this kind is counted as dropped and "
                                    "drawn by nobody");
        }
        if (!isAtmosphericBucket(schema->resolve.bucket) && schema->resolve.bucket != EffectBucket::Surface &&
            schema->resolve.records == nullptr) {
            say(subject, "resolve", "a bucket with its own builder but no `records` hook, so the "
                                    "conformance probe cannot ask whether it resolves at all");
        }
        // ---- ADR-703: what a type must say about itself ----------------------------------------
        if (schema->description[0] == '\0') {
            say(subject, "description", "no description, so the panel's help and the AI catalogue "
                                        "have nothing to say about it");
        }
        if (schema->performance == PerformanceClass::Unset || schema->primaryCost == 0) {
            say(subject, "performance", "no performance class or primary cost");
        }
        // ---- ADR-702: what a type must declare to be attached to anything ----------------------
        if (schema->targets == 0) {
            say(subject, "targets", "supports no target, so the Add Effect menu offers it nowhere and "
                                    "a file naming it can never load");
        }
        if ((schema->targets & ~static_cast<EffectTargetMask>((1u << kEffectTargetCount) - 1u)) != 0) {
            say(subject, "targets", "names a target bit past the last EffectTarget");
        }
        // The stage is where the renderer evaluates the contribution, and the bucket is the integrator
        // that does it. They are declared separately because a stage is a promise to the renderer and
        // a bucket is an implementation -- and a pair that disagree is a type drawn by a pass that
        // runs at the wrong point in the frame.
        const RenderStage expected = [&] {
            switch (schema->resolve.bucket) {
            case EffectBucket::Comet:
            case EffectBucket::Aurora: return RenderStage::Sky;
            case EffectBucket::Medium: return RenderStage::Volumetric;
            case EffectBucket::Surface: return RenderStage::Material;
            case EffectBucket::EntityLanes: return RenderStage::Material;
            case EffectBucket::Ribbon: return RenderStage::Particles;
            case EffectBucket::Distortion: return RenderStage::ScreenSpace;
            case EffectBucket::Emitter: return RenderStage::Particles;
            case EffectBucket::Transform: return RenderStage::Geometry;
            }
            return RenderStage::Sky;
        }();
        if (schema->stage != expected) {
            say(subject, "stage", std::string("declares render stage '") + renderStageName(schema->stage) +
                                      "' but its bucket is evaluated at '" + renderStageName(expected) + "'");
        }
        if ((schema->getSource == nullptr) != (schema->setSource == nullptr)) {
            say(subject, "endpoint", "declares only one of getSource / setSource");
        }
        if ((schema->getTarget == nullptr) != (schema->setTarget == nullptr) ||
            (schema->getTarget == nullptr) != (schema->hasTarget == nullptr)) {
            say(subject, "endpoint", "declares only some of hasTarget / getTarget / setTarget");
        }
        if (schema->styles.empty()) {
            say(subject, "styles", "no style presets, so the panel's preset combo is empty");
        }
        for (const EffectStyle& style : schema->styles) {
            if (style.apply == nullptr || style.name[0] == '\0') {
                say(subject, "styles", "a style has no name or no body");
            }
        }

        std::unordered_set<std::string> leaves;
        for (const EffectField& field : schema->fields) {
            const std::string where = std::string("'") + field.leaf + "'";
            if (field.leaf[0] == '\0') {
                say(subject, "field", "a row has no leaf, so it registers no parameter path");
                continue;
            }
            if (!leaves.insert(field.leaf).second) {
                say(subject, "field", where + " is declared twice; the second registration is "
                                              "refused and the row it belongs to is dead");
            }
            if (field.label[0] == '\0') {
                say(subject, "field", where + " has no artist-facing label");
            }
            // The compile-time half is the `consteval` factories: a Float row cannot be built
            // without float accessors. This is the half that catches a row built by aggregate
            // initialisation around them.
            if (!field.hasAccessors()) {
                say(subject, "field",
                    where + " has no accessors for its type (and is not store-backed) -- it would "
                            "register a parameter that reads and writes nothing");
            }
            if (field.type != FieldType::Float) {
                continue;
            }
            if (!(field.hardMax >= field.hardMin)) {
                say(subject, "range", where + " has hardMax below hardMin");
                continue;
            }
            if (field.softMin < field.hardMin || field.softMax > field.hardMax) {
                say(subject, "range", where + " soft range escapes its hard range, so the slider "
                                              "clamps silently part-way along");
            }
            if (field.floorValue > field.ceilValue) {
                say(subject, "range", where + " has a sanitise floor above its ceiling");
            }
        }

        // Shared rows are registered for every kind, so a kind declaring one of their leaves would
        // register the same path twice and get whichever registration won.
        for (const EffectField& field : sharedEffectFields()) {
            if (leaves.count(field.leaf) != 0) {
                say(subject, "field", std::string("'") + field.leaf +
                                          "' collides with a shared row every kind registers");
            }
        }

        const auto declares = [&](const char* leaf) {
            if (leaves.count(leaf) != 0) {
                return true;
            }
            for (const EffectField& f : sharedEffectFields()) {
                if (std::string_view(f.leaf) == leaf) {
                    return true;
                }
            }
            return false;
        };

        if (schema->beatLeaf[0] == '\0') {
            say(subject, "beat", "no beatLeaf, so the Beat response slider has nothing to drive");
        } else if (!declares(schema->beatLeaf)) {
            say(subject, "beat", std::string("beatLeaf '") + schema->beatLeaf +
                                     "' is not a leaf this kind declares -- the slider would write a "
                                     "route that binds to nothing");
        }
        if (schema->routes.empty()) {
            say(subject, "default-routes",
                "no default audio routes, so a newly added effect of this kind is silent");
        }
        for (const EffectRoute& r : schema->routes) {
            if (r.leaf[0] == '\0' || r.source[0] == '\0') {
                say(subject, "default-routes", "a route has no source or no target leaf");
            } else if (!declares(r.leaf)) {
                say(subject, "default-routes",
                    std::string("route '") + r.source + "' -> '" + r.leaf +
                        "' names a leaf this kind does not declare");
            }
        }
        if ((schema->anchorSection != nullptr) != (schema->getAnchor != nullptr)) {
            say(subject, "anchor", "the anchor combo and the anchor accessors disagree about whether "
                                   "this kind has one");
        }
    }
    return out;
}

} // namespace avgen::world
