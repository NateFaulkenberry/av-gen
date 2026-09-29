#include "stage/setpiece.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace avgen::stage {

namespace {

constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;

using U = SlotUse;

// ---- the templates' slots -----------------------------------------------------------------------
//
// The defaults are Glowmere Valley 3's own abduction (`tools/gv3/cast.py`), where it had one: a
// template that shipped with numbers nobody had looked at through a lens would make every set piece
// the plan did not tune look like a test.

// The craft's arrival and departure are the same slots in the two templates that stop somewhere;
// they are written out in both rather than spliced, so each table reads as the whole template.
constexpr std::array<SetPieceSlot, 35> kAbduction{{
    {"transitSeconds", 0.1f, 0.02f, 30.0f, U::Parameter, "hidden move to where it appears", "s"},
    {"approachSeconds", 12.0f, 1.0f, 120.0f, U::Parameter, "approach", "s"},
    {"entryHeight", 70.0f, 5.0f, 400.0f, U::Parameter, "height it appears at, above the ground", "m"},
    {"cruiseClearance", 34.0f, 0.0f, 200.0f, U::Parameter, "least height over the ground on the way in", "m"},
    {"hoverHeight", 23.0f, 4.0f, 120.0f, U::Parameter, "hover height above the ground", "m"},
    {"hoverSeconds", 1.6f, 0.0f, 30.0f, U::Parameter, "hover before the beam", "s"},
    {"craftWobble", 0.3f, 0.0f, 5.0f, U::Parameter, "hover sway", "m"},
    {"craftWobbleRate", 0.45f, 0.0f, 5.0f, U::Parameter, "hover sway rate", "Hz"},
    {"beamSeconds", 1.1f, 0.05f, 10.0f, U::Parameter, "beam rising", "s"},
    {"beamSpawnRate", 4200.0f, 0.0f, 60000.0f, U::Parameter, "beam density", "/s"},
    {"beamEmissive", 0.7f, 0.0f, 20.0f, U::Parameter, "beam brightness", "x"},
    {"beamSize", 1.0f, 0.0f, 5.0f, U::Parameter, "beam width", "x"},
    {"beamFadeSeconds", 0.15f, 0.0f, 5.0f, U::Parameter, "beam drain", "s"},
    {"gapSeconds", 1.5f, 0.0f, 30.0f, U::Parameter, "hold after the beam goes out", "s"},
    {"leaveSeconds", 7.0f, 0.5f, 60.0f, U::Parameter, "leaving", "s"},
    {"leaveHeight", 280.0f, 0.0f, 1000.0f, U::Parameter, "height it leaves to, above the ground", "m"},
    {"liftSeconds", 4.9f, 0.3f, 30.0f, U::Parameter, "lift duration", "s"},
    {"liftHeight", -3.55f, -40.0f, 20.0f, U::Parameter, "where the lift ends, from the beam's source", "m"},
    {"animalSpin", 90.0f, -1440.0f, 1440.0f, U::Parameter, "animal spin", "deg/s"},
    {"animalWobble", 0.0f, 0.0f, 5.0f, U::Parameter, "animal sway", "m"},
    {"animalWobbleRate", 1.35f, 0.0f, 10.0f, U::Parameter, "animal sway rate", "Hz"},
    {"animalGait", 0.7f, 0.0f, 10.0f, U::Parameter, "a carried animal's legs", "x"},
    {"fadeDelaySeconds", 3.5f, 0.0f, 30.0f, U::Parameter, "rise before it dissolves", "s"},
    {"fadeSeconds", 1.4f, 0.0f, 30.0f, U::Parameter, "dissolve", "s"},
    {"gatherRadius", 18.0f, 1.0f, 200.0f, U::Parameter, "how far it reaches for an animal", "m"},
    // Glowmere Valley 2's own abduction's (6.5 m). The canopy asked is the tallest scatter layer that can
    // grow at a point (`ClearanceField::canopyHeight`), not what stands there: over Glowmere Valley 3's
    // whole valley floor that is 3.2 m (the meadow layer), 5.4 m in scrub, 7.1-8.0 m in the woods. The
    // first default, 3 m, refused every animal on open ground; 6.5 m lifts from meadow and scrub and
    // refuses the woods.
    {"targetClearance", 6.5f, 0.0f, 60.0f, U::Parameter, "canopy allowed over an animal (0: do not ask)", "m"},
    {"approachBearing", 180.0f, 0.0f, 360.0f, U::Structure, "comes in from (compass)", "deg"},
    {"approachDistance", 220.0f, 0.0f, 1500.0f, U::Structure, "appears this far away", "m"},
    {"departBearing", 0.0f, 0.0f, 360.0f, U::Structure, "leaves toward (compass)", "deg"},
    {"departDistance", 320.0f, 0.0f, 2000.0f, U::Structure, "leaves this far", "m"},
    {"cruiseSpeed", 80.0f, 1.0f, 1000.0f, U::Structure, "fastest it may fly between set pieces", "m/s"},
    {"animals", 1.0f, 1.0f, 3.0f, U::Structure, "animals lifted", ""},
    {"stackRadius", 1.6f, 0.0f, 8.0f, U::Structure, "animals' spacing in the beam", "m"},
    {"stackStagger", 1.2f, 0.0f, 8.0f, U::Structure, "animals' height spacing in the beam", "m"},
    // ADR-984: 0 takes what it lifted for good (retired, ADR-934). More gives it back: put where it
    // was taken, as it was, this many seconds after the beam goes out -- GV3's drummer, behind his kit
    // again once no shot sees it.
    {"returnSeconds", 0.0f, 0.0f, 600.0f, U::Structure, "give it back this long after the beam goes out (0: taken for good)", "s"},
}};

constexpr std::array<SetPieceSlot, 24> kSurvey{{
    {"transitSeconds", 0.1f, 0.02f, 30.0f, U::Parameter, "hidden move to where it appears", "s"},
    {"approachSeconds", 12.0f, 1.0f, 120.0f, U::Parameter, "approach", "s"},
    {"entryHeight", 70.0f, 5.0f, 400.0f, U::Parameter, "height it appears at, above the ground", "m"},
    {"cruiseClearance", 34.0f, 0.0f, 200.0f, U::Parameter, "least height over the ground on the way in", "m"},
    {"hoverHeight", 23.0f, 4.0f, 120.0f, U::Parameter, "hover height above the ground", "m"},
    {"hoverSeconds", 1.6f, 0.0f, 30.0f, U::Parameter, "hover before the beam", "s"},
    {"craftWobble", 0.3f, 0.0f, 5.0f, U::Parameter, "hover sway", "m"},
    {"craftWobbleRate", 0.45f, 0.0f, 5.0f, U::Parameter, "hover sway rate", "Hz"},
    {"beamSeconds", 1.1f, 0.05f, 10.0f, U::Parameter, "beam rising", "s"},
    {"beamSpawnRate", 4200.0f, 0.0f, 60000.0f, U::Parameter, "beam density", "/s"},
    {"beamEmissive", 0.7f, 0.0f, 20.0f, U::Parameter, "beam brightness", "x"},
    {"beamSize", 1.0f, 0.0f, 5.0f, U::Parameter, "beam width", "x"},
    {"beamFadeSeconds", 0.15f, 0.0f, 5.0f, U::Parameter, "beam drain", "s"},
    {"gapSeconds", 1.5f, 0.0f, 30.0f, U::Parameter, "hold after the beam goes out", "s"},
    {"leaveSeconds", 7.0f, 0.5f, 60.0f, U::Parameter, "leaving", "s"},
    {"leaveHeight", 280.0f, 0.0f, 1000.0f, U::Parameter, "height it leaves to, above the ground", "m"},
    {"sweepSeconds", 10.0f, 0.5f, 120.0f, U::Parameter, "sweep", "s"},
    {"approachBearing", 180.0f, 0.0f, 360.0f, U::Structure, "comes in from (compass)", "deg"},
    {"approachDistance", 220.0f, 0.0f, 1500.0f, U::Structure, "appears this far away", "m"},
    {"departBearing", 0.0f, 0.0f, 360.0f, U::Structure, "leaves toward (compass)", "deg"},
    {"departDistance", 320.0f, 0.0f, 2000.0f, U::Structure, "leaves this far", "m"},
    {"cruiseSpeed", 80.0f, 1.0f, 1000.0f, U::Structure, "fastest it may fly between set pieces", "m/s"},
    {"sweepBearing", 90.0f, 0.0f, 360.0f, U::Structure, "sweeps toward (compass)", "deg"},
    {"sweepLength", 60.0f, 0.0f, 600.0f, U::Structure, "sweep length", "m"},
}};

constexpr std::array<SetPieceSlot, 7> kFlyby{{
    {"transitSeconds", 0.1f, 0.02f, 30.0f, U::Parameter, "hidden move to where it appears", "s"},
    {"altitude", 80.0f, 5.0f, 1000.0f, U::Parameter, "height above the ground", "m"},
    {"crossSeconds", 3.2f, 0.3f, 120.0f, U::Parameter, "crossing", "s"},
    {"cruiseClearance", 34.0f, 0.0f, 200.0f, U::Parameter, "least height over the ground", "m"},
    {"pathBearing", 270.0f, 0.0f, 360.0f, U::Structure, "flies toward (compass)", "deg"},
    {"pathLength", 460.0f, 10.0f, 4000.0f, U::Structure, "path length", "m"},
    {"cruiseSpeed", 80.0f, 1.0f, 1000.0f, U::Structure, "fastest it may fly between set pieces", "m/s"},
}};

// ---- building ------------------------------------------------------------------------------------

Value param(const char* name) { return bound(name); }

StepDesc step(StepKind kind, std::string name, std::string role = {}) {
    StepDesc s;
    s.kind = kind;
    s.name = std::move(name);
    s.role = std::move(role);
    return s;
}

StepDesc moveTo(std::string name, glm::vec2 xz, const char* height, const char* duration) {
    StepDesc s = step(StepKind::MoveTo, std::move(name));
    s.point = glm::vec3(xz.x, 0.0f, xz.y);
    s.aboveGround = true;
    s.height = param(height);
    s.duration = param(duration);
    return s;
}

StepDesc hold(std::string name, const char* duration, bool wobble) {
    StepDesc s = step(StepKind::Follow, std::move(name));
    s.relative = true;
    s.hold = true;
    s.duration = param(duration);
    if (wobble) {
        s.wobble = param("craftWobble");
        s.wobbleRate = param("craftWobbleRate");
    }
    return s;
}

StepDesc setTo(std::string name, std::string target, Value to, Value duration = literal(0.0f), int component = 0) {
    StepDesc s = step(StepKind::Set, std::move(name));
    s.target = std::move(target);
    s.to = std::move(to);
    s.duration = std::move(duration);
    s.component = component;
    return s;
}

CueDesc cue(std::string role, std::vector<StepDesc> steps) {
    CueDesc c;
    c.role = std::move(role);
    c.steps = std::move(steps);
    return c;
}

std::string targetRole(int k) { return fmt::format("target{}", k + 1); }

// The per-template numbers the timeline and the beats are both built from, so the two cannot drift.
struct Plan {
    const SetPieceSpec* spec = nullptr;
    std::string moment;
    SetPieceTimeline timeline;
    glm::vec2 sweepEnd{0.0f}; // survey
    double transitAt = 0.0;   // when the craft is taken
    int animals = 0;
};

float v(const SetPieceSpec& s, std::string_view slot) { return setPieceValue(s, slot); }

Result<Plan> plan(const SetPieceSpec& spec) {
    if (auto ok = validateSetPieceSpec(spec); !ok) {
        return std::unexpected(ok.error());
    }
    Plan p;
    p.spec = &spec;
    p.moment = spec.moment.empty() ? defaultSetPieceMoment(spec.kind) : spec.moment;
    p.animals = setPieceAnimalCount(spec);
    const double fs = spec.frameSeconds;
    const double t = spec.atSeconds;
    SetPieceTimeline& tl = p.timeline;

    // Offsets of every moment from the FIRST one, on the frame model the beats below run on: a beat
    // hand-off costs a frame; a `moveTo` that opens its beat ends a frame before its duration is up;
    // one that follows an instant step (a `show`) ends exactly on it.
    std::vector<std::pair<std::string, double>> offsets;
    const auto d = [&](std::string_view slot) { return static_cast<double>(v(spec, slot)); };
    double tail = 0.0; // from the last moment to the end
    const double transit = d("transitSeconds");
    switch (spec.kind) {
    case SetPieceKind::Abduction: {
        const double ta = d("approachSeconds");
        const double th = d("hoverSeconds");
        const double tb = d("beamSeconds");
        // The lift beat lasts as long as its longest cue: the rise, or the wait, the dissolve and the
        // retire after it -- the retire a frame after the dissolve ends.
        const double lift = std::max(d("liftSeconds"), d("fadeDelaySeconds") + d("fadeSeconds") + fs);
        offsets = {{"approach", 0.0},
                   {"beam", ta + th + (2.0 * fs)},
                   {"lift", ta + th + tb + (3.0 * fs)},
                   {"depart", ta + th + tb + lift + (4.0 * fs)}};
        tail = d("gapSeconds") + d("leaveSeconds") + fs;
        // ADR-984: a subject given back is put back a step after its wait, inside the departure.
        if (d("returnSeconds") > 0.0) {
            tail = std::max(tail, d("returnSeconds") + (2.0 * fs));
        }
        break;
    }
    case SetPieceKind::Survey: {
        const double ta = d("approachSeconds");
        const double th = d("hoverSeconds");
        const double tb = d("beamSeconds");
        const double ts = d("sweepSeconds");
        offsets = {{"approach", 0.0},
                   {"beam", ta + th + (2.0 * fs)},
                   {"sweep", ta + th + tb + (3.0 * fs)},
                   {"depart", ta + th + tb + ts + (3.0 * fs)}};
        tail = d("gapSeconds") + d("leaveSeconds") + fs;
        break;
    }
    case SetPieceKind::Flyby: {
        offsets = {{"cross", 0.0}, {"depart", d("crossSeconds") + fs}};
        tail = 0.0;
        break;
    }
    }
    double anchorOffset = 0.0;
    for (const auto& [name, offset] : offsets) {
        if (name == p.moment) {
            anchorOffset = offset;
        }
    }
    // The first moment, as scheduled: early by the slack, which the clocked moment's own beat absorbs.
    const double first = t - anchorOffset - kAnchorSlackSeconds;
    p.transitAt = first - transit;
    bool after = false;
    for (const auto& [name, offset] : offsets) {
        if (name == p.moment) {
            after = true;
            tl.moments.emplace_back(name, t);
        } else if (!after) {
            tl.moments.emplace_back(name, first + offset);
        } else {
            tl.moments.emplace_back(name, t + (offset - anchorOffset));
        }
    }
    tl.start = p.transitAt;
    tl.end = tl.moments.back().second + tail;

    // ---- where ----
    const glm::vec2 place = spec.place.point;
    if (spec.kind == SetPieceKind::Flyby) {
        const glm::vec2 dir = bearingDirection(v(spec, "pathBearing"));
        const float half = v(spec, "pathLength") * 0.5f;
        tl.station = place;
        tl.entry = place - (dir * half);
        tl.exit = place + (dir * half);
    } else {
        tl.station = place;
        glm::vec2 leaveFrom = place;
        if (spec.kind == SetPieceKind::Survey) {
            const glm::vec2 dir = bearingDirection(v(spec, "sweepBearing"));
            const float half = v(spec, "sweepLength") * 0.5f;
            tl.station = place - (dir * half);
            p.sweepEnd = place + (dir * half);
            leaveFrom = p.sweepEnd;
        }
        tl.entry = tl.station + (bearingDirection(v(spec, "approachBearing")) * v(spec, "approachDistance"));
        tl.exit = leaveFrom + (bearingDirection(v(spec, "departBearing")) * v(spec, "departDistance"));
    }
    return p;
}

// Every parameter name a scenario's beats actually read, so a slot the instance never uses -- the
// gather radius of an abduction whose animals are named -- is not registered as a knob that does
// nothing (the silent no-op family the engineering rules name).
void referenced(const Value& v, std::vector<std::string>& out) {
    if (v.bound() && std::find(out.begin(), out.end(), v.param) == out.end()) {
        out.push_back(v.param);
    }
}

std::vector<std::string> referencedParams(const std::vector<BeatDesc>& beats) {
    std::vector<std::string> out;
    for (const BeatDesc& b : beats) {
        referenced(b.stillSpeed, out);
        referenced(b.startAt, out);
        for (const QueryDesc& q : b.find) {
            referenced(q.radius, out);
            referenced(q.minRadius, out);
            referenced(q.clearance, out);
        }
        for (const CueDesc& c : b.cues) {
            for (const StepDesc& st : c.steps) {
                for (const Value* v : {&st.duration, &st.height, &st.speed, &st.tolerance, &st.clearance, &st.spin,
                                       &st.wobble, &st.wobbleRate, &st.to, &st.from, &st.rate}) {
                    referenced(*v, out);
                }
            }
        }
    }
    return out;
}

// Every Parameter slot of the template, with this set piece's value, plus the two clocks and, when
// the beam is coloured, its colour -- kept only where a beat reads it.
std::vector<ScenarioParam> parametersOf(const Plan& p, const std::vector<BeatDesc>& beats) {
    const SetPieceSpec& spec = *p.spec;
    std::vector<ScenarioParam> out;
    for (const SetPieceSlot& slot : setPieceSlots(spec.kind)) {
        if (slot.use == SlotUse::Parameter) {
            out.push_back(ScenarioParam{slot.name, setPieceValue(spec, slot.name), slot.min, slot.max,
                                        setPieceSlotLabel(slot)});
        }
    }
    // The clocks are timeline seconds. The range is the widest a film could need; the validator is
    // what refuses a set piece that falls outside its song.
    out.push_back(ScenarioParam{"transitAt", static_cast<float>(p.transitAt), 0.0f, 100000.0f,
                                "craft taken for its hidden move, at (s)"});
    out.push_back(ScenarioParam{p.moment + "At", static_cast<float>(spec.atSeconds), 0.0f, 100000.0f,
                                fmt::format("{} at (s)", p.moment)});
    if (spec.beamColor) {
        out.push_back(ScenarioParam{"beamRed", spec.beamColor->r, 0.0f, 64.0f, "beam colour: red"});
        out.push_back(ScenarioParam{"beamGreen", spec.beamColor->g, 0.0f, 64.0f, "beam colour: green"});
        out.push_back(ScenarioParam{"beamBlue", spec.beamColor->b, 0.0f, 64.0f, "beam colour: blue"});
    }
    const std::vector<std::string> read = referencedParams(beats);
    std::erase_if(out, [&](const ScenarioParam& q) { return std::find(read.begin(), read.end(), q.name) == read.end(); });
    return out;
}

// The beam's colour, into both ends of its ramp: three channels of `colorStart` and three of
// `colorEnd`, each a one-component `set` (ADR-928's `component`), alpha left as authored. Six
// parallel cues rather than one cue of six steps: a cue advances one step a frame, and six frames of
// a half-tinted beam is a flicker somebody would see.
std::vector<CueDesc> colourCues(const SetPieceSpec& spec, bool restore) {
    std::vector<CueDesc> out;
    const char* channels[] = {"beamRed", "beamGreen", "beamBlue"};
    for (const char* target : {"colorStart", "colorEnd"}) {
        const glm::vec4 rest = std::string_view(target) == "colorStart" ? spec.beamRestStart : spec.beamRestEnd;
        for (int c = 0; c < 3; ++c) {
            Value to = restore ? literal(rest[c]) : param(channels[c]);
            out.push_back(cue("actor.beam", {setTo(fmt::format("{}-{}-{}", restore ? "restore" : "tint", target, c),
                                                   target, to, literal(0.0f), c)}));
        }
    }
    return out;
}

BeatDesc beat(std::string name, std::vector<CueDesc> cues) {
    BeatDesc b;
    b.name = std::move(name);
    b.cues = std::move(cues);
    return b;
}

// The beats every template opens with: the craft hidden at t = 0, then a hidden move to where it will
// appear, clocked to `transitAt`.
std::vector<BeatDesc> opening(const Plan& p, glm::vec2 entryXZ, const char* entryHeight, bool beamPart) {
    const SetPieceSpec& spec = *p.spec;
    std::vector<BeatDesc> beats;
    beats.push_back(beat("rest", {cue("actor", {step(StepKind::Hide, "unseen")})}));
    BeatDesc transit = beat("transit", {cue("actor", {moveTo("to-entry", entryXZ, entryHeight, "transitSeconds")})});
    if (beamPart) {
        transit.cues.push_back(cue("actor.beam", {step(StepKind::Hide, "beam-off")}));
        // The colour goes on while the beam is out and the craft unseen.
        if (spec.beamColor) {
            for (CueDesc& c : colourCues(spec, false)) {
                transit.cues.push_back(std::move(c));
            }
        }
    }
    transit.startAt = param("transitAt");
    beats.push_back(std::move(transit));
    return beats;
}

// The placed moment's beat carries the clock: it begins on the frame at or after `<moment>At`.
void clock(std::vector<BeatDesc>& beats, const Plan& p) {
    for (BeatDesc& b : beats) {
        if (b.name == p.moment) {
            b.startAt = bound(p.moment + "At");
        }
    }
}

// The beam coming up while the craft holds: the same four cues for an abduction and a survey.
std::vector<CueDesc> beamRising() {
    StepDesc on = step(StepKind::Show, "beam-on");
    StepDesc rate = setTo("beam-density", "spawnRate", param("beamSpawnRate"), param("beamSeconds"));
    StepDesc width = setTo("beam-width", "size", param("beamSize"), param("beamSeconds"));
    width.from = literal(0.0f);
    width.hasFrom = true;
    StepDesc glow = setTo("beam-brightness", "emissive", param("beamEmissive"), param("beamSeconds"));
    return {cue("actor", {hold("hold", "beamSeconds", true)}), cue("actor.beam", {on, rate}), cue("actor.beam", {width}),
            cue("actor.beam", {glow})};
}

// The beam going out and the craft leaving -- and, when the set piece gives back what it lifted
// (ADR-984), each subject put back where it was taken, `returnSeconds` after the beam goes out.
BeatDesc departing(const Plan& p, int giveBack = 0) {
    const SetPieceSpec& spec = *p.spec;
    StepDesc cut = setTo("beam-cut", "size", literal(0.0f), param("beamFadeSeconds"));
    BeatDesc b = beat("depart", {cue("actor.beam", {step(StepKind::Hide, "beam-out"),
                                                    setTo("beam-empty", "spawnRate", literal(0.0f)), cut})});
    if (spec.beamColor) {
        for (CueDesc& c : colourCues(spec, true)) { // the beam is out: the next set piece finds it as authored
            b.cues.push_back(std::move(c));
        }
    }
    StepDesc gap = step(StepKind::Wait, "hold");
    gap.duration = param("gapSeconds");
    b.cues.push_back(cue("actor", {gap, moveTo("leave", p.timeline.exit, "leaveHeight", "leaveSeconds"),
                                   step(StepKind::Hide, "gone")}));
    for (int k = 0; k < giveBack; ++k) {
        StepDesc away = step(StepKind::Wait, "away");
        away.duration = literal(v(spec, "returnSeconds"));
        b.cues.push_back(cue(targetRole(k), {away, step(StepKind::Return, "back")}));
    }
    b.release = true;
    return b;
}

QueryDesc gather(const SetPieceSpec& spec, int k, bool fromFirst) {
    QueryDesc q;
    q.bind = targetRole(k);
    if (k < static_cast<int>(spec.animals.size())) {
        q.name = spec.animals[static_cast<std::size_t>(k)];
        return q;
    }
    q.tag = spec.tag;
    if (fromFirst) {
        q.from = targetRole(0);
    } else {
        q.center = glm::vec3(spec.place.point.x, spec.groundAtPlace, spec.place.point.y);
    }
    q.radius = param("gatherRadius");
    q.clearance = param("targetClearance");
    q.pick = Pick::Nearest;
    return q;
}

ScenarioDesc abduction(const Plan& p) {
    const SetPieceSpec& spec = *p.spec;
    const bool region = spec.place.kind == SetPiecePlace::Kind::Region;
    const int n = p.animals;
    const bool giveBack = v(spec, "returnSeconds") > 0.0f;
    std::vector<BeatDesc> beats = opening(p, p.timeline.entry, "entryHeight", true);
    if (region) {
        // The subject first, so the craft can come in on it: the nearest animal to the region's
        // centre with clear air above it. None there, and the set piece does not happen -- the
        // scenario ends with the craft never shown, which the trace reports.
        QueryDesc first;
        first.bind = targetRole(0);
        if (!spec.animals.empty()) {
            first.name = spec.animals.front();
        } else {
            first.tag = spec.tag;
            first.center = glm::vec3(spec.place.point.x, spec.groundAtPlace, spec.place.point.y);
            first.radius = literal(spec.place.radius);
            first.clearance = param("targetClearance");
        }
        beats.back().find.push_back(std::move(first));
    }
    // Arrival: shown, and flown in to the station -- the point, or over the subject it tracks.
    StepDesc in = moveTo("approach", p.timeline.station, "hoverHeight", "approachSeconds");
    in.clearance = param("cruiseClearance");
    if (region) {
        in.point = glm::vec3(0.0f);
        in.toRole = targetRole(0);
    }
    beats.push_back(beat("approach", {cue("actor", {step(StepKind::Show, "seen"), in})}));

    // The hover: nothing runs until the craft has actually stopped (ADR-385). The animals are taken
    // here, at arrival, so the ones lifted are the ones under the craft rather than the ones that
    // were there when it set off.
    BeatDesc hover = beat("hover", {cue("actor", {hold("hover", "hoverSeconds", true)})});
    hover.stillRoles = {"actor"};
    for (int k = region ? 1 : 0; k < n; ++k) {
        hover.find.push_back(gather(spec, k, region));
    }
    for (int k = 0; k < n; ++k) {
        hover.cues.push_back(cue(targetRole(k), {hold("caught", "hoverSeconds", false)}));
    }
    hover.otherwise = "depart"; // nobody under it: it leaves without beaming
    beats.push_back(std::move(hover));

    BeatDesc beam = beat("beam", beamRising());
    for (int k = 0; k < n; ++k) {
        beam.cues.push_back(cue(targetRole(k), {hold("held", "beamSeconds", false)}));
    }
    beats.push_back(std::move(beam));

    // The lift: every animal at once, one cue each, to its own place in the column -- spread round
    // the axis and stepped in height, so two bodies never rise through each other.
    BeatDesc lift = beat("lift", {cue("actor", {hold("hold", "liftSeconds", true)})});
    const float radius = n > 1 ? v(spec, "stackRadius") : 0.0f;
    const float stagger = v(spec, "stackStagger");
    for (int k = 0; k < n; ++k) {
        const float angle = (2.0f * std::numbers::pi_v<float> * static_cast<float>(k)) / static_cast<float>(std::max(n, 1));
        StepDesc up = step(StepKind::MoveTo, "lift");
        up.toRole = "actor.beam";
        up.anchor = Anchor::Drawn;
        up.place = Anchor::Drawn;
        up.point = glm::vec3(radius * std::cos(angle), -stagger * static_cast<float>(k), radius * std::sin(angle));
        up.height = param("liftHeight");
        up.duration = param("liftSeconds");
        up.spin = param("animalSpin");
        up.wobble = param("animalWobble");
        up.wobbleRate = param("animalWobbleRate");
        up.rate = param("animalGait");
        lift.cues.push_back(cue(targetRole(k), {up}));
        StepDesc rise = step(StepKind::Wait, "rise");
        rise.duration = param("fadeDelaySeconds");
        StepDesc fade = setTo("fade", "opacity", literal(0.0f), param("fadeSeconds"));
        fade.from = literal(1.0f);
        fade.hasFrom = true;
        fade.ease = true;
        // ADR-984: a subject the set piece gives back dissolves and waits, unseen, to be put back as
        // the craft leaves; one it keeps is retired.
        if (giveBack) {
            lift.cues.push_back(cue(targetRole(k), {rise, fade}));
        } else {
            lift.cues.push_back(cue(targetRole(k), {rise, fade, step(StepKind::Retire, "vanish")}));
        }
    }
    beats.push_back(std::move(lift));
    beats.push_back(departing(p, giveBack ? n : 0));
    clock(beats, p);

    ScenarioDesc s;
    s.name = setPieceScenarioName(spec.id);
    s.actor = spec.actor;
    s.seed = spec.seed;
    s.autoStart = true;
    s.maxCycles = 1;
    s.params = parametersOf(p, beats);
    s.beats = std::move(beats);
    return s;
}

ScenarioDesc survey(const Plan& p) {
    const SetPieceSpec& spec = *p.spec;
    std::vector<BeatDesc> beats = opening(p, p.timeline.entry, "entryHeight", true);
    StepDesc in = moveTo("approach", p.timeline.station, "hoverHeight", "approachSeconds");
    in.clearance = param("cruiseClearance");
    beats.push_back(beat("approach", {cue("actor", {step(StepKind::Show, "seen"), in})}));
    BeatDesc hover = beat("hover", {cue("actor", {hold("hover", "hoverSeconds", true)})});
    hover.stillRoles = {"actor"}; // the beam lights under a craft that has stopped, as a lift's does
    beats.push_back(std::move(hover));
    beats.push_back(beat("beam", beamRising()));
    // The sweep: the craft glides across the field with the beam lit. The one set piece that moves
    // under a lit beam, on purpose -- it lifts nothing, and a scan that stood still would not sweep.
    StepDesc across = moveTo("sweep", p.sweepEnd, "hoverHeight", "sweepSeconds");
    across.wobble = param("craftWobble");
    across.wobbleRate = param("craftWobbleRate");
    beats.push_back(beat("sweep", {cue("actor", {across})}));
    beats.push_back(departing(p));
    clock(beats, p);

    ScenarioDesc s;
    s.name = setPieceScenarioName(spec.id);
    s.actor = spec.actor;
    s.seed = spec.seed;
    s.autoStart = true;
    s.maxCycles = 1;
    s.params = parametersOf(p, beats);
    s.beats = std::move(beats);
    return s;
}

ScenarioDesc flyby(const Plan& p, bool beamPart) {
    const SetPieceSpec& spec = *p.spec;
    std::vector<BeatDesc> beats = opening(p, p.timeline.entry, "altitude", beamPart);
    StepDesc across = moveTo("cross", p.timeline.exit, "altitude", "crossSeconds");
    across.clearance = param("cruiseClearance");
    beats.push_back(beat("cross", {cue("actor", {step(StepKind::Show, "seen"), across})}));
    beats.push_back(beat("depart", {cue("actor", {step(StepKind::Hide, "gone")})}));
    clock(beats, p);

    ScenarioDesc s;
    s.name = setPieceScenarioName(spec.id);
    s.actor = spec.actor;
    s.seed = spec.seed;
    s.autoStart = true;
    s.maxCycles = 1;
    s.params = parametersOf(p, beats);
    s.beats = std::move(beats);
    return s;
}

} // namespace

