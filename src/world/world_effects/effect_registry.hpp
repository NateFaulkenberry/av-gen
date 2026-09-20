#pragma once

// The world-effect registry (ADR-500).
//
// **What this replaces.** ADR-387 claimed that two `constexpr` field tables bought registration,
// modulation, timeline keys, presets, save, capture, default audio routes, panel presence and
// serialisation with no bespoke code. ADR-392 measured that claim and found it true of exactly
// three directions -- register, apply, capture -- with five more hand-written per-kind lists beside
// them: `toJson`, `fromJson`, `sanitise`, the style presets and `defaultAtmosphericRoutes`, plus the
// panel's row tables. One aurora field appeared at **nine** sites that had to agree by hand, two of
// them bare string literals that neither failed to compile nor threw.
//
// This file collapses all of them into one row. An effect declares, in one file: its name and kind,
// its fields (ranges, defaults, units, artist labels, tooltips, panel page), its JSON round trip,
// its validation clamps, its default audio routes, its style presets, and how it resolves per
// frame. Every shared file iterates this registry instead of switching on an enumerator.
//
// **What a new effect still costs outside its own file, counted honestly.** Four lines, in two
// files, and every one of them is forced by the compiler or named by a failing test:
//
//   1. an enumerator in `AtmosphereKind` (`world/atmospherics.hpp`);
//   2. an entry in `kAtmosphereKinds` (below);
//   3. a declaration in `effect_registry.cpp`, and
//   4. the reference to it in `builtinSchemas()` beside it.
//
// The list in (2) could be derived from the schemas, collapsing two of these, and that is
// deliberately NOT done. If the list of enumerators came from the schemas, then "every enumerator
// has a schema" would be a check asking the schemas about the schemas -- the self-agreeing shape
// ADR-392 spent its length warning about. `kAtmosphereKinds` is a second, independent list, and
// `tests/unit/test_effect_conformance.cpp` holds it against the enum read out of the header. One
// extra line per effect buys a guard that can fail.
//
// Everything else -- and it was about twenty edits across six files that every other agent also
// had open -- is now the one file the effect lives in.
//
// **Where an effect's numbers live.** A kind ported from before this ADR keeps its typed struct on
// `AtmosphericEffect` (`Comet`, `Aurora`, `Vortex`) and its rows carry ordinary member accessors,
// which is what makes the port provably behaviour-neutral: the same expression reads the same
// field. A kind declared after it has nowhere to put a typed struct on a shared header, so its rows
// name a key in `AtmosphericEffect::values` instead and the registry does the reading. Both kinds
// of row are declared the same way and every consumer goes through `fieldFloat` / `setFieldFloat`,
// so nothing downstream knows which it got.
//
// **The failure mode this is shaped against.** ADR-392: a panel row naming a parameter nobody
// registered draws an empty box, which is indistinguishable from "this scene has no such effect".
// Here a panel row *is* the registration -- one row, one parameter, one slider, one JSON key -- so
// the two cannot disagree. What remains checkable is whether a row is complete, and `checkRegistry`
// answers that by name for every kind, in the CPU suite, whatever the warning level.

#include "world/atmospherics.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::params {
class ParameterSet;
struct ModRoute;
} // namespace avgen::params

