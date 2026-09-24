// Pulse (Effect Library, catalog-light.md): a periodic swell in the owner's emission.
//
// **What it is.** A targeted LFO that belongs to its owner, in two modes:
//
//   * **Whole** -- the entity's whole emission swells and falls. The CPU evaluates the waveform at
//     `t * rate + phase` and multiplies the owner's lane gain by `peak * (1 - depth + depth * w)`.
//   * **Travelling** -- a bright band moves along one of the owner's axes (the generalisation of
//     ADR-376's tree energy). The gain is the peak; the shader evaluates the band per fragment from
//     the record's band sub-block, whose cross-section is one cycle of the same waveform. The band
//     is an exclusive sub-block: a second travelling Pulse on one owner is `Dropped` by name.
//
// It MULTIPLIES (rendering-architecture §7): a Pulse on a Glowing entity pulses the glow, its rim and
// its spill light. On an entity with neither an emissive material nor a Glow there is no light to
// pulse, and it changes nothing -- which is what "a pulse of the owner's emission" means.
//
// **Stateless, so seek-exact.** The phase is `local * rate + phase` with no accumulator (`local` is
// the seconds since the activation window opened), so second N is the same played or scrubbed
// (ADR-091). The price, documented rather than hidden: a MODULATED rate jumps the phase. To lock to
// the music, route `beat.phase` onto Phase with Rate 0; the pulse then follows the beat clock.
//
// **Entity only.** The catalog also offers Pulse on a Light, through LIGHTMOD multiplying the light's
// record. That needs the Lighting stage to own authored lights' finals each frame, which nothing
// in this package does; it is left for the package that gives LIGHTMOD its modulate half, rather
// than faked here by writing into the scene's light list from an evaluator.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* const kModeNames[] = {"Whole", "Travelling"};
constexpr const char* const kWaveNames[] = {"Sine", "Triangle", "Square", "Saw", "Heartbeat"};
static_assert(std::size(kWaveNames) == kFxWaveformCount, "the Waveform row and FxWaveform must agree");
constexpr const char* const kAxisNames[] = {"Up", "Forward", "Right"};

constexpr float kRate = 0.5f;
constexpr float kPeak = 3.0f;
constexpr float kDepth = 0.85f;
constexpr float kPhase = 0.0f;
constexpr float kSharpness = 1.0f;
constexpr float kBandWidth = 0.15f;

constexpr EffectField kFields[] = {
    storedChoice("mode", "Mode", 0, kModeNames).main()
        .tooltip("Whole: the entity's light swells and falls as one.\n"
                 "Travelling: a bright band moves along the entity."),
    storedChoice("waveform", "Waveform", 0, kWaveNames).main()
        .tooltip("The shape of one cycle. In Travelling mode, the shape of the band."),
    storedFloat("rate", "Rate", kRate, -20.0f, 20.0f, 0.0f, 4.0f).fmt("%.2f Hz").main()
        .tooltip("Cycles per second. Negative runs a travelling band the other way. For a\n"
                 "pulse locked to the music, set 0 and route beat.phase to Phase."),
    storedFloat("peak", "Brightness", kPeak, 0.0f, 50.0f, 0.0f, 10.0f).main().floorAt(0.0f)
        .tooltip("How bright the entity's light is at the crest, as a multiple of what it\n"
                 "would be without the pulse. Multiplies a Glow on the same entity."),
    storedFloat("depth", "Depth", kDepth, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How far it falls between crests. 1 goes dark; 0 does not pulse at all."),
    storedFloat("phase", "Phase", kPhase, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("Where in its cycle the pulse starts. Two pulses half a cycle apart\n"
                 "alternate. Route beat.phase here (with Rate 0) to lock it to the beat."),
    storedFloat("sharpness", "Sharpness", kSharpness, 0.25f, 8.0f, 0.25f, 4.0f).floorAt(0.05f)
        .tooltip("Whole mode: raises the waveform to this power. Above 1 the crests get\n"
                 "shorter and the rests longer; below 1 the reverse."),
    storedChoice("axis", "Axis", 0, kAxisNames).sec("Travelling band")
        .tooltip("Which of the entity's own axes the band travels along."),
    storedFloat("bandWidth", "Band width", kBandWidth, 0.01f, 1.0f, 0.02f, 0.5f).fmt("%.2f")
        .tooltip("Half the band's width, as a fraction of the entity's length along the axis."),
};

// ---- styles ------------------------------------------------------------------------------------

void set(E& e, int mode, FxWaveform wave, float rate, float peak, float depth, float sharpness, int axis,
         float width) {
    e.values.setFloat("pulse/mode", static_cast<float>(mode));
    e.values.setFloat("pulse/waveform", static_cast<float>(wave));
    e.values.setFloat("pulse/rate", rate);
    e.values.setFloat("pulse/peak", peak);
    e.values.setFloat("pulse/depth", depth);
    e.values.setFloat("pulse/sharpness", sharpness);
    e.values.setFloat("pulse/axis", static_cast<float>(axis));
    e.values.setFloat("pulse/bandWidth", width);
}

constexpr const char* kStyleNames[] = {"Heartbeat", "Breathing", "Beat Strobe", "Energy Climb"};

void heartbeat(E& e) {
    set(e, 0, FxWaveform::Heartbeat, 1.1f, 4.0f, 0.9f, 1.0f, 0, kBandWidth);
    e.style = kStyleNames[0];
}
void breathing(E& e) {
    set(e, 0, FxWaveform::Sine, 0.2f, 2.5f, 0.7f, 1.0f, 0, kBandWidth);
    e.style = kStyleNames[1];
}
void beatStrobe(E& e) {
    set(e, 0, FxWaveform::Square, 2.0f, 6.0f, 1.0f, 1.0f, 0, kBandWidth);
    e.style = kStyleNames[2];
}
void energyClimb(E& e) {
    set(e, 1, FxWaveform::Sine, 0.6f, 4.0f, 0.9f, 1.0f, 0, 0.18f);
    e.style = kStyleNames[3];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], heartbeat},
    {kStyleNames[1], breathing},
    {kStyleNames[2], beatStrobe},
    {kStyleNames[3], energyClimb},
};

