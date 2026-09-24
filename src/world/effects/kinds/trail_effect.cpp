// Trail (Effect Library Wave 1, docs/design/effect-library/catalog-motion.md "Trail"; Light Trail,
// catalog-light.md, is a style of it): a persistent streak the owner leaves behind as it moves.
//
// **How it is drawn.** RIBBON over HIST. The owner's transform history (world/effects/history_bank)
// is read inside `length` seconds, resampled at fixed instants on the timeline, Catmull-Rom
// subdivided, and written as a camera-facing strip (world/effects/ribbon_frame). Width tapers with
// age, colour runs from `color` at the head to `tailColor` at the tail, opacity falls as
// `(1 - age)^fade`. The head is the owner's CURRENT drawn transform, so the strip never lags its
// owner by a frame.
//
// **Why the resample instants are fixed on the timeline and not relative to "now".** A strip whose
// control points sat at `now - k*step` would re-sample a different point of the history every
// frame, and the whole ribbon would swim along its own length. Control points at multiples of the
// step are the SAME world points from one frame to the next; only the head and the cut at the tail
// move. That is what "no flicker between frames" costs, and it costs nothing.
//
// **Modes.** Ribbon: a soft-edged strip, additive or alpha. Light Trail: an additive white-hot core
// with a Gaussian glow across the full width, the long-exposure look.
//
// **Determinism.** Stateless given HIST, and HIST is checkpointed (ADR-700), so a scrub to second N
// draws the trail a play to N draws. Activation and timing are honoured: the envelope scales the
// opacity, and the tail is cut at the instant the activation opened, so a trail that switches on
// grows out of its owner rather than appearing full-length.

#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_timing.hpp"
#include "world/effects/history_bank.hpp"
#include "world/effects/ribbon_frame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kKey = "trail";

constexpr const char* kModeNames[] = {"Ribbon", "Light Trail"};
constexpr const char* kBlendNames[] = {"Additive", "Alpha"};

// Row indices, so the reader and the rows cannot disagree about a default.
enum Row : std::size_t { Mode, Length, Width, Taper, Color, TailColor, Intensity, Opacity, Fade, Core, Glow,
                         CoreBoost, Blend, Smoothing, RowCount };