namespace avgen::world {

// ---- one row ------------------------------------------------------------------------------------

enum class FieldType : std::uint8_t { Float, Color, Bool };

// Which disclosure level the panel draws this row at. `Hidden` is a real parameter that the panel
// does not offer -- there are none today, and the value exists so that "registered but not drawn"
// has to be *said* rather than happening because somebody forgot a row.
enum class FieldPage : std::uint8_t { Main, Advanced, Hidden };

// A row. One of these is a parameter path, a modulation target, a timeline key, a save entry, a
// slider with a label and a tooltip, and a JSON key -- which are the nine sites ADR-392 counted.
//
// Build one with the `floatField` / `colorField` / `boolField` / `storedFloat` ... factories below.
// They are `consteval` and they take the accessors as required arguments, so a float row without
// float accessors is a compile error rather than a row that silently reads nothing.
struct EffectField {
    FieldType type = FieldType::Float;
    // Appended to `atmos/<effect name>/`. Also the JSON key when `jsonPath` is empty.
    const char* leaf = "";
    // What an artist reads. ADR-230's rule: "Cloud Density", not "DensityFieldThreshold".
    const char* label = "";
    // Where this value lives inside the effect's JSON block, slash separated, relative to
    // `j[<schema key>]`. A numeric segment indexes an array, which is how the vortex's three
    // `centerX/Y/Z` parameters share one `"center": [x, y, z]` in the file it already writes.
    // Empty means the flat `j[<schema key>][leaf]`, which is what a kind declared after ADR-500
    // gets for free.
    const char* jsonPath = "";
    const char* format = "";  // slider format; empty is the panel's "%.2f"
    const char* tip = "";     // the row's own tooltip, empty for none
    const char* section = ""; // non-empty draws a SeparatorText before this row
    FieldPage page = FieldPage::Advanced;
    bool logarithmic = false;

    // Floats only. The soft range is what the slider offers; the hard range is what a modulation
    // route is clamped to, and the two differ wherever an authored value may legitimately go past
    // what a slider should reach for.
    float hardMin = 0.0f;
    float hardMax = 1.0f;
    float softMin = 0.0f;
    float softMax = 1.0f;

    // ADR-392's `sanitise`, as a property of the row rather than a separate list. Applied to the
    // *modulated final* before it reaches the resolver, because a route can drive a final anywhere
    // inside the hard range and a few of these are divisors. Defaults are the whole real line, so a
    // row that needs no floor says nothing.
    float floorValue = -3.402823466e+38f;
    float ceilValue = 3.402823466e+38f;

    // Exactly one of these three groups is non-null, or `storeKey` is. `checkRegistry` says which
    // row of which kind is wrong, by name.
    float (*getFloat)(const AtmosphericEffect&) = nullptr;
    void (*setFloat)(AtmosphericEffect&, float) = nullptr;
    glm::vec3 (*getColor)(const AtmosphericEffect&) = nullptr;
    void (*setColor)(AtmosphericEffect&, glm::vec3) = nullptr;
    bool (*getBool)(const AtmosphericEffect&) = nullptr;
    void (*setBool)(AtmosphericEffect&, bool) = nullptr;

    // ...or the row is backed by `AtmosphericEffect::values`, for a kind with no struct of its own.
    // The key stored there is `<schema key>/<leaf>`, so two kinds' `density` do not collide and an
    // effect that changes kind keeps what the kind it left was holding -- the property the comment
    // above `struct AtmosphericEffect` states and the reason it is not a variant.
    bool stored = false;
    float storedDefault = 0.0f;         // Float and Bool (0 or 1)
    glm::vec3 storedColor{0.0f};        // Color

    [[nodiscard]] constexpr bool hasAccessors() const {
        if (stored) {
            return getFloat == nullptr && getColor == nullptr && getBool == nullptr;
        }
        switch (type) {
        case FieldType::Float: return getFloat != nullptr && setFloat != nullptr;
        case FieldType::Color: return getColor != nullptr && setColor != nullptr;
        case FieldType::Bool: return getBool != nullptr && setBool != nullptr;
        }
        return false;
    }