// The kick lifts the crest: the pulse keeps its own clock and the music pushes its brightness.
constexpr EffectRoute kRoutes[] = {
    {"beat.pulse", "peak", 1.5f, 10.0f, 240.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Pulse;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.25;
    e.timing.fadeOut = 0.5;
    set(e, 0, FxWaveform::Sine, kRate, kPeak, kDepth, kSharpness, 0, kBandWidth);
    return e;
}

// ---- resolve -----------------------------------------------------------------------------------

int indexOf(const E& e, const char* key, int count) {
    return std::clamp(static_cast<int>(e.values.getFloat(key, 0.0f) + 0.5f), 0, count - 1);
}

bool lanes(const E& e, const EffectContext&, const NodeView& view, double local, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    const auto wave = static_cast<FxWaveform>(indexOf(e, "pulse/waveform", static_cast<int>(kFxWaveformCount)));
    const bool travelling = indexOf(e, "pulse/mode", 2) == 1;
    const float rate = v.getFloat("pulse/rate", kRate);
    const float peak = std::max(v.getFloat("pulse/peak", kPeak), 0.0f);
    const float depth = std::clamp(v.getFloat("pulse/depth", kDepth), 0.0f, 1.0f);
    const float phase = v.getFloat("pulse/phase", kPhase);
    // The cycle in double: `local * rate` at an hour in is ~10^4 cycles, and float would lose the
    // fraction that is the whole of the phase.
    const double cycleD = local * static_cast<double>(rate) + static_cast<double>(phase);
    const auto cycle = static_cast<float>(cycleD - std::floor(cycleD));

    if (!travelling) {
        const float sharp = std::max(v.getFloat("pulse/sharpness", kSharpness), 0.05f);
        const float w = std::pow(std::clamp(pulseWave(wave, cycle), 0.0f, 1.0f), sharp);
        c.gain = peak * (1.0f - depth + depth * w);
        return true;
    }

    // Travelling: the band runs along one of the owner's own axes, across its drawn extent.
    const int axis = indexOf(e, "pulse/axis", 3);
    glm::vec3 dir = axis == 0 ? glm::vec3(view.world[1]) : axis == 1 ? -glm::vec3(view.world[2]) : glm::vec3(view.world[0]);
    if (!(glm::dot(dir, dir) > 1e-12f)) {
        dir = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    dir = glm::normalize(dir);
    float u0 = 0.0f;
    float u1 = 1.0f;
    if (view.hasBounds) {
        u0 = 3.0e38f;
        u1 = -3.0e38f;
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p((corner & 1) ? view.boundsMax.x : view.boundsMin.x,
                              (corner & 2) ? view.boundsMax.y : view.boundsMin.y,
                              (corner & 4) ? view.boundsMax.z : view.boundsMin.z);
            const float u = glm::dot(p, dir);
            u0 = std::min(u0, u);
            u1 = std::max(u1, u);
        }
    } else {
        const float centre = glm::dot(glm::vec3(view.world[3]), dir);
        u0 = centre - 1.0f;
        u1 = centre + 1.0f;
    }
    const float half = std::clamp(v.getFloat("pulse/bandWidth", kBandWidth), 0.01f, 1.0f);
    c.gain = peak;
    c.hasBand = true;
    c.band.axis = dir;
    c.band.u0 = u0;
    c.band.u1 = std::max(u1, u0 + 1e-3f);
    // The centre sweeps from one band-width before the start to one after the end, so the band
    // enters and leaves the body rather than appearing on it. A negative rate runs it backwards.
    c.band.centre = -half + cycle * (1.0f + 2.0f * half);
    if (rate < 0.0f) {
        c.band.centre = 1.0f + half - cycle * (1.0f + 2.0f * half);
    }
    c.band.halfWidth = half;
    c.band.waveform = wave;
    c.band.depth = depth;
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Pulse;
    s.key = "pulse";
    s.enumName = "Pulse";
    s.displayName = "Pulse";
    s.description = "The entity's light swells and falls on a waveform -- as a whole, or as a bright band "
                    "travelling along it. Multiplies the entity's own emission and any Glow on it.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostFragment;
    s.addLabel = "Pulse";
    s.addTip = "This entity's light swells and falls: a heartbeat, a breath, a strobe, or a band\n"
               "of light climbing it. Pulses its own emission and any Glow on it.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "peak";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& pulseSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