constexpr EffectField kFields[] = {
    storedChoice("mode", "Mode", 0, kModeNames).main()
        .tooltip("Ribbon: a soft-edged strip.\nLight Trail: a white-hot core in a Gaussian glow, the "
                 "long-exposure look."),
    storedFloat("length", "Length", 1.2f, 0.05f, 8.0f, 0.1f, 4.0f).fmt("%.2f s").main().floorAt(0.05f)
        .tooltip("How many seconds of the owner's path the trail shows."),
    storedFloat("width", "Width", 0.5f, 0.005f, 40.0f, 0.02f, 6.0f).fmt("%.2f m").main().log().floorAt(0.0f)
        .tooltip("Width at the head, in metres. Never drawn narrower than 1.5 pixels: a thinner trail "
                 "fades instead of breaking up."),
    storedFloat("taper", "Taper", 0.85f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How much narrower the tail is than the head: 0 keeps the width, 1 ends in a point."),
    storedColor("color", "Colour", glm::vec3(0.35f, 0.85f, 1.0f)).main(),
    storedColor("tailColor", "Tail colour", glm::vec3(0.55f, 0.2f, 1.0f)).main(),
    storedFloat("intensity", "Intensity", 3.0f, 0.0f, 200.0f, 0.0f, 20.0f).main().floorAt(0.0f)
        .tooltip("HDR brightness. Above about 1 the trail blooms."),
    storedFloat("opacity", "Opacity", 0.6f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("The head's opacity. The default route adds the owner's speed, so a trail is\n"
                 "strongest when its owner is fastest."),
    storedFloat("fade", "Fade", 1.5f, 0.1f, 8.0f, 0.25f, 4.0f).sec("Shape").floorAt(0.05f)
        .tooltip("How the opacity falls with age: 1 is linear, higher keeps the head bright and\n"
                 "lets the tail go sooner."),
    storedFloat("core", "Core width", 0.22f, 0.02f, 1.0f, 0.05f, 1.0f)
        .tooltip("Light Trail: the hot core's width, as a fraction of the whole."),
    storedFloat("glow", "Glow", 1.0f, 0.0f, 8.0f, 0.0f, 3.0f).floorAt(0.0f)
        .tooltip("Light Trail: the halo's brightness relative to the colour."),
    storedFloat("coreBoost", "Core brightness", 2.5f, 0.0f, 20.0f, 0.0f, 6.0f).floorAt(0.0f)
        .tooltip("Light Trail: how much hotter the core is than the halo."),
    storedChoice("blend", "Blend", 0, kBlendNames)
        .tooltip("Additive adds light (a glowing trail). Alpha covers what is behind (a smoke or\n"
                 "paint trail). A Light Trail is always additive."),
    storedFloat("smoothing", "Smoothing", 3.0f, 0.0f, 8.0f, 0.0f, 6.0f).fmt("%.0f").clampTo(0.0f, 8.0f)
        .tooltip("Catmull-Rom subdivisions between the trail's control points. More is smoother on\n"
                 "tight curves and costs vertices."),
};
static_assert(std::size(kFields) == RowCount);

// ---- reading and writing the store --------------------------------------------------------------
//
// Through a stack buffer rather than `storeKey`, so the per-frame read allocates nothing.

struct Key {
    std::array<char, 48> buf{};
    std::size_t len = 0;
    explicit Key(const char* leaf) {
        const std::size_t k = std::strlen(kKey);
        const std::size_t l = std::min(std::strlen(leaf), buf.size() - k - 1);
        std::memcpy(buf.data(), kKey, k);
        buf[k] = '/';
        std::memcpy(buf.data() + k + 1, leaf, l);
        len = k + 1 + l;
    }
    [[nodiscard]] std::string_view view() const { return {buf.data(), len}; }
};

float get(const E& e, Row row) {
    return e.values.getFloat(Key(kFields[row].leaf).view(), kFields[row].storedDefault);
}
glm::vec3 getColor(const E& e, Row row) {
    return e.values.getColor(Key(kFields[row].leaf).view(), kFields[row].storedColor);
}
int getChoice(const E& e, Row row) {
    return std::clamp(static_cast<int>(get(e, row) + 0.5f), 0, kFields[row].choiceCount - 1);
}
void set(E& e, Row row, float v) { e.values.setFloat(Key(kFields[row].leaf).view(), v); }
void setColor(E& e, Row row, glm::vec3 v) { e.values.setColor(Key(kFields[row].leaf).view(), v); }

// ---- styles -------------------------------------------------------------------------------------

constexpr const char* kStyleNames[] = {"UFO Wake", "Light Trail", "Comet Tail", "Subtle"};

// Every style writes every row, so switching styles never leaves the last one's settings behind.
void applyStyle(E& e, int which) {
    struct S {
        float mode, length, width, taper;
        glm::vec3 color, tail;
        float intensity, opacity, fade, core, glow, boost, blend, smoothing;
    };
    static constexpr S kS[] = {
        // UFO Wake: a broad luminous wake, cyan to violet, long enough to read the path it flew.
        {1.0f, 2.4f, 3.2f, 0.9f, {0.30f, 1.0f, 0.85f}, {0.45f, 0.25f, 1.0f}, 5.0f, 0.7f, 1.6f, 0.16f, 1.2f,
         3.0f, 0.0f, 3.0f},
        // Light Trail (Long Exposure): narrow, hot, persistent, the streak a lamp leaves on film.
        {1.0f, 3.0f, 0.6f, 0.3f, {1.0f, 0.82f, 0.55f}, {1.0f, 0.35f, 0.15f}, 8.0f, 0.9f, 1.1f, 0.18f, 1.3f,
         3.5f, 0.0f, 3.0f},
        // Comet Tail: short and fat, tapering to a point, fading hard.
        {0.0f, 1.4f, 1.6f, 1.0f, {1.0f, 0.75f, 0.35f}, {0.9f, 0.18f, 0.05f}, 4.0f, 0.85f, 2.2f, 0.55f, 0.0f,
         1.0f, 0.0f, 3.0f},
        // Subtle: a faint, soft, short smear that marks motion without drawing attention -- laid over
        // the scene (alpha) rather than added to it, so it reads as a wake of air, not as light.
        {0.0f, 0.8f, 0.35f, 0.7f, {0.85f, 0.92f, 1.0f}, {0.6f, 0.7f, 1.0f}, 1.0f, 0.3f, 1.4f, 0.5f, 0.0f,
         1.0f, 1.0f, 2.0f},
    };
    const S& s = kS[which];
    set(e, Mode, s.mode);
    set(e, Length, s.length);
    set(e, Width, s.width);
    set(e, Taper, s.taper);
    setColor(e, Color, s.color);
    setColor(e, TailColor, s.tail);
    set(e, Intensity, s.intensity);
    set(e, Opacity, s.opacity);
    set(e, Fade, s.fade);
    set(e, Core, s.core);
    set(e, Glow, s.glow);
    set(e, CoreBoost, s.boost);
    set(e, Blend, s.blend);
    set(e, Smoothing, s.smoothing);
    e.style = kStyleNames[which];
}
void style0(E& e) { applyStyle(e, 0); }
void style1(E& e) { applyStyle(e, 1); }
void style2(E& e) { applyStyle(e, 2); }
void style3(E& e) { applyStyle(e, 3); }

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], style0}, {kStyleNames[1], style1}, {kStyleNames[2], style2}, {kStyleNames[3], style3},
};