    // Chainable, so a row reads as one expression and the optional parts are named where they are
    // set rather than counted along a positional list.
    [[nodiscard]] consteval EffectField json(const char* p) const { EffectField f = *this; f.jsonPath = p; return f; }
    [[nodiscard]] consteval EffectField fmt(const char* p) const { EffectField f = *this; f.format = p; return f; }
    [[nodiscard]] consteval EffectField tooltip(const char* p) const { EffectField f = *this; f.tip = p; return f; }
    [[nodiscard]] consteval EffectField sec(const char* p) const { EffectField f = *this; f.section = p; return f; }
    [[nodiscard]] consteval EffectField main() const { EffectField f = *this; f.page = FieldPage::Main; return f; }
    [[nodiscard]] consteval EffectField advanced() const { EffectField f = *this; f.page = FieldPage::Advanced; return f; }
    [[nodiscard]] consteval EffectField hidden() const { EffectField f = *this; f.page = FieldPage::Hidden; return f; }
    [[nodiscard]] consteval EffectField log() const { EffectField f = *this; f.logarithmic = true; return f; }
    [[nodiscard]] consteval EffectField floorAt(float v) const { EffectField f = *this; f.floorValue = v; return f; }
    [[nodiscard]] consteval EffectField ceilAt(float v) const { EffectField f = *this; f.ceilValue = v; return f; }
    [[nodiscard]] consteval EffectField clampTo(float lo, float hi) const {
        EffectField f = *this;
        f.floorValue = lo;
        f.ceilValue = hi;
        return f;
    }
};

// The factories. `consteval`, so a malformed row cannot reach a runtime table; and the accessors
// are required positional arguments, which is the compile-time half of "a forgotten piece fails
// loudly": there is no way to spell a Float row that reads nothing.
[[nodiscard]] consteval EffectField floatField(const char* leaf, const char* label, float lo, float hi,
                                               float slo, float shi,
                                               float (*get)(const AtmosphericEffect&),
                                               void (*set)(AtmosphericEffect&, float)) {
    EffectField f;
    f.type = FieldType::Float;
    f.leaf = leaf;
    f.label = label;
    f.hardMin = lo;
    f.hardMax = hi;
    f.softMin = slo;
    f.softMax = shi;
    f.getFloat = get;
    f.setFloat = set;
    return f;
}

[[nodiscard]] consteval EffectField colorField(const char* leaf, const char* label,
                                               glm::vec3 (*get)(const AtmosphericEffect&),
                                               void (*set)(AtmosphericEffect&, glm::vec3)) {
    EffectField f;
    f.type = FieldType::Color;
    f.leaf = leaf;
    f.label = label;
    f.getColor = get;
    f.setColor = set;
    return f;
}

[[nodiscard]] consteval EffectField boolField(const char* leaf, const char* label,
                                              bool (*get)(const AtmosphericEffect&),
                                              void (*set)(AtmosphericEffect&, bool)) {
    EffectField f;
    f.type = FieldType::Bool;
    f.leaf = leaf;
    f.label = label;
    f.getBool = get;
    f.setBool = set;
    return f;
}

// The same row, backed by `AtmosphericEffect::values` instead of a struct member. What a kind
// declared in its own file uses, because a shared header is exactly the thing this ADR is removing
// edits from.
[[nodiscard]] consteval EffectField storedFloat(const char* leaf, const char* label, float def, float lo,
                                                float hi, float slo, float shi) {
    EffectField f;
    f.type = FieldType::Float;
    f.leaf = leaf;
    f.label = label;
    f.hardMin = lo;
    f.hardMax = hi;
    f.softMin = slo;
    f.softMax = shi;
    f.stored = true;
    f.storedDefault = def;
    return f;
}

[[nodiscard]] consteval EffectField storedColor(const char* leaf, const char* label, glm::vec3 def) {
    EffectField f;
    f.type = FieldType::Color;
    f.leaf = leaf;
    f.label = label;
    f.stored = true;
    f.storedColor = def;
    return f;
}

[[nodiscard]] consteval EffectField storedBool(const char* leaf, const char* label, bool def) {
    EffectField f;
    f.type = FieldType::Bool;
    f.leaf = leaf;
    f.label = label;
    f.stored = true;
    f.storedDefault = def ? 1.0f : 0.0f;
    return f;
}

// ---- the rest of a declaration -------------------------------------------------------------------

// A style configures the underlying parameters and then gets out of the way; nothing reads the name
// at runtime (ADR-230 §10).
struct EffectStyle {
    const char* name = "";
    void (*apply)(AtmosphericEffect&) = nullptr;
};

// One default audio route. `leaf` rather than a full path, so the target cannot be written outside
// the effect's own prefix -- which is one of the three things ADR-392's route check asks, now true
// by construction rather than by checking.
struct EffectRoute {
    const char* source = "";
    const char* leaf = "";
    float amount = 0.0f;
    float attackMs = 0.0f;
    float decayMs = 0.0f;
};

// Which GPU payload this kind writes into. A kind reaches the picture through one of the three
// integrators the engine already has; a kind that needs a *new* integrator needs shader work, which
// is the one part of an effect this registry does not and cannot move into a single C++ file.
enum class EffectBucket : std::uint8_t {
    Comet,  // the view-ray trail integrator in shaders/atmosphere_fx.wgsl
    Aurora, // the vertical-shell curtains in the same shader
    Vortex, // the placed volumetric medium in shaders/volume.wgsl
};

// How one authored effect becomes records in that bucket.
//
// `count` exists for the shower case: one authored Meteor Shower is N trails, each with its own arc
// and envelope, and writing N records into the comet bucket is how it reaches the GPU without a
// line of new shader. `fill` is handed `base`, which already carries the envelope, the elapsed
// second and the effect pointer that every kind shares, and returns false to skip a record (a
// degenerate arc, a stagger that has not started).
struct EffectResolve {
    EffectBucket bucket = EffectBucket::Comet;
    std::size_t (*count)(const AtmosphericEffect&) = nullptr; // null means exactly one
    bool (*fill)(const AtmosphericEffect&, std::size_t index, const AtmosphericContext&,
                 const ResolvedAtmospheric& base, ResolvedAtmospheric& out) = nullptr;
};

// The declaration. One of these per kind, in that kind's own file.
struct EffectSchema {
    AtmosphereKind kind = AtmosphereKind::Comet;
    // The serialised name. `atmosphereKindName` / `atmosphereKindFromName` are derived from this,
    // so the round trip ADR-392's check 5 asks about cannot be an if-chain that fell behind.
    const char* key = "";
    // The enumerator's own spelling -- "MeteorShower" for `AtmosphereKind::MeteorShower`.
    //
    // It is here because it is NOT the same string as `key`, and finding that out cost a test
    // failure: ADR-392's enum reader compared the lowercased enumerator with `atmosphereKindName`,
    // which worked for three kinds because "Comet" lowercases to "comet" by coincidence. A kind
    // whose file format says "meteors" broke it. Declaring the mapping means the reader can hold
    // the header against the registry **by name** in both directions -- an enumerator with no
    // schema, and a schema naming an enumerator that no longer exists -- which is the guard, and
    // the coincidence was never it.
    const char* enumName = "";
    const char* displayName = ""; // "Comet"
    const char* addLabel = "";    // "Add comet"
    const char* addTip = "";      // what the button's tooltip says
    // What appears in the panel under the collapsing header, above "Advanced". Rows, in order.
    std::span<const EffectField> fields;
    std::span<const EffectStyle> styles;
    std::span<const EffectRoute> routes;
    // The leaf the "Beat response" slider drives. Must be one of `fields`; `checkRegistry` says so
    // by name when it is not.
    const char* beatLeaf = "";
    // Does this kind light the ground below (ADR-230 §6)? A vortex does not: its light on the world
    // is `spill`, and a combo that changed nothing would be worse than no combo.
    bool groundGlow = false;
    // The two anchor combos. Non-null means the panel draws "Anchored to" under `anchorSection` and
    // the serialiser writes `anchor` and `anchorPosition` at `anchorJson`.
    const char* anchorSection = nullptr;
    const char* anchorJson = nullptr; // e.g. "path" -> j["comet"]["path"]["anchor"]
    SkyAnchor (*getAnchor)(const AtmosphericEffect&) = nullptr;
    void (*setAnchor)(AtmosphericEffect&, SkyAnchor) = nullptr;
    glm::vec3 (*getAnchorPosition)(const AtmosphericEffect&) = nullptr;
    void (*setAnchorPosition)(AtmosphericEffect&, glm::vec3) = nullptr;
    // The ready-made effect the "Add" button makes and the conformance probe uses -- so a probe is
    // something an artist can actually make rather than a default-constructed struct no scene holds.
    AtmosphericEffect (*factory)(std::string name) = nullptr;
    EffectResolve resolve;
    // Anything in this kind's JSON block that is not a parameter. There are two in the whole
    // family -- the comet's `sparkle.fadeDistance` and `sparkle.seed` -- and they live here rather
    // than as rows because a seed is a `uint32` and a fade distance is not something to automate.
    // In the kind's own file, which is the point.
    void (*writeExtra)(const AtmosphericEffect&, nlohmann::json&) = nullptr;
    void (*readExtra)(AtmosphericEffect&, const nlohmann::json&) = nullptr;
    // Kind-specific refusals beyond "every value is finite and inside its hard range", which the
    // registry checks for every kind without being told.
    Result<void> (*validate)(const AtmosphericEffect&) = nullptr;
};

// ---- the enum, closed ------------------------------------------------------------------------------

// Every enumerator of `AtmosphereKind`, in declaration order.
//
// ADR-392's construction, kept and moved here so there is one such list rather than two. It sits
// beside an exhaustive `switch` with no `default`, so a new enumerator is at minimum a `-Wswitch`
// diagnostic -- and because `AVGEN_WARNINGS_AS_ERRORS` is OFF in this build and a warning in a
// five-thousand-line log is not a guard, `tests/unit/test_effect_registry.cpp` reads
// `enum class AtmosphereKind` out of `atmospherics.hpp` and requires this array to cover exactly
// the enumerators declared there, failing **by the name of the missing one**.
inline constexpr std::array<AtmosphereKind, 5> kAtmosphereKinds{
    AtmosphereKind::Comet,         //
    AtmosphereKind::Aurora,        //
    AtmosphereKind::Vortex,        //
    AtmosphereKind::MeteorShower,  //
    AtmosphereKind::VolumetricFog, //
};

// The kind's position in `kAtmosphereKinds`, or `size()` for an enumerator that is not in it --
// which is the loud case and which `checkRegistry` and the header-reading test both name.
//
// A search rather than an exhaustive `switch`, and that is a deliberate downgrade of the weaker of
// ADR-392's two guards. The switch made a new enumerator a `-Wswitch` diagnostic, but
// `AVGEN_WARNINGS_AS_ERRORS` is OFF (`CMakeLists.txt:33`) and ADR-392 says plainly that a warning
// in a five-thousand-line log is not a guard. What it cost was a second line per effect in a shared
// file, on top of the array entry directly above -- and the guard that actually fires, the test
// that reads the enum out of the header and names the kind, does not need it. The exhaustive
// switch that DOES still matter is over `EffectBucket` in `resolveAtmosphericEffects`, where the
// arms are per GPU payload and adding one is a real decision.
[[nodiscard]] constexpr std::size_t atmosphereKindIndex(AtmosphereKind k) {
    for (std::size_t i = 0; i < kAtmosphereKinds.size(); ++i) {
        if (kAtmosphereKinds[i] == k) {
            return i;
        }
    }
    return kAtmosphereKinds.size();
}

static_assert(atmosphereKindIndex(AtmosphereKind::Comet) == 0);
static_assert(atmosphereKindIndex(AtmosphereKind::VolumetricFog) == 4);

[[nodiscard]] inline std::span<const AtmosphereKind> declaredAtmosphereKinds() { return kAtmosphereKinds; }

// The rows every kind registers whatever it is: lifecycle, ground illumination and the field
// subscription. ADR-387's point, and the reason it is worth keeping: ONE row here gives every kind
// a registered parameter, a modulation target, a timeline key, a preset member, a save entry and
// apply and capture in both directions -- and a new kind gets it on the day it is added, without
// anybody remembering to.
//
// They are registered for every kind including the ones whose panel does not offer them, because
// the set of paths a project may already name must not shrink.
[[nodiscard]] std::span<const EffectField> sharedEffectFields();

// ---- the registry --------------------------------------------------------------------------------

// Every declared kind, in enumerator order.
[[nodiscard]] std::span<const EffectSchema* const> effectSchemas();
// The schema for a kind, or null -- which is the loud case: `checkRegistry` reports it and
// `resolveAtmosphericEffects` counts the effect as dropped rather than drawing it as a neighbour.
[[nodiscard]] const EffectSchema* effectSchema(AtmosphereKind kind);
[[nodiscard]] const EffectSchema* effectSchema(std::string_view key);
// The schema an effect is of. Null for a kind with no declaration.
[[nodiscard]] const EffectSchema* effectSchema(const AtmosphericEffect& effect);

// ---- reading and writing one row -----------------------------------------------------------------
//
// Every consumer goes through these, so a struct-backed row and a store-backed row are the same
// thing downstream.
[[nodiscard]] float fieldFloat(const EffectField& field, const EffectSchema& schema,
                               const AtmosphericEffect& effect);
void setFieldFloat(const EffectField& field, const EffectSchema& schema, AtmosphericEffect& effect, float v);
[[nodiscard]] glm::vec3 fieldColor(const EffectField& field, const EffectSchema& schema,
                                   const AtmosphericEffect& effect);
void setFieldColor(const EffectField& field, const EffectSchema& schema, AtmosphericEffect& effect,
                   glm::vec3 v);
[[nodiscard]] bool fieldBool(const EffectField& field, const EffectSchema& schema,
                             const AtmosphericEffect& effect);
void setFieldBool(const EffectField& field, const EffectSchema& schema, AtmosphericEffect& effect, bool v);

// The key a stored row occupies in `AtmosphericEffect::values`.
[[nodiscard]] std::string storeKey(const EffectSchema& schema, const EffectField& field);

// ---- the derived directions ----------------------------------------------------------------------
//
// Each of these was a hand-written per-kind list before ADR-500 and is now one loop over `fields`.

// ADR-392's `sanitise`: the row's floor and ceiling, applied to a modulated final.
void sanitiseEffect(AtmosphericEffect& effect);

// The kind's payload block, in the exact shape the file format already has.
void effectPayloadToJson(const EffectSchema& schema, const AtmosphericEffect& effect, nlohmann::json& out);
// Returns a message on a value the file cannot mean (an unknown anchor name), the way `fromJson`
// already refuses one. An absent key leaves the default, which is what every file written before a
// row existed describes.
[[nodiscard]] Result<void> effectPayloadFromJson(const EffectSchema& schema, const nlohmann::json& block,
                                                 AtmosphericEffect& effect);

// Every value finite and inside its hard range, plus the kind's own `validate`.
[[nodiscard]] Result<void> validateEffectFields(const AtmosphericEffect& effect);

// ---- the registry's own contract -----------------------------------------------------------------

// One thing about the registry itself that does not line up, named.
struct RegistryFinding {
    std::string subject; // the kind
    std::string rule;
    std::string detail;
};

// Walks every schema and reports, **by name**:
//   * an enumerator with no schema, or two schemas claiming one enumerator;
//   * a duplicate serialisation key, or one that does not round-trip through the kind names;
//   * a row with no accessors and no store key, or with the accessors of a different type;
//   * a duplicate leaf within a kind, or a leaf that collides with a shared row;
//   * a float row whose default lies outside its hard range or whose soft range escapes it;
//   * a default route, or a `beatLeaf`, naming a leaf the kind does not declare;
//   * a kind with no factory, no resolve, or no styles.
//
// This is the replacement for ADR-392's "a forgotten piece is silent". It runs in the CPU suite,
// needs no GPU, and fails by the name of the kind and of the row.
[[nodiscard]] std::vector<RegistryFinding> checkRegistry();

} // namespace avgen::world
