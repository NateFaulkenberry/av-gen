#pragma once

// The effect type registry (ADR-500, ADR-702).
//
// ADR-702: this is now the registry of EVERY effect type, not of the "world" family. ADR-500 built it
// for the sky and medium kinds; ADR-702 ported ADR-207's surface waves onto it (Ground Pulse and
// Travel Beam, bucket `Surface`) and gave each schema the three things a type needs in order to be
// attached to more than the scene: the TARGETS it supports (World, Entity, Camera, Light), the
// CATEGORY the Add Effect menu files it under, and the RENDER STAGE (and priority within it) its
// contribution is evaluated at. The Effects panel, the Add Effect menu, the parameter registrar, the
// serialiser and the evaluator all read these rows; none of them names a type.
//
// **What this replaces.** ADR-387 claimed that two `constexpr` field tables bought registration,
// modulation, timeline keys, presets, save, capture, default audio routes, panel presence and
// serialisation with no bespoke code. ADR-392 measured that claim and found it true of exactly
// three directions -- register, apply, capture -- with five more hand-written per-kind lists beside
// them: `toJson`, `fromJson`, `sanitise`, the style presets and `defaultEffectRoutes`, plus the
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
//   1. an enumerator in `EffectKind` (`world/effects/effect_kind.hpp`);
//   2. an entry in `kEffectKinds` (below);
//   3. a declaration in `effect_registry.cpp`, and
//   4. the reference to it in `builtinSchemas()` beside it.
//
// The list in (2) could be derived from the schemas, collapsing two of these, and that is
// deliberately NOT done. If the list of enumerators came from the schemas, then "every enumerator
// has a schema" would be a check asking the schemas about the schemas -- the self-agreeing shape
// ADR-392 spent its length warning about. `kEffectKinds` is a second, independent list, and
// `tests/unit/test_effect_conformance.cpp` holds it against the enum read out of the header. One
// extra line per effect buys a guard that can fail.
//
// Everything else -- and it was about twenty edits across six files that every other agent also
// had open -- is now the one file the effect lives in.
//
// ---- adding an effect, start to finish -----------------------------------------------------------
//
// 1. Write `src/world/effects/kinds/<kind>_effect.cpp`. Copy the smallest existing one --
//    `volumetric_fog_effect.cpp` -- and replace its contents. It needs:
//
//      constexpr EffectField kFields[] = { ... };   // the rows, in panel order, `.main()` first
//      constexpr EffectStyle kStyles[] = { ... };   // at least one preset
//      constexpr EffectRoute kRoutes[] = { ... };   // at least one default audio route
//      EffectInstance make(std::string name);    // what the "Add" button produces
//      bool fill(...);                              // one record's worth of resolve
//      EffectSchema buildSchema();                  // the five above, plus key/enumName/beatLeaf
//      const EffectSchema& <kind>Schema();          // a function-local static over buildSchema()
//
// 2. Add the enumerator to `EffectKind` in `world/effects/effect_kind.hpp`.
// 3. Add it to `kEffectKinds` below.
// 4. Declare `<kind>Schema()` in `effect_registry.cpp` and reference it in `builtinSchemas()`.
//
// Then run `avgen_tests "[registry],[conformance]"`. Anything you have missed is a named failure:
// a leaf your default route aims at and does not declare, a default outside its hard range, a
// `beatLeaf` that is not yours, a kind that resolves into somebody else's bucket, a value that does
// not survive the file. You do not need to touch `atmospherics.cpp`, `effect_params.cpp`, the
// Effects panel, `ui_logic.hpp` or `effect_conformance.cpp` -- and if you find yourself
// wanting to, that is a gap in this registry worth reporting rather than working around.
//
// **What is NOT free.** `EffectResolve::bucket` says which of the three integrators the engine
// already has your kind reaches the picture through: the view-ray trail and the vertical-shell
// curtains in `shaders/atmosphere_fx.wgsl`, and the placed volumetric medium in
// `shaders/volume.wgsl`. A look one of those three can produce is one file. A look that needs a
// NEW integrator needs WGSL, a GPU struct and a renderer change, and none of that is something a
// registry can move into a `.cpp`.
//
// **Where an effect's numbers live.** A kind ported from before this ADR keeps its typed struct on
// `EffectInstance` (`Comet`, `Aurora`, `Vortex`) and its rows carry ordinary member accessors,
// which is what makes the port provably behaviour-neutral: the same expression reads the same
// field. A kind declared after it has nowhere to put a typed struct on a shared header, so its rows
// name a key in `EffectInstance::values` instead and the registry does the reading. Both kinds
// of row are declared the same way and every consumer goes through `fieldFloat` / `setFieldFloat`,
// so nothing downstream knows which it got.
//
// **The failure mode this is shaped against.** ADR-392: a panel row naming a parameter nobody
// registered draws an empty box, which is indistinguishable from "this scene has no such effect".
// Here a panel row *is* the registration -- one row, one parameter, one slider, one JSON key -- so
// the two cannot disagree. What remains checkable is whether a row is complete, and `checkRegistry`
// answers that by name for every kind, in the CPU suite, whatever the warning level.