// ---- names ----------------------------------------------------------------------------------------

const char* setPieceKindName(SetPieceKind kind) {
    switch (kind) {
    case SetPieceKind::Abduction: return "abduction";
    case SetPieceKind::Survey: return "survey";
    case SetPieceKind::Flyby: return "flyby";
    }
    return "?";
}

std::optional<SetPieceKind> setPieceKindFromName(std::string_view name) {
    if (name == "abduction") return SetPieceKind::Abduction;
    if (name == "survey") return SetPieceKind::Survey;
    if (name == "flyby") return SetPieceKind::Flyby;
    return std::nullopt;
}

std::vector<std::string> setPieceKindNames() { return {"abduction", "survey", "flyby"}; }

std::span<const SetPieceSlot> setPieceSlots(SetPieceKind kind) {
    switch (kind) {
    case SetPieceKind::Abduction: return kAbduction;
    case SetPieceKind::Survey: return kSurvey;
    case SetPieceKind::Flyby: return kFlyby;
    }
    return {};
}

const SetPieceSlot* findSetPieceSlot(SetPieceKind kind, std::string_view name) {
    for (const SetPieceSlot& slot : setPieceSlots(kind)) {
        if (name == slot.name) {
            return &slot;
        }
    }
    return nullptr;
}

std::string setPieceSlotLabel(const SetPieceSlot& slot) {
    const std::string_view unit = slot.unit != nullptr ? std::string_view(slot.unit) : std::string_view();
    return unit.empty() ? std::string(slot.label) : fmt::format("{} ({})", slot.label, unit);
}

