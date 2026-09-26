#pragma once

// Set pieces: staging scenarios written from a template (ADR-928).
//
// ## Why this exists
//
// `StagingDesc` could already say everything a UFO event needs -- a craft and its beam as one actor,
// region queries with a clearance, claims, parallel cues, the `stillRoles` gate -- and every one of
// Glowmere Valley 3's was still written beat by beat in Python (`tools/gv3/cast.py`): one abduction,
// 40 hand-tuned numbers, and a dissolve re-timed by measuring where it landed. The owner's brief asks
// for many UFO events through the film, varied in place, scale, timing, animal count and relation to
// the music, and "never the same abduction duplicated". Writing ten of those by hand is how one
// abduction gets copied ten times.
//
// A set piece is therefore a **template**: a parameterised `ScenarioDesc` with named slots, instanced
// with overrides. Three ship:
//
//   abduction  1-3 animals lifted in parallel, one role and one cue each, under a craft the
//              `stillRoles` gate has seen stop (ADR-385)
//   survey     the beam lights over a field and sweeps across it; nothing is lifted
//   flyby      a crossing on a path
//
// ## Slots
//
// Every number a template reads is a slot with a default and a legal range. A slot is one of two
// kinds, and the difference is what the rules call the silent no-op family:
//
//   Parameter  becomes a scenario parameter (`staging/setpiece/<id>/<slot>`) because a step reads it
//              every frame: a duration, a height, a beam level, the moment a beat is clocked to.
//              Keyframeable, presettable and visible in the editor like any director knob.
//   Structure  is folded into the beats when the template is instanced: how many animals, which
//              bearing the craft comes in on, how far away it appears. Nothing reads these at run
//              time, so registering them would be registering parameters that do nothing; they
//              change by revising the plan that made the set piece.
//
// ## Time
//
// A set piece is placed by ONE moment -- by default the beam for an abduction or a survey, the
// crossing for a flyby -- at a timeline second (a plan resolves "bar 89" or "the drop" to one before
// it gets here). That moment's beat carries a clock (`BeatDesc::startAt`), so it lands on the frame
// at or after its second whatever the beats before it cost; everything before it is scheduled
// backwards from it with `kAnchorSlackSeconds` to spare, and everything after follows at the
// authored durations. The other moments' nominal times are reported, not promised: a beat hand-off
// costs a frame, and `avgen_cast_trace` measures where they land.
//
// ## One craft, several set pieces
//
// Each set piece is its own scenario, `setpiece/<id>`, and all of them autostart and wait. The craft
// is hidden before a set piece takes it and hidden again when it leaves, so a craft can play any
// number of them in sequence; the plan's compiler orders them per craft and refuses overlaps and
// travel it cannot make in the gap (`directing/validator.cpp`). Every set piece hides its craft at
// t = 0 -- idempotent, and it keeps an instance's content independent of which set pieces come
// before it, so adding an earlier one does not rewrite (and re-fingerprint) the later ones.
//
// ## Events
//
// Every beat of a set piece is a world event named `setpiece/<id>/<beat>` (ADR-930): `approach`,
// `beam`, `lift`, `sweep`, `cross` and `depart` are the ones that mean something to a viewer, and
// `setPieceMoments` lists them per template.

#include "core/error.hpp"
#include "stage/staging.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::stage {

enum class SetPieceKind : std::uint8_t { Abduction, Survey, Flyby };
[[nodiscard]] const char* setPieceKindName(SetPieceKind kind);
[[nodiscard]] std::optional<SetPieceKind> setPieceKindFromName(std::string_view name);
[[nodiscard]] std::vector<std::string> setPieceKindNames();

enum class SlotUse : std::uint8_t {
    Parameter, // read by a step every frame: a scenario parameter
    Structure, // folded into the beats when the template is instanced
};

struct SetPieceSlot {
    const char* name;
    float value;
    float min;
    float max;
    SlotUse use;
    const char* label; // what somebody describing the picture would call it: "hover height"
    const char* unit;  // "m", "s", "deg", "deg/s", "m/s", "x", ""
};

[[nodiscard]] std::span<const SetPieceSlot> setPieceSlots(SetPieceKind kind);
[[nodiscard]] const SetPieceSlot* findSetPieceSlot(SetPieceKind kind, std::string_view name);
// Every slot name of a template, for an error's suggestions and for a schema.
[[nodiscard]] std::vector<std::string> setPieceSlotNames(SetPieceKind kind);