#include "world/effects/effect_instance.hpp"

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

struct EntityLaneContribution; // world/effects/entity_fx.hpp (ADR-703, FXL)

// ---- one row ------------------------------------------------------------------------------------

// ADR-566: `Choice` is a row whose value is one of a short named list -- the fog bank's five
// volume primitives are the first, and every future "which of these" control is the reason it is a
// registry type rather than a float slider labelled 0..5.
//
// It is carried as a FLOAT INDEX everywhere a number is what the machinery wants -- `values`, a
// project parameter (ADR-264), a modulation route's clamp -- because making it a second scalar
// representation would double every conversion in this file. It is written to JSON as its NAME,
// because a scene file outlives the order of an enum: appending a primitive must not silently
// change what every saved bank is, and an index in a file makes that exact mistake available.
enum class FieldType : std::uint8_t { Float, Color, Bool, Choice };

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
    float (*getFloat)(const EffectInstance&) = nullptr;
    void (*setFloat)(EffectInstance&, float) = nullptr;
    glm::vec3 (*getColor)(const EffectInstance&) = nullptr;
    void (*setColor)(EffectInstance&, glm::vec3) = nullptr;
    bool (*getBool)(const EffectInstance&) = nullptr;
    void (*setBool)(EffectInstance&, bool) = nullptr;

    // ...or the row is backed by `EffectInstance::values`, for a kind with no struct of its own.
    // The key stored there is `<schema key>/<leaf>`, so two kinds' `density` do not collide and an
    // effect that changes kind keeps what the kind it left was holding -- the property the comment
    // above `struct EffectInstance` states and the reason it is not a variant.
    bool stored = false;
    float storedDefault = 0.0f;         // Float, Bool (0 or 1) and Choice (the index)
    glm::vec3 storedColor{0.0f};        // Color

    // Choice only. Points at a static array the declaring file owns; `choiceCount` is its length.
    // The hard range is 0 .. count-1, so a modulation route or a project parameter cannot select a
    // primitive that does not exist.
    const char* const* choices = nullptr;
    int choiceCount = 0;

    [[nodiscard]] constexpr const char* choiceName(float v) const {
        if (choices == nullptr || choiceCount <= 0) {
            return "";
        }
        int i = static_cast<int>(v + 0.5f);
        if (i < 0) { i = 0; }
        if (i >= choiceCount) { i = choiceCount - 1; }
        return choices[i];
    }
    // -1 when the name is not one of this row's choices, which the readers treat as "leave the
    // value alone" rather than as "index 0": a scene written by a newer build names a primitive
    // this one does not have, and silently becoming a Bank is worse than staying whatever the
    // default is and saying nothing.
    [[nodiscard]] constexpr int choiceIndex(std::string_view name) const {
        for (int i = 0; i < choiceCount; ++i) {
            if (name == std::string_view(choices[i])) {
                return i;
            }
        }
        return -1;
    }

    [[nodiscard]] constexpr bool hasAccessors() const {
        if (stored) {
            return getFloat == nullptr && getColor == nullptr && getBool == nullptr;
        }
        switch (type) {
        case FieldType::Float: return getFloat != nullptr && setFloat != nullptr;
        case FieldType::Color: return getColor != nullptr && setColor != nullptr;
        case FieldType::Bool: return getBool != nullptr && setBool != nullptr;
        // ADR-702: a Choice may be a typed member too -- a wave's propagation kind is an enum on
        // `WaveEffect` -- read and written through the float accessors as its index.
        case FieldType::Choice: return getFloat != nullptr && setFloat != nullptr && choiceCount > 0;
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
                                               float (*get)(const EffectInstance&),
                                               void (*set)(EffectInstance&, float)) {
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
                                               glm::vec3 (*get)(const EffectInstance&),
                                               void (*set)(EffectInstance&, glm::vec3)) {
    EffectField f;
    f.type = FieldType::Color;
    f.leaf = leaf;
    f.label = label;
    f.getColor = get;
    f.setColor = set;
    return f;
}

[[nodiscard]] consteval EffectField boolField(const char* leaf, const char* label,
                                              bool (*get)(const EffectInstance&),
                                              void (*set)(EffectInstance&, bool)) {
    EffectField f;
    f.type = FieldType::Bool;
    f.leaf = leaf;
    f.label = label;
    f.getBool = get;
    f.setBool = set;
    return f;
}

// The same row, backed by `EffectInstance::values` instead of a struct member. What a kind
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

// A named list, backed by `values` like every other stored row. `N` comes from the array, so the
// count cannot disagree with the list -- the class of defect ADR-563's lane budget was.
template <std::size_t N>
[[nodiscard]] consteval EffectField storedChoice(const char* leaf, const char* label, int def,
                                                 const char* const (&names)[N]) {
    EffectField f;
    f.type = FieldType::Choice;
    f.leaf = leaf;
    f.label = label;
    f.hardMin = 0.0f;
    f.hardMax = static_cast<float>(N - 1);
    f.softMin = 0.0f;
    f.softMax = static_cast<float>(N - 1);
    f.stored = true;
    f.storedDefault = static_cast<float>(def);
    f.choices = names;
    f.choiceCount = static_cast<int>(N);
    return f;
}

// ADR-702. A Choice held as a typed enum member rather than in the value store: the index is read and
// written through the float accessors. Registered as an int parameter like `storedChoice`, and
// written to JSON by NAME like it, so a file outlives the enum's order.
template <std::size_t N>
[[nodiscard]] consteval EffectField choiceField(const char* leaf, const char* label,
                                                const char* const (&names)[N],
                                                float (*get)(const EffectInstance&),
                                                void (*set)(EffectInstance&, float)) {
    EffectField f;
    f.type = FieldType::Choice;
    f.leaf = leaf;
    f.label = label;
    f.hardMin = 0.0f;
    f.hardMax = static_cast<float>(N - 1);
    f.softMin = 0.0f;
    f.softMax = static_cast<float>(N - 1);
    f.choices = names;
    f.choiceCount = static_cast<int>(N);
    f.getFloat = get;
    f.setFloat = set;
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
    void (*apply)(EffectInstance&) = nullptr;
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
    // ADR-562: a PLACED VOLUMETRIC MEDIUM in shaders/volume.wgsl -- a cosmic vortex, a fog bank, a
    // tornado. Named `Vortex` until the bucket held more than one kind, which made the name a lie
    // about what it carried: a fog bank has never been a vortex, it shared the funnel's field.
    Medium,
    // ADR-702: ADR-207's per-fragment wave term in the surface shader (shaders/wave_effects.wgsl),
    // evaluated in the lit pass on every opaque surface. A Ground Pulse or a Travel Beam.
    Surface,
    // ---- ADR-703: the Effect Library's Wave 1 integrators ------------------------------------
    // Each is built by its own builder into its own frame block, as `Surface` is; none of them is
    // resolved by `resolveAtmosphericEffects`.
    EntityLanes, // per-entity material lanes in the lit pass (FXL): Glow, Pulse, Bloom Source
    Ribbon,      // camera-facing strips drawn in pass 1's blended section (RIBBON): Trail
    Distortion,  // screen-space offsets resolved against a scene-colour copy (DF): Space Warp
    Emitter,     // an effect-owned particle system (EMIT): Particle Emitter
    // ---- Wave 2 ---------------------------------------------------------------------------------
    // A per-owner render-transform offset (XFORM, world/effects/transform_frame.hpp), built BEFORE
    // the flatten and composed into the owner node's transform by the Composition: Orbit, Spiral,
    // Float, Shake, Bounce.
    Transform,
    // The sky's star field (world/effects/star_field.hpp): one per frame, drawn by the background
    // pass in place of its fixed stars. Stars.
    Starfield,
};

// The three buckets `resolveAtmosphericEffects` owns. Everything else has a builder of its own.
[[nodiscard]] constexpr bool isAtmosphericBucket(EffectBucket b) {
    return b == EffectBucket::Comet || b == EffectBucket::Aurora || b == EffectBucket::Medium;
}

// ADR-702 §9. WHERE in the frame a type's contribution is evaluated. This is declared per type
// rather than inferred from list order, because list order is an author's stacking decision and a
// frame has an order of its own: a surface term must be in the lit pass, a sky term drawn at the far
// plane after the opaque geometry, a medium marched after both. The renderer executes stages in
// this order; within a stage, `EffectSchema::priority` and then the effect's stack position decide
// which instance gets a GPU slot first, which is the only thing order can change inside an additive
// stage. Values are ordered; do not reorder them.
enum class RenderStage : std::uint8_t {
    Geometry,    // changes what geometry exists or where it is -- XFORM (Orbit, Float, ...), before the flatten
    Material,    // a per-fragment term on lit surfaces -- the surface waves
    Lighting,    // adds or modulates lights (none yet: a Flicker would)
    Sky,         // drawn at the far plane after the opaque pass -- comet, aurora, meteor shower
    Volumetric,  // a placed medium in the volumetric march -- fog bank, tornado, vortex
    Particles,   // emits particles (none yet)
    ScreenSpace, // a pass over the finished lit image (none yet)
    PostProcess, // the post chain (none yet: Chromatic Aberration, Film Grain would)
};
inline constexpr std::size_t kRenderStageCount = 8;
[[nodiscard]] const char* renderStageName(RenderStage s);

// Where the Add Effect menu files a type. Presentation only; nothing at runtime reads it.
enum class EffectCategory : std::uint8_t { Atmosphere, Sky, Lighting, Distortion, Motion, Particles };
inline constexpr std::size_t kEffectCategoryCount = 6;

// ADR-703 (research: docs/design/effect-library/performance-risks.md). How expensive a type is, and
// where the cost lands. Declared so quality tiers, the panel and the AI catalogue can reason about
// cost by rule rather than by name; `checkRegistry` requires both on every type.
enum class PerformanceClass : std::uint8_t { Unset, VeryLow, Low, Medium, High, VeryHigh };
[[nodiscard]] const char* performanceClassName(PerformanceClass c);
enum PrimaryCost : std::uint8_t {
    CostCpu = 1u << 0,
    CostVertex = 1u << 1,
    CostFragment = 1u << 2,
    CostCompute = 1u << 3,
    CostBandwidth = 1u << 4,
    CostMemory = 1u << 5,
    CostExtraPass = 1u << 6,
};
[[nodiscard]] const char* effectCategoryName(EffectCategory c);

// How one authored effect becomes records in that bucket.
//
// `count` exists for the shower case: one authored Meteor Shower is N trails, each with its own arc
// and envelope, and writing N records into the comet bucket is how it reaches the GPU without a
// line of new shader. `fill` is handed `base`, which already carries the envelope, the elapsed
// second and the effect pointer that every kind shares, and returns false to skip a record (a
// degenerate arc, a stagger that has not started).
struct EffectResolve {
    EffectBucket bucket = EffectBucket::Comet;
    std::size_t (*count)(const EffectInstance&) = nullptr; // null means exactly one
    bool (*fill)(const EffectInstance&, std::size_t index, const EffectContext&,
                 const ResolvedAtmospheric& base, ResolvedAtmospheric& out) = nullptr;
    // ADR-562, `EffectBucket::Medium` only: how this kind's authored numbers become the 16 packed
    // lanes the march reads. Declared here so a new medium kind is still one file and four lines --
    // the alternative was a `switch` over kinds inside `buildAtmosphericFrame`, which is exactly the
    // shared-header edit the registry exists to remove.
    //
    // `envelope` is the lifecycle fade, already resolved. It is passed rather than pre-multiplied
    // because only the per-metre coefficients should scale with it: fading a medium means less of it
    // in the air, which is what scaling an extinction and an emissive density does, while fading its
    // COLOURS would leave a full-strength grey ghost and fading its RADIUS would shrink it rather
    // than dim it (ADR-387).
    //
    // ADR-572 (§17): `flow` is what the air is doing where this medium is -- the subscription the
    // effect already declares, resolved. It is handed to the packer because the packer is where a
    // medium's MOTION is computed, and §17 asks a medium to respond to a flow field.
    //
    // Every kind's packer changed in one commit rather than an overload being added beside the old
    // one: ADR-441 is explicit that this engine takes no compatibility shims while it is in heavy
    // development, and a half-converted hook is the state in which the two versions disagree about
    // which is authoritative.
    //
    // **This is the ONLY channel by which a medium answers the wind, and it is one on purpose.**
    // `agent/fog` and `agent/tornado` each built an answer to the same question and they collided
    // at the merge. ADR-580 §68 had added a SECOND function pointer beside this one,
    // `lean(EffectInstance&, downwind, influence)`, called by `buildAtmosphericFrame` on a copy
    // of the effect just before packing it. Two hooks, one question, and nothing downstream able
    // to disagree about which was authoritative -- this repository's signature defect (ADR-576,
    // and the three subsystems with no users found in one night). It was removed at the merge.
    //
    // **ADR-580 §68's insight is kept and it lives in the packers now.** A kind's answer to the
    // wind IS per kind and the frame builder must not know it: a cosmic vortex is a disc whose
    // shape the march's coefficients were tuned against, so it answers by MOVING; a fog bank is the
    // same placed medium and answers the same way; a tornado's axis is already a CURVE rather than
    // a line, so it answers by BENDING, which is both what a storm column visibly does and free,
    // since the lean term is evaluated per sample whatever its value. All three of those are now
    // the body of a packer that was handed `flow` anyway, and a kind with no wind response simply
    // ignores its `flow` argument -- which is a legitimate answer, and one that
    // `effect_conformance`'s `flow-reaches` check still says out loud, because that check compares
    // PACKED FRAMES and never knew which hook produced them.
    //
    // What made the hook removable rather than merely redundant: `buildAtmosphericFrame` called it
    // on a local copy of the effect whose only reader was `packMediumSlot`, so nothing between the
    // mutation and the pack could observe it. A future kind that needs the world changed BEFORE
    // some other stage reads it does not get a second hook here; it gets a reason recorded in an
    // ADR first.
    void (*pack)(const EffectInstance&, float envelope, const MediumFlowInput& flow,
                 MediumSlot& out) = nullptr;
    // ADR-703. For a bucket with a builder of its own (everything but Comet/Aurora/Medium): how
    // many records ONE live instance contributes to its bucket in `context`, asked through the same
    // code the builder runs. What the conformance probe's "resolves into its own bucket" check
    // counts -- the check that would otherwise have nothing to ask for a builder it does not know.
    // `checkRegistry` requires it for every non-atmospheric bucket except `Surface`, whose builder
    // (`resolveWave`) the probe calls directly.
    std::size_t (*records)(const EffectInstance&, const EffectContext&) = nullptr;
    // ADR-703, `EffectBucket::EntityLanes` only (FXL, world/effects/entity_fx.hpp): this live
    // instance's contribution to its owner's lanes -- a gain, a tint, added emission, a rim, a band,
    // a spill-light request. The builder owns activation, the envelope, folding and the record; the
    // type only says what it adds. `double` is the seconds since its activation window opened.
    bool (*lanes)(const EffectInstance&, const EffectContext&, const NodeView&, double,
                  EntityLaneContribution&) = nullptr;
};

// The declaration. One of these per kind, in that kind's own file.
struct EffectSchema {
    EffectKind kind = EffectKind::Comet;
    // The serialised name. `effectKindName` / `effectKindFromName` are derived from this,
    // so the round trip ADR-392's check 5 asks about cannot be an if-chain that fell behind.
    const char* key = "";
    // The enumerator's own spelling -- "MeteorShower" for `EffectKind::MeteorShower`.
    //
    // It is here because it is NOT the same string as `key`, and finding that out cost a test
    // failure: ADR-392's enum reader compared the lowercased enumerator with `effectKindName`,
    // which worked for three kinds because "Comet" lowercases to "comet" by coincidence. A kind
    // whose file format says "meteors" broke it. Declaring the mapping means the reader can hold
    // the header against the registry **by name** in both directions -- an enumerator with no
    // schema, and a schema naming an enumerator that no longer exists -- which is the guard, and
    // the coincidence was never it.
    const char* enumName = "";
    const char* displayName = ""; // "Comet"
    // ADR-703. One paragraph an artist reads in the panel's help and the AI reads in its tool
    // catalogue: what the effect IS, not how it is drawn.
    const char* description = "";
    PerformanceClass performance = PerformanceClass::Unset;
    std::uint8_t primaryCost = 0; // PrimaryCost bits
    // ADR-702. Which owners this type may be attached to (a mask of `targetBit(EffectTarget::...)`),
    // what the Add Effect menu files it under, and where in the frame it is evaluated. Read by
    // `effectAllowedOn`, the menu and the evaluator -- never re-declared by any of them.
    EffectTargetMask targets = targetBit(EffectTarget::World);
    EffectCategory category = EffectCategory::Atmosphere;
    RenderStage stage = RenderStage::Sky;
    int priority = 0; // lower first within a stage; ties keep stack order
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
    SkyAnchor (*getAnchor)(const EffectInstance&) = nullptr;
    void (*setAnchor)(EffectInstance&, SkyAnchor) = nullptr;
    glm::vec3 (*getAnchorPosition)(const EffectInstance&) = nullptr;
    void (*setAnchorPosition)(EffectInstance&, glm::vec3) = nullptr;
    // ADR-702. A type whose origin (and optionally target) is a THING in the scene -- a hero, a node,
    // the camera, the effect's own owner -- rather than a number. Non-null means the panel draws the
    // "Source" (and "Target") pickers and the serialiser writes `source` / `target` beside the
    // parameters. Only the surface waves have one today; a Space Warp that rides its UFO would not
    // need one (its source is its owner by construction), but a Lightning Strike between two nodes
    // would.
    EffectEndpoint (*getSource)(const EffectInstance&) = nullptr;
    void (*setSource)(EffectInstance&, const EffectEndpoint&) = nullptr;
    bool (*hasTarget)(const EffectInstance&) = nullptr;
    EffectEndpoint (*getTarget)(const EffectInstance&) = nullptr;
    void (*setTarget)(EffectInstance&, bool has, const EffectEndpoint&) = nullptr;
    // The ready-made effect the "Add" button makes and the conformance probe uses -- so a probe is
    // something an artist can actually make rather than a default-constructed struct no scene holds.
    EffectInstance (*factory)(std::string name) = nullptr;
    EffectResolve resolve;
    // Anything in this kind's JSON block that is not a parameter. There are two in the whole
    // family -- the comet's `sparkle.fadeDistance` and `sparkle.seed` -- and they live here rather
    // than as rows because a seed is a `uint32` and a fade distance is not something to automate.
    // In the kind's own file, which is the point.
    void (*writeExtra)(const EffectInstance&, nlohmann::json&) = nullptr;
    // Refuses (the file does not load) rather than skipping what it cannot read: a wave endpoint
    // naming a hero with no name must not quietly become the default endpoint.
    Result<void> (*readExtra)(EffectInstance&, const nlohmann::json&) = nullptr;
    // Kind-specific refusals beyond "every value is finite and inside its hard range", which the
    // registry checks for every kind without being told.
    Result<void> (*validate)(const EffectInstance&) = nullptr;
};

// ---- the enum, closed ------------------------------------------------------------------------------

// Every enumerator of `EffectKind`, in declaration order.
//
// ADR-392's construction, kept and moved here so there is one such list rather than two.
//
// It is a **second, independent list** on purpose, and that is what it is for. Deriving it from
// `effectSchemas()` would save a line per effect and would make "every enumerator has a schema" a
// check asking the schemas about the schemas -- the self-agreeing shape ADR-392 spent its length
// warning about. Because it is independent, `tests/unit/test_effect_conformance.cpp` can read
// `enum class EffectKind` out of `effects/effect_kind.hpp` and require this array to cover exactly the
// enumerators declared there, failing **by the name of the one that is missing**. That is the guard,
// and it is the only one that fires: `AVGEN_WARNINGS_AS_ERRORS` is OFF (`CMakeLists.txt:33`), so a
// `-Wswitch` diagnostic is a line in a five-thousand-line log.
inline constexpr std::array<EffectKind, 35> kEffectKinds{
    EffectKind::Comet,         //
    EffectKind::Aurora,        //
    EffectKind::Vortex,        //
    EffectKind::MeteorShower,  //
    EffectKind::VolumetricFog, //
    EffectKind::Tornado,       // ADR-580
    EffectKind::GroundPulse,   // ADR-702
    EffectKind::TravelBeam,    // ADR-702
    EffectKind::Glow,          // ADR-703 (FXL)
    EffectKind::Pulse,         // ADR-703 (FXL)
    EffectKind::BloomSource,   // ADR-703 (FXL)
    EffectKind::Trail,         // ADR-703 (Wave 1): RIBBON over HIST
    EffectKind::SpaceWarp,     // ADR-703 (DF)
    EffectKind::ParticleEmitter, // ADR-703
    EffectKind::Orbit,         // Wave 2 (XFORM)
    EffectKind::Spiral,        // Wave 2 (XFORM)
    EffectKind::Float,         // Wave 2 (XFORM)
    EffectKind::Shake,         // Wave 2 (XFORM)
    EffectKind::Bounce,        // Wave 2 (XFORM)
    EffectKind::Shockwave,     // Wave 2 (TRIGGER + DF)
    EffectKind::Ripple,        // Wave 2 (TRIGGER + DF)
    EffectKind::Dissolve,      // Wave 2 (FXL surface)
    EffectKind::Growth,        // Wave 2 (FXL surface)
    EffectKind::Breathing,     // Wave 2 (FXL surface)
    EffectKind::OrganicPulsation, // Wave 2 (FXL surface)
    EffectKind::Bioluminescence,  // Wave 2 (FXL surface)
    EffectKind::PulsingVeins,  // Wave 2 (FXL surface)
    EffectKind::Fresnel,       // Wave 2 (FXL surface)
    EffectKind::RimLight,      // Wave 2 (FXL surface)
    EffectKind::ColorCycling,  // Wave 2 (FXL surface)
    EffectKind::VelocityDistortion, // Wave 2 (DF over HIST)
    EffectKind::MotionSmear,   // Wave 2 (FXL surface)
    EffectKind::Stars,         // Wave 2 (the sky's star field)
    EffectKind::HeatShimmer,   // Wave 3 (DF: Cylinder + Shimmer)
    EffectKind::GravitationalLens, // Wave 3 (DF: Facing + Lens)
};

// The kind's position in `kEffectKinds`, or `size()` for an enumerator that is not in it --
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
[[nodiscard]] constexpr std::size_t effectKindIndex(EffectKind k) {
    for (std::size_t i = 0; i < kEffectKinds.size(); ++i) {
        if (kEffectKinds[i] == k) {
            return i;
        }
    }
    return kEffectKinds.size();
}

static_assert(effectKindIndex(EffectKind::Comet) == 0);
static_assert(effectKindIndex(EffectKind::VolumetricFog) == 4);
static_assert(effectKindIndex(EffectKind::Tornado) == 5);

[[nodiscard]] inline std::span<const EffectKind> declaredEffectKinds() { return kEffectKinds; }

// The rows every kind registers whatever it is: lifecycle, ground illumination and the field
// subscription. ADR-387's point, and the reason it is worth keeping: ONE row here gives every kind
// a registered parameter, a modulation target, a timeline key, a preset member, a save entry and
// apply and capture in both directions -- and a new kind gets it on the day it is added, without
// anybody remembering to.
//
// They are registered for every kind including the ones whose panel does not offer them, because
// the set of paths a project may already name must not shrink.
[[nodiscard]] std::span<const EffectField> sharedEffectFields();
// ADR-702: whether a shared row means anything for this type. Timing rows always do. The ground
// illumination rows only for a type that lights the ground (`groundGlow`), and the field
// subscription only for a type the evaluator samples a field for -- so a Ground Pulse registers no
// `groundIntensity` that nothing would ever read. Every consumer of `sharedEffectFields` (the
// registrar, the serialiser, the panel) asks this, which is what keeps the three in agreement.
//
// Scoped to the surface waves on purpose: the sky and medium types have always registered every
// shared row, projects carry values for them, and removing a registered parameter from an existing
// type is a content migration that this ADR does not need.
[[nodiscard]] bool sharedFieldApplies(const EffectSchema& schema, const EffectField& field);

// ---- the registry --------------------------------------------------------------------------------

// Every declared kind, in enumerator order.
[[nodiscard]] std::span<const EffectSchema* const> effectSchemas();
// The schema for a kind, or null -- which is the loud case: `checkRegistry` reports it and
// `resolveAtmosphericEffects` counts the effect as dropped rather than drawing it as a neighbour.
[[nodiscard]] const EffectSchema* effectSchema(EffectKind kind);
[[nodiscard]] const EffectSchema* effectSchema(std::string_view key);
// The schema an effect is of. Null for a kind with no declaration.
[[nodiscard]] const EffectSchema* effectSchema(const EffectInstance& effect);

// ---- reading and writing one row -----------------------------------------------------------------
//
// Every consumer goes through these, so a struct-backed row and a store-backed row are the same
// thing downstream.
[[nodiscard]] float fieldFloat(const EffectField& field, const EffectSchema& schema,
                               const EffectInstance& effect);
void setFieldFloat(const EffectField& field, const EffectSchema& schema, EffectInstance& effect, float v);
[[nodiscard]] glm::vec3 fieldColor(const EffectField& field, const EffectSchema& schema,
                                   const EffectInstance& effect);
void setFieldColor(const EffectField& field, const EffectSchema& schema, EffectInstance& effect,
                   glm::vec3 v);
[[nodiscard]] bool fieldBool(const EffectField& field, const EffectSchema& schema,
                             const EffectInstance& effect);
void setFieldBool(const EffectField& field, const EffectSchema& schema, EffectInstance& effect, bool v);

// The key a stored row occupies in `EffectInstance::values`.
[[nodiscard]] std::string storeKey(const EffectSchema& schema, const EffectField& field);

// ---- the derived directions ----------------------------------------------------------------------
//
// Each of these was a hand-written per-kind list before ADR-500 and is now one loop over `fields`.

// ADR-392's `sanitise`: the row's floor and ceiling, applied to a modulated final.
void sanitiseEffect(EffectInstance& effect);

// The kind's payload block, in the exact shape the file format already has.
void effectPayloadToJson(const EffectSchema& schema, const EffectInstance& effect, nlohmann::json& out);
// Returns a message on a value the file cannot mean (an unknown anchor name), the way `fromJson`
// already refuses one. An absent key leaves the default, which is what every file written before a
// row existed describes.
[[nodiscard]] Result<void> effectPayloadFromJson(const EffectSchema& schema, const nlohmann::json& block,
                                                 EffectInstance& effect);

// Every value finite and inside its hard range, plus the kind's own `validate`.
[[nodiscard]] Result<void> validateEffectFields(const EffectInstance& effect);

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
