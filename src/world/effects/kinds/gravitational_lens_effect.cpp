// Gravitational Lens (Effect Library Wave 3, catalog-distortion.md "Gravitational Lens"): background
// light bent around a mass -- stars and far geometry stretched into arcs around an Einstein ring, a
// mirrored inner image, and in Black Hole mode an event-horizon disc ringed by a photon ring or an
// accretion glow.
//
// **What it is.** One DF producer, one proxy per instance: a sphere hull sampled on the plane through
// the mass FACING THE CAMERA (`DistortionShape::Facing`), carrying a thin-lens REMAP rather than an
// additive warp (`DistortionField::Lens`). A pixel at offset theta from the mass (metres in the lens
// plane) shows the background at
//
//     beta = theta (1 - theta_E^2 / (|theta|^2 + c^2)),      c = theta_E / 4,
//
// the point-mass lens equation (Refsdal; the mapping *Interstellar*'s lensing used, James et al.
// 2015) softened by a small core c, which keeps the displacement bounded (at most 2 theta_E, at
// |theta| = c) instead of diverging at the centre. So: outside the ring the background is magnified
// outward and stretched tangentially; at |theta| ~ theta_E it is smeared into a ring; inside it the
// image of the far side appears mirrored. Surface brightness is conserved by a remap, which is the
// physics: arcs are as bright as what they are images of. The field is feathered to zero at `reach`
// Einstein radii, so the lens has no edge.
//
// **Depth.** DF's lens-plane rule: nothing nearer than the mass (plus an entity owner's bounding
// radius) is bent, and no tap reads it -- a foreground object in front of a black hole stays crisp and
// never smears into the arcs. The horizon disc and the ring are drawn at the lens plane and are hidden
// by what stands in front of it.
//
// **The sky.** Where the bent tap lands on the sky, it reads the sky the frame already holds at that
// screen position -- and for a sky at infinity that IS the radiance along the bent ray, whatever the
// sky is (Glowmere's background shader layer, the IBL cube, a star field). Because the remap keeps
// every tap within 2 theta_E of the pixel and inside the lens's own disc, a lens wholly on screen never
// needs radiance from off screen; one crossing the frame edge is clamped there, as every DF tap is
// (ADR-703: the visible sky is not always the IBL, and a cube tap would paint a second, different sky
// at the edge). No environment lookup is added.

#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/kinds/stored_rows.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kModes[] = {"lens", "black hole"};
enum class Mode : int { Lens = 0, BlackHole = 1 };

constexpr EffectField kFields[] = {
    storedFloat("einsteinRadius", "Einstein radius", 2.0f, 0.01f, 5000.0f, 0.2f, 40.0f).main().fmt("%.2f m").log()
        .floorAt(0.01f).sec("Lens")
        .tooltip("The radius of the ring the background is smeared into, in metres at the mass. The\n"
                 "lens reaches several times further (Reach)."),
    storedChoice("mode", "Mode", 0, kModes).main()
        .tooltip("Lens: the mass is invisible, only its bending shows. Black hole: an event-horizon disc\n"
                 "sits at its centre, ringed by the photon ring."),
    storedFloat("horizonScale", "Horizon size", 0.45f, 0.05f, 0.95f, 0.1f, 0.9f).main()
        .tooltip("Black hole: the horizon disc's radius as a fraction of the Einstein radius."),
    storedFloat("chroma", "Chroma", 0.04f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("Splits the colours along the bend. Gravity bends every colour alike; a touch reads as\n"
                 "a camera lens looking at it."),
    storedColor("ringColor", "Ring colour", glm::vec3(1.0f, 0.82f, 0.58f)).main().sec("Ring"),
    storedFloat("photonRing", "Ring glow", 0.0f, 0.0f, 60.0f, 0.0f, 12.0f).main()
        .tooltip("A glowing ring, in HDR: it reaches the bloom. Around the horizon in Black hole mode (a\n"
                 "photon ring, or with a wide ring an accretion glow); on the Einstein ring otherwise.\n"
                 "0 is no ring."),
    storedFloat("ringWidth", "Ring width", 0.06f, 0.01f, 1.0f, 0.02f, 0.6f).main()
        .tooltip("The ring's width as a fraction of the Einstein radius: thin is a photon ring, wide an\n"
                 "accretion glow."),
    storedFloat("falloffRadius", "Reach", 3.0f, 1.2f, 10.0f, 1.5f, 6.0f).sec("Shape")
        .tooltip("How far the lens reaches, in Einstein radii. The bend is feathered to nothing there."),
    storedFloat("feather", "Edge feather", 0.5f, 0.05f, 1.0f, 0.1f, 1.0f)
        .tooltip("How much of the reach the bend spends fading out: high is a gentle, invisible edge."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the mass is. On an entity: an offset from the centre of the\n"
                 "owner's bounds, in world metres."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"gravLens", kFields};

struct Look {
    const char* name;
    Mode mode;
    float einsteinRadius, horizonScale;
    glm::vec3 ringColor;
    float photonRing, ringWidth, chroma, falloffRadius, feather;
};

// The catalog's three presets, and an accretion variant of the black hole.
constexpr Look kLooks[] = {
    {"Black Hole", Mode::BlackHole, 3.0f, 0.45f, {1.0f, 0.86f, 0.62f}, 5.0f, 0.05f, 0.03f, 3.0f, 0.5f},
    {"Subtle Mass", Mode::Lens, 1.2f, 0.45f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.06f, 0.02f, 2.5f, 0.6f},
    {"Einstein Ring", Mode::Lens, 4.0f, 0.45f, {0.7f, 0.85f, 1.0f}, 0.0f, 0.06f, 0.04f, 3.5f, 0.6f},
    {"Accretion Glow", Mode::BlackHole, 3.0f, 0.4f, {1.0f, 0.5f, 0.18f}, 3.5f, 0.3f, 0.03f, 3.0f, 0.5f},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "mode", static_cast<float>(l.mode));
    kRows.set(e, "einsteinRadius", l.einsteinRadius);
    kRows.set(e, "horizonScale", l.horizonScale);
    kRows.setRgb(e, "ringColor", l.ringColor);
    kRows.set(e, "photonRing", l.photonRing);
    kRows.set(e, "ringWidth", l.ringWidth);
    kRows.set(e, "chroma", l.chroma);
    kRows.set(e, "falloffRadius", l.falloffRadius);
    kRows.set(e, "feather", l.feather);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }
void style3(E& e) { applyLook(e, kLooks[3]); }

constexpr EffectStyle kStyles[] = {
    {kLooks[0].name, style0},
    {kLooks[1].name, style1},
    {kLooks[2].name, style2},
    {kLooks[3].name, style3},
};

// The catalog's routes: the bass makes the lens breathe, the beat flares the ring.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "einsteinRadius", 0.35f, 40.0f, 500.0f},
    {"beat.pulse", "photonRing", 2.5f, 10.0f, 300.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::GravitationalLens;
    e.activation = Activation::Always;
    applyLook(e, kLooks[0]);
    e.style.clear();
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::GravitationalLens;
    s.key = "gravLens";
    s.enumName = "GravitationalLens";
    s.displayName = "Gravitational Lens";
    s.description =
        "Light from behind bends around an invisible mass: the background and the sky are stretched into "
        "arcs around an Einstein ring, with a mirrored image inside it. In Black hole mode an event-horizon "
        "disc sits at the centre, ringed by a glowing photon ring or accretion glow. What stands in front "
        "of the mass is not bent.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment | CostBandwidth | CostExtraPass;
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Distortion;
    s.stage = RenderStage::ScreenSpace;
    // Last among the distortions: a remap stacked with additive warps is applied after them (the
    // catalog's 2.10), and DF orders by priority.
    s.priority = 3;
    s.addLabel = "Gravitational Lens";
    s.addTip = "A mass bending the light behind it into rings -- or a black hole with a glowing edge.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "photonRing";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Distortion;
    s.resolve.records = distortionRecords;
    return s;
}

} // namespace