// The moments a template's viewer would name, in the order they happen. Each is a beat, and so a
// world event `setpiece/<id>/<moment>`.
[[nodiscard]] std::vector<std::string> setPieceMoments(SetPieceKind kind);
[[nodiscard]] std::string defaultSetPieceMoment(SetPieceKind kind);

// The slack scheduled before the clocked moment: time the beats before it may overrun by (a frame per
// hand-off, a gate that waits a frame for the craft to settle) and still let it land on its second.
// A quarter of a second is fifteen frames at 60 fps, several times what those costs add up to.
inline constexpr double kAnchorSlackSeconds = 0.25;
// The least time between one set piece's craft leaving and the next taking it (ADR-928): the same
// slack, for the same reason, plus the frame its scenario takes to finish.
inline constexpr double kCraftHandoverSeconds = 0.5;

struct SetPiecePlace {
    enum class Kind : std::uint8_t {
        Point,  // the craft works over this point
        Region, // the craft works over a subject found within `radius` of `point`
    };
    Kind kind = Kind::Point;
    glm::vec2 point{0.0f}; // world x, z
    float radius = 0.0f;   // Region only: metres
};

struct SetPieceSpec {
    std::string id;                 // "east-field": scenario "setpiece/east-field"
    SetPieceKind kind = SetPieceKind::Abduction;
    std::string actor;              // the craft: a staging actor, with a `beam` part unless a flyby
    std::string moment;             // the moment `atSeconds` places; "" = the template's default
    double atSeconds = 0.0;
    SetPiecePlace place;
    std::string tag = "animal";     // what the subject query and the gather look for
    std::vector<std::pair<std::string, float>> overrides; // slot overrides, by slot name
    // Optional: the beam's colour while this set piece runs, linear RGB. Written to both ends of the
    // column's colour ramp at the beam, and `beamRestColor` written back as it departs, so the next
    // set piece finds the beam the scene authored.
    std::optional<glm::vec3> beamColor;
    glm::vec4 beamRestStart{0.0f}; // the scene's `colorStart` / `colorEnd`, for the restore
    glm::vec4 beamRestEnd{0.0f};
    std::vector<std::string> animals; // named subjects (abduction), in lift order; empty = gather
    bool beamPart = true;             // the craft has a `beam` part (required unless a flyby)
    float groundAtPlace = 0.0f;       // the terrain under `place.point`, for the queries' centre
    double frameSeconds = 1.0 / 60.0; // the film's frame, for the nominal timeline
    std::uint32_t seed = 0;           // 0 = derived from the id
};

// A slot's value for this set piece: its override, else the template's default. The slot must exist.
[[nodiscard]] float setPieceValue(const SetPieceSpec& spec, std::string_view slot);
// How many animals an abduction lifts: the named ones, else the `animals` slot. 0 for the others.
[[nodiscard]] int setPieceAnimalCount(const SetPieceSpec& spec);

// Refuses what an instance could not honour: an unknown slot, a value outside its range, an unknown
// moment, a count the template does not take, names that disagree with the count, no craft.
[[nodiscard]] Result<void> validateSetPieceSpec(const SetPieceSpec& spec);

// What an instance will do, nominally: when the craft is taken and let go, when each moment is,
// and where it works, appears and leaves to (world x, z; for a Region the station is the centre, an
// estimate -- the subject decides the rest at run time).
struct SetPieceTimeline {
    double start = 0.0;  // the craft is taken (its hidden move to the entry begins)
    double end = 0.0;    // the craft is hidden again and the scenario over
    std::vector<std::pair<std::string, double>> moments;
    glm::vec2 station{0.0f};
    glm::vec2 entry{0.0f};
    glm::vec2 exit{0.0f};
    [[nodiscard]] std::optional<double> at(std::string_view moment) const;
};
[[nodiscard]] Result<SetPieceTimeline> setPieceTimeline(const SetPieceSpec& spec);

// The instance: a scenario named `setPieceScenarioName(spec.id)`, validated by `validateSetPieceSpec`.
[[nodiscard]] Result<ScenarioDesc> instanceSetPiece(const SetPieceSpec& spec);

[[nodiscard]] std::string setPieceScenarioName(std::string_view id);
[[nodiscard]] bool isSetPieceScenario(std::string_view scenario);
// "setpiece/east-field" -> "east-field"; "" when it is not one.
[[nodiscard]] std::string setPieceIdOf(std::string_view scenario);

// Where a compass bearing points, as a world x, z direction: 0 is north (-z), 90 east (+x).
[[nodiscard]] glm::vec2 bearingDirection(float degrees);

} // namespace avgen::stage