// The owner's speed brings the trail up (`owner.` resolves to the owner's `entity.<name>.speed`
// when the routes are attached; see effect_params.hpp). 0.15 per m/s: a trail at the authored 0.6
// is at full opacity from 2.7 m/s, and the decay lets it linger as the owner brakes. The treble
// sparkles along it, as the catalogue's modulation table asks.
constexpr EffectRoute kRoutes[] = {
    {"owner.speed", "opacity", 0.15f, 60.0f, 500.0f},
    {"audio.treble", "intensity", 1.5f, 15.0f, 220.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Trail;
    e.activation = Activation::Always;
    e.timing = Timing{};
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 0.8;
    applyStyle(e, 0);
    return e;
}

// ---- resolution ---------------------------------------------------------------------------------

struct Live {
    double start = 0.0;   // the instant the activation (plus delay) opened: the tail is cut here
    float envelope = 0.0f;
};

// Is this instance on at all, and how strongly. The ONE gate both `records` and `emit` go through,
// so the conformance probe asks the same question the builder answers.
bool live(const E& e, const EffectContext& ctx, Live& out) {
    if (!e.enabled || e.owner.kind != EffectTarget::Entity || e.owner.name.empty()) {
        return false;
    }
    const auto window = resolveActivationWindow(e.activation, e.timing, ctx.seconds, ctx.shots, false,
                                                e.owner.name);
    if (!window) {
        return false;
    }
    const double local = ctx.seconds - window->start - e.timing.delay;
    const float envelope = timingEnvelope(e.timing, local, window->end - window->start - e.timing.delay);
    if (envelope <= 1e-4f) {
        return false;
    }
    if (get(e, Width) <= 0.0f || get(e, Intensity) <= 0.0f || get(e, Opacity) <= 0.0f) {
        return false;
    }
    out.start = window->start + e.timing.delay;
    out.envelope = envelope;
    return true;
}

std::size_t records(const E& e, const EffectContext& ctx) {
    Live l;
    if (!live(e, ctx, l)) {
        return 0;
    }
    NodeView view;
    return ctx.scene != nullptr && ctx.scene->nodeView(e.owner.name, view) ? 1u : 0u;
}

float historySeconds(const E& e) {
    // Two grid steps past the length: the tail's cut is interpolated, so it needs the sample
    // before it too.
    return get(e, Length) + static_cast<float>(2.0 * HistoryBank::kGridStep);
}

// At most this many control points, however long the trail: the Catmull-Rom does the rest, and it
// keeps a four-second trail at 60 Hz from costing 480 vertices before any subdivision.
constexpr int kMaxControlPoints = 48;
// The fraction of the length over which the head's opacity comes up.
constexpr float kHeadSoftening = 0.08f;

EffectStatus emit(const E& e, const EffectContext& ctx, const HistoryBank& history, RibbonSink& sink,
                  std::string& reason) {
    Live l;
    if (!live(e, ctx, l)) {
        return EffectStatus::Dormant;
    }
    const std::size_t ring = history.find(e.owner.name);
    if (ring >= history.ringCount() || history.sampleCount(ring) == 0) {
        return EffectStatus::Dormant; // nothing recorded yet: the first frame, or not subscribed
    }
    const HistorySample& newest = history.sample(ring, history.sampleCount(ring) - 1);
    const HistorySample& oldest = history.sample(ring, 0);

    // The head: where the owner is DRAWN this frame. The ring's newest sample is the same instant
    // when the engine recorded before evaluating (it does); the view is preferred because it is
    // what the renderer draws the owner at.
    glm::vec3 head = newest.position;
    if (ctx.scene != nullptr) {
        NodeView view;
        if (ctx.scene->nodeView(e.owner.name, view)) {
            head = glm::vec3(view.world[3]);
        }
    }

    const float length = std::max(get(e, Length), 0.05f);
    const double now = std::max(ctx.seconds, newest.t);
    const double tail = std::max({now - static_cast<double>(length), l.start, oldest.t});
    if (now - tail <= 1e-6) {
        return EffectStatus::Dormant;
    }

    // Control instants: fixed multiples of `step` on the timeline, so the same world points are
    // re-used frame after frame (see the header), plus the head and the tail cut.
    const double grid = HistoryBank::kGridStep;
    const double wanted = static_cast<double>(length) / static_cast<double>(kMaxControlPoints - 2);
    const double step = std::max(grid, std::ceil(wanted / grid - 1e-9) * grid);

    const float width = get(e, Width);
    const float taper = std::clamp(get(e, Taper), 0.0f, 1.0f);
    const glm::vec3 color = getColor(e, Color);
    const glm::vec3 tailColor = getColor(e, TailColor);
    const float intensity = get(e, Intensity);
    const float opacity = std::clamp(get(e, Opacity), 0.0f, 1.0f) * l.envelope;
    const float fade = std::max(get(e, Fade), 0.05f);
    const auto point = [&](const glm::vec3& p, double t) {
        // Age as a fraction of the full length -- not of the shown span -- so a trail that has
        // only just switched on is its head, not a compressed copy of a whole trail.
        const float age = std::clamp(static_cast<float>((now - t) / static_cast<double>(length)), 0.0f, 1.0f);
        // The head is rounded over its first few percent -- narrower and fainter where it leaves the
        // owner -- so a wide strip does not start with a square-cut end at the owner's centre.
        const float h = std::clamp(age / kHeadSoftening, 0.0f, 1.0f);
        const float head = h * h * (3.0f - 2.0f * h);
        RibbonPoint r;
        r.position = p;
        r.width = width * (1.0f - taper * age) * (0.35f + 0.65f * head);
        r.color = glm::mix(color, tailColor, age) * intensity;
        r.opacity = opacity * std::pow(1.0f - age, fade) * (0.4f + 0.6f * head);
        return r;
    };

    // The body follows where the owner was DRAWN, not where it was simulated: HIST is the
    // pre-offset path (exact under seek), so an Orbit or Float on the owner is re-applied at each
    // sample instant. Without it the head (drawn) and the body (simulated) are joined by a streak.
    const auto drawn = [&](double at, const glm::vec3& simulated) {
        glm::vec3 p;
        return ctx.scene != nullptr && ctx.scene->nodeDrawnPosition(e.owner.name, at, p) ? p : simulated;
    };
    std::array<RibbonPoint, kMaxControlPoints + 2> points{};
    std::size_t n = 0;
    points[n++] = point(head, now);
    // The first fixed instant strictly before `now`, then every step back to the tail.
    double t = std::floor(now / step - 1e-9) * step;
    if (now - t < 1e-6) {
        t -= step;
    }
    for (; t > tail + 1e-9 && n + 1 < points.size(); t -= step) {
        HistorySample s;
        if (history.sampleAt(ring, t, s)) {
            points[n++] = point(drawn(t, s.position), t);
        }
    }
    HistorySample cut;
    if (history.sampleAt(ring, tail, cut)) {
        points[n++] = point(drawn(tail, cut.position), tail);
    }

    RibbonStyle style;
    const bool lightTrail = getChoice(e, Mode) == 1;
    if (lightTrail) {
        style.blend = RibbonBlend::Additive;
        style.coreFraction = std::clamp(get(e, Core), 0.02f, 1.0f);
        style.glow = std::max(get(e, Glow), 0.0f);
        style.coreBoost = std::max(get(e, CoreBoost), 0.0f);
        style.softEdge = false;
    } else {
        style.blend = getChoice(e, Blend) == 1 ? RibbonBlend::Alpha : RibbonBlend::Additive;
        style.coreFraction = 0.45f;
        style.glow = 0.0f;
        style.coreBoost = 1.0f;
        style.softEdge = true;
    }
    const int subdivisions = static_cast<int>(std::clamp(get(e, Smoothing), 0.0f, 8.0f) + 0.5f);
    switch (sink.appendStrip(std::span<const RibbonPoint>(points.data(), n), subdivisions, style)) {
    case RibbonFit::Written: return EffectStatus::Drawn;
    case RibbonFit::Reduced:
        reason = "The ribbon vertex budget (65536) was full: drawn at reduced detail.";
        return EffectStatus::Partial;
    case RibbonFit::NoFit:
        reason = "The ribbon vertex budget (65536) was full: not drawn.";
        return EffectStatus::Dropped;
    case RibbonFit::Nothing: return EffectStatus::Dormant; // the owner has not moved within `length`
    }
    return EffectStatus::Dormant;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Trail;
    s.key = kKey;
    s.enumName = "Trail";
    s.displayName = "Trail";
    s.description = "A persistent streak its owner leaves behind as it moves: a ribbon through the "
                    "owner's recent path that tapers, changes colour and fades with age. The Light "
                    "Trail mode is the long-exposure look, a white-hot core in a soft glow.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostCpu | CostVertex | CostFragment;
    s.addLabel = "Trail";
    s.addTip = "A streak through the path this entity has just flown or walked.\n"
               "Light Trail mode is the long-exposure look.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Motion;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "width";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Ribbon;
    s.resolve.records = records;
    // How this type becomes strips, registered with the Ribbon builder from here so the builder
    // names no type. Building the schema is what registers it, and every path to a Trail goes
    // through its schema first.
    registerRibbonProducer(EffectKind::Trail, RibbonProducer{emit, historySeconds});
    return s;
}

} // namespace

const EffectSchema& trailSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