std::vector<std::string> setPieceSlotNames(SetPieceKind kind) {
    std::vector<std::string> out;
    for (const SetPieceSlot& slot : setPieceSlots(kind)) {
        out.emplace_back(slot.name);
    }
    return out;
}

std::vector<std::string> setPieceMoments(SetPieceKind kind) {
    switch (kind) {
    case SetPieceKind::Abduction: return {"approach", "beam", "lift", "depart"};
    case SetPieceKind::Survey: return {"approach", "beam", "sweep", "depart"};
    case SetPieceKind::Flyby: return {"cross", "depart"};
    }
    return {};
}

std::string defaultSetPieceMoment(SetPieceKind kind) {
    return kind == SetPieceKind::Flyby ? std::string("cross") : std::string("beam");
}

std::string setPieceScenarioName(std::string_view id) { return fmt::format("setpiece/{}", id); }

bool isSetPieceScenario(std::string_view scenario) { return scenario.starts_with("setpiece/") && scenario.size() > 9; }

std::string setPieceIdOf(std::string_view scenario) {
    return isSetPieceScenario(scenario) ? std::string(scenario.substr(9)) : std::string();
}

glm::vec2 bearingDirection(float degrees) {
    const float a = degrees * kDegreesToRadians;
    return {std::sin(a), -std::cos(a)};
}