// The producer (declared in distortion_frame.cpp's table). One camera-facing lens per live instance.
std::size_t gravitationalLensProxies(const EffectInstance& e, const EffectContext& ctx, float envelope,
                                     std::span<DistortionProxy> out) {
    if (out.empty() || envelope <= 0.0f) {
        return 0;
    }
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    glm::vec3 centre = offset;
    float ownerRadius = 0.0f;
    switch (e.owner.kind) {
    case EffectTarget::World: break;
    case EffectTarget::Entity: {
        NodeView view;
        if (!kinds::ownerCentre(e, ctx, offset, centre, ownerRadius, view)) {
            return 0; // no drawn owner this frame: no mass to bend around
        }
        break;
    }
    case EffectTarget::Camera:
    case EffectTarget::Light: return 0; // not a target this type declares
    }
    const float thetaE = kRows.f(e, "einsteinRadius");
    if (!kinds::finite3(centre) || !(thetaE > 1e-3f) || !std::isfinite(thetaE)) {
        return 0;
    }
    const float reach = std::clamp(kRows.f(e, "falloffRadius"), 1.2f, 10.0f) * thetaE;
    const bool hole = static_cast<Mode>(kRows.choice(e, "mode")) == Mode::BlackHole;

    DistortionProxy& p = out[0];
    // The exclusion radius: an entity owner's bounds stay in front of the lens plane, crisp.
    p.centre = glm::vec4(centre, ownerRadius);
    p.axis0 = glm::vec4(reach, 0.0f, 0.0f, static_cast<float>(DistortionShape::Facing));
    p.axis1 = glm::vec4(0.0f, reach, 0.0f, static_cast<float>(DistortionField::Lens));
    // The band the bend fades in over behind the lens plane.
    p.axis2 = glm::vec4(0.0f, 0.0f, reach, std::max(0.25f * reach, 0.05f));
    // The softened lens's displacement never exceeds theta_E^2 / (2c) = 2 theta_E: the rects' bound.
    p.terms = glm::vec4(1.0f, 0.0f, 0.0f, 2.0f * thetaE * envelope);
    p.motion = glm::vec4(envelope, hole ? envelope : 0.0f, 0.0f, 0.0f);
    p.shape = glm::vec4(thetaE, hole ? std::clamp(kRows.f(e, "horizonScale"), 0.05f, 0.95f) : 0.0f, 0.0f,
                        1.0f - std::clamp(kRows.f(e, "feather"), 0.05f, 1.0f));
    p.noise = glm::vec4(0.0f, std::clamp(kRows.f(e, "chroma"), 0.0f, 1.0f), std::clamp(kRows.f(e, "ringWidth"), 0.01f, 1.0f),
                        0.0f);
    p.rim = glm::vec4(kRows.rgb(e, "ringColor") * std::max(kRows.f(e, "photonRing"), 0.0f) * envelope, 0.0f);
    return 1;
}

const EffectSchema& gravitationalLensSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