std::optional<double> SetPieceTimeline::at(std::string_view moment) const {
    for (const auto& [name, t] : moments) {
        if (name == moment) {
            return t;
        }
    }
    return std::nullopt;
}

// ---- values ---------------------------------------------------------------------------------------

float setPieceValue(const SetPieceSpec& spec, std::string_view slot) {
    for (auto it = spec.overrides.rbegin(); it != spec.overrides.rend(); ++it) {
        if (it->first == slot) {
            return it->second;
        }
    }
    const SetPieceSlot* s = findSetPieceSlot(spec.kind, slot);
    return s != nullptr ? s->value : 0.0f;
}

int setPieceAnimalCount(const SetPieceSpec& spec) {
    if (spec.kind != SetPieceKind::Abduction) {
        return 0;
    }
    if (!spec.animals.empty()) {
        return static_cast<int>(spec.animals.size());
    }
    return static_cast<int>(std::lround(setPieceValue(spec, "animals")));
}

Result<void> validateSetPieceSpec(const SetPieceSpec& spec) {
    const char* kind = setPieceKindName(spec.kind);
    if (spec.id.empty()) {
        return fail("a set piece needs an id");
    }
    // The id is a path segment twice over -- `staging/setpiece/<id>/<slot>` and the world event
    // `setpiece/<id>/<moment>` -- so a slash or a space in it would make a name nothing could parse.
    if (spec.id.find_first_of("/ \t\n") != std::string::npos) {
        return fail("set piece '{}': an id may not contain '/' or whitespace", spec.id);
    }
    if (spec.actor.empty()) {
        return fail("set piece '{}': which craft plays it? (`actor` is empty)", spec.id);
    }
    if (!std::isfinite(spec.atSeconds)) {
        return fail("set piece '{}': its time is not a number", spec.id);
    }
    if (!(spec.frameSeconds > 0.0)) {
        return fail("set piece '{}': the frame must be positive", spec.id);
    }
    const std::string moment = spec.moment.empty() ? defaultSetPieceMoment(spec.kind) : spec.moment;
    const std::vector<std::string> moments = setPieceMoments(spec.kind);
    if (std::find(moments.begin(), moments.end(), moment) == moments.end()) {
        return fail("set piece '{}': a {} has no moment '{}' (it has {})", spec.id, kind, moment,
                    fmt::join(moments, ", "));
    }
    for (const auto& [name, value] : spec.overrides) {
        const SetPieceSlot* slot = findSetPieceSlot(spec.kind, name);
        if (slot == nullptr) {
            return fail("set piece '{}': a {} has no slot '{}'", spec.id, kind, name);
        }
        if (!std::isfinite(value) || value < slot->min || value > slot->max) {
            return fail("set piece '{}': {} = {} is outside {}'s range {}..{}", spec.id, name, value, kind, slot->min,
                        slot->max);
        }
    }
    if (spec.kind != SetPieceKind::Abduction && !spec.animals.empty()) {
        return fail("set piece '{}': only an abduction takes animals", spec.id);
    }
    if (spec.kind == SetPieceKind::Abduction) {
        const auto slotted = static_cast<int>(std::lround(setPieceValue(spec, "animals")));
        const bool overridden = std::any_of(spec.overrides.begin(), spec.overrides.end(),
                                            [](const auto& o) { return o.first == "animals"; });
        if (!spec.animals.empty() && (spec.animals.size() > 3 || (overridden && slotted != static_cast<int>(spec.animals.size())))) {
            return fail("set piece '{}': {} animal(s) are named and {} asked for; an abduction lifts 1 to 3", spec.id,
                        spec.animals.size(), slotted);
        }
    }
    if (spec.kind == SetPieceKind::Flyby && spec.beamColor) {
        return fail("set piece '{}': a flyby shows no beam, so it has no beam colour", spec.id);
    }
    if (spec.kind != SetPieceKind::Flyby && !spec.beamPart) {
        return fail("set piece '{}': a {} lights the craft's beam, and craft '{}' has no part called 'beam'", spec.id,
                    kind, spec.actor);
    }
    if (spec.beamColor && (!std::isfinite(spec.beamColor->r) || !std::isfinite(spec.beamColor->g) ||
                           !std::isfinite(spec.beamColor->b) || spec.beamColor->r < 0.0f || spec.beamColor->g < 0.0f ||
                           spec.beamColor->b < 0.0f)) {
        return fail("set piece '{}': a beam colour is three non-negative numbers", spec.id);
    }
    if (spec.place.kind == SetPiecePlace::Kind::Region && !(spec.place.radius > 0.0f)) {
        return fail("set piece '{}': a region needs a radius", spec.id);
    }
    if (spec.place.kind == SetPiecePlace::Kind::Region && spec.kind != SetPieceKind::Abduction) {
        return fail("set piece '{}': only an abduction searches a region for its subject; a {} works over a point",
                    spec.id, kind);
    }
    return {};
}

Result<SetPieceTimeline> setPieceTimeline(const SetPieceSpec& spec) {
    auto p = plan(spec);
    if (!p) {
        return std::unexpected(p.error());
    }
    return p->timeline;
}

Result<ScenarioDesc> instanceSetPiece(const SetPieceSpec& spec) {
    auto p = plan(spec);
    if (!p) {
        return std::unexpected(p.error());
    }
    // A clock before the film starts is a set piece that would have had to begin before t = 0: the
    // scenario would run it late and out of step, so it is refused rather than clamped.
    if (p->transitAt < 0.0) {
        return fail("set piece '{}': placing its {} at {:.3f}s needs the craft from {:.3f}s, before the film starts",
                    spec.id, p->moment, spec.atSeconds, p->transitAt);
    }
    switch (spec.kind) {
    case SetPieceKind::Abduction: return abduction(*p);
    case SetPieceKind::Survey: return survey(*p);
    case SetPieceKind::Flyby: return flyby(*p, spec.beamPart);
    }
    return fail("set piece '{}': unknown template", spec.id);
}

} // namespace avgen::stage
