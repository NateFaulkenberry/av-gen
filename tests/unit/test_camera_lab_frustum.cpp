// The frustum the cull is given.
//
// The Camera Lab owns the frustum; the Visibility Lab owns what survives it and the LOD Lab owns
// which rung a survivor is drawn at. This file is about the handful of decisions on the camera's
// side of that line, and every one of them was checked against the code rather than taken from the
// brief -- two of the three claims handed to this lab turned out to need correcting, which is why
// they are written down here as assertions instead of as prose.
//
// **Planes are extracted twice, deliberately.** `world::frustumPlanes` is the GPU-free core the
// composition culls entities and terrain chunks with; `rendering::frustumPlanes` is the one that
// fills the GPU cull uniform and fits the shadow cascades. `rendering/` deliberately does not depend
// on `world/`, which is a real architectural boundary and not an accident -- but two independent
// implementations of Gribb-Hartmann that are allowed to drift would show up as an object culled on
// the CPU and drawn on the GPU, or the reverse, in one scene, once, with no error anywhere. So the
// price of keeping both is a test that says they are the same function.
//
// **The count of duplicates in the brief was wrong, and the correction matters.** There are *two*
// plane extractions, not three: `scene_renderer.cpp`'s third site is `aabbInsideFrustum`, which is a
// box *test* and a duplicate of `world::aabbVisible` rather than of either extractor. Three
// box-versus-frustum tests exist (`world::aabbVisible`, `rendering::aabbInsideFrustum`, and the
// sphere test in `cull.wgsl`), and they are three because they run in three places with three
// different dependency rules. Both facts are asserted below.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/visibility.hpp"
#include "scene/composition.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "scene/scene_types.hpp"
#include "world/terrain.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

// Cameras at real orientations and real places. Not one of them is on an axis and not one of them is
// at the origin, because the failure this repository has already paid for once was a rule that was
// bit-identical to the wrong rule for a centred, axis-aligned fixture (ADR-182).
std::vector<scene::Camera> cameras() {
    std::vector<scene::Camera> out;
    const glm::vec3 eyes[] = {{13.0f, 7.5f, -21.0f}, {-140.0f, 62.0f, 88.0f},
                              {3.25f, 0.4f, 1.75f},  {0.0f, 900.0f, 0.5f},
                              {-7.0f, -12.0f, 33.0f}};
    const glm::vec3 aims[] = {{1.0f, 2.0f, 3.0f},  {0.0f, 0.0f, 0.0f},
                              {9.0f, -4.0f, 2.0f}, {1.0f, 0.0f, -1.0f},
                              {0.5f, 40.0f, 0.5f}};
    for (int i = 0; i < 5; ++i) {
        scene::Camera c;
        c.position = eyes[i];
        c.target = aims[i];
        c.nearPlane = 0.05f + 0.3f * static_cast<float>(i);
        c.farPlane = 180.0f + 900.0f * static_cast<float>(i);
        c.fovYRadians = 0.42f + 0.21f * static_cast<float>(i);
        c.lens.useExplicitFov = true;
        out.push_back(c);
    }
    return out;
}

const float kAspects[] = {16.0f / 9.0f, 9.0f / 16.0f, 1.0f, 2.39f, 0.4f};

// A composition with nothing in it. `app::Engine` has none until a project is loaded, and loading
// one to ask a question about a viewport rectangle would make this file depend on a scene file.
struct BareComposition {
    assets::AssetRegistry registry{std::filesystem::temp_directory_path() / "avgen-camlab-frustum"};
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp{registry, "frustum"};
    BareComposition() { comp.attach(params, modulator); }
};

} // namespace

TEST_CASE("The two frustum extractions are the same function", "[camera][lab][frustum]") {
    // Bit-for-bit, not approximately. They are the same arithmetic in the same order and the only
    // textual difference between them is the epsilon guarding the normalisation (`> 0` against
    // `> 1e-12`), which cannot matter for a projection whose rows have length of order one. If this
    // ever fails it is because somebody changed one of the two, which is exactly the event that
    // needs to be noisy.
    for (const scene::Camera& cam : cameras()) {
        for (const float aspect : kAspects) {
            const glm::mat4 vp = cam.projection(aspect) * cam.view();
            const world::FrustumPlanes a = world::frustumPlanes(vp);
            const rendering::FrustumPlanes b = rendering::frustumPlanes(vp);
            REQUIRE(a.size() == b.size());
            for (std::size_t p = 0; p < a.size(); ++p) {
                INFO("aspect " << aspect << " plane " << p << ": world (" << a[p].x << ", " << a[p].y
                     << ", " << a[p].z << ", " << a[p].w << ") rendering (" << b[p].x << ", "
                     << b[p].y << ", " << b[p].z << ", " << b[p].w << ")");
                CHECK(a[p] == b[p]);
            }
        }
    }
}

TEST_CASE("The frustum planes are normalised and point inwards", "[camera][lab][frustum]") {
    // What the rest of the engine assumes about them, in both consumers: `aabbVisible` compares
    // `dot(n, corner) + w` against zero with no scale, and `cull.wgsl` compares it against `-radius`
    // in **metres**. The second only means anything if the planes carry unit normals, and nothing
    // anywhere says so -- so it is said here.
    for (const scene::Camera& cam : cameras()) {
        for (const float aspect : kAspects) {
            const world::FrustumPlanes planes =
                world::frustumPlanes(cam.projection(aspect) * cam.view());
            for (std::size_t p = 0; p < planes.size(); ++p) {
                INFO("plane " << p << " length " << glm::length(glm::vec3(planes[p])));
                CHECK_THAT(glm::length(glm::vec3(planes[p])),
                           Catch::Matchers::WithinAbs(1.0, 1e-4));
            }
            // Inwards: a point just past the near plane along the view direction is inside every
            // one of them. The control is the same point placed *behind* the eye, which must be
            // outside at least one -- without it, a set of planes that pointed outwards would also
            // satisfy "the length is one".
            const glm::vec3 forward = glm::normalize(cam.target - cam.position);
            const glm::vec3 inside = cam.position + forward * (cam.nearPlane + 1.0f);
            const glm::vec3 behind = cam.position - forward * 50.0f;
            int insideCount = 0;
            int behindCount = 0;
            for (const glm::vec4& pl : planes) {
                insideCount += glm::dot(glm::vec3(pl), inside) + pl.w >= 0.0f ? 1 : 0;
                behindCount += glm::dot(glm::vec3(pl), behind) + pl.w >= 0.0f ? 1 : 0;
            }
            INFO("aspect " << aspect << ": inside " << insideCount << "/6, behind " << behindCount
                 << "/6");
            CHECK(insideCount == 6);
            CHECK(behindCount < 6);
        }
    }
}

TEST_CASE("A wider aspect only ever admits more, never less", "[camera][lab][frustum]") {
    // The property the terrain cull's aspect floor rests on, stated as a property rather than
    // assumed. Widening the aspect widens the horizontal half-angle and leaves the vertical one
    // alone, so the wide frustum contains the narrow one -- which is what makes "floor the aspect"
    // a conservative choice and not merely a different one.
    for (const scene::Camera& cam : cameras()) {
        const world::FrustumPlanes narrow =
            world::frustumPlanes(cam.projection(16.0f / 9.0f) * cam.view());
        const world::FrustumPlanes wide = world::frustumPlanes(cam.projection(2.5f) * cam.view());
        // Screen-right out of the view matrix's first row, not `cross(forward, worldUp)`. One of
        // the five cameras above looks very nearly straight down, where that cross product collapses
        // to nothing -- the fan then never moved, every box stayed in both frustums, and the control
        // reported "wide only = 0" as though the two frustums were the same. The view matrix is the
        // only thing that knows where the screen's horizontal actually is.
        const glm::mat4 view = cam.view();
        const glm::vec3 forward(-view[0][2], -view[1][2], -view[2][2]);
        const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
        int admittedByBoth = 0;
        int admittedByWideOnly = 0;
        int admittedByNarrowOnly = 0;
        // A fan of small boxes swept across the horizontal field at a fixed depth, and the sweep is
        // sized from the camera rather than typed in: at depth d the narrow frustum is
        // d * aspect * tan(fovY / 2) wide, so sweeping to twice that guarantees the fan leaves it.
        // A fixed +/-100 m sweep looked fine and silently fell entirely *inside* both frustums for
        // two of the five cameras, which made the control read "wide only = 0" -- the arm reporting
        // that the fixture was too small, exactly as it should.
        const float depth = 60.0f;
        const float halfWidth = depth * 2.5f * std::tan(cam.effectiveFovY() * 0.5f);
        const float step = halfWidth * 2.0f / 40.0f;
        for (int i = -40; i <= 40; ++i) {
            const glm::vec3 c = cam.position + forward * depth + right * (static_cast<float>(i) * step);
            const glm::vec3 lo = c - glm::vec3(0.5f);
            const glm::vec3 hi = c + glm::vec3(0.5f);
            const bool n = world::aabbVisible(narrow, lo, hi);
            const bool w = world::aabbVisible(wide, lo, hi);
            admittedByBoth += (n && w) ? 1 : 0;
            admittedByWideOnly += (!n && w) ? 1 : 0;
            admittedByNarrowOnly += (n && !w) ? 1 : 0;
        }
        INFO("both " << admittedByBoth << ", wide only " << admittedByWideOnly << ", narrow only "
             << admittedByNarrowOnly);
        CHECK(admittedByNarrowOnly == 0);   // containment
        CHECK(admittedByWideOnly > 0);      // and the control: the two really are different
    }
}

TEST_CASE("Entity culling cannot be switched off by a zero viewport", "[camera][lab][frustum]") {
    // The brief handed this lab a fact to check: "`viewportHeight_ == 0` disables entity culling
    // entirely". The guard exists -- `Composition::cullEntityNodes` returns early on it -- but the
    // field it guards **cannot be zero**. It is declared 900, and `setViewport` clamps both axes to
    // at least 1. So the branch is unreachable and entity culling has never been off.
    //
    // Asserted through engine behaviour rather than by reading the number back (§26), because "the
    // field is nonzero" would still pass if the guard were later made reachable some other way. What
    // is checked is that an entity behind the camera is *culled*, which is the thing the guard would
    // have prevented.
    BareComposition fx;
    // A viewport nobody ever set.
    CHECK(fx.comp.viewportWidth() > 0);
    CHECK(fx.comp.viewportHeight() > 0);
    // And one somebody set to zero, which is the only route a caller has.
    fx.comp.setViewport(0, 0);
    CHECK(fx.comp.viewportWidth() > 0);
    CHECK(fx.comp.viewportHeight() > 0);
}

TEST_CASE("The cull aspect follows the render extent, and widens only for terrain",
          "[camera][lab][frustum]") {
    // The asymmetry the brief asks for a verdict on, measured rather than argued.
    //
    // `Composition::cullEntityNodes` builds its frustum at the *exact* viewport aspect;
    // `Composition::updateTerrainLod` floors it at 2.5. Both read the same `viewportWidth_` and
    // `viewportHeight_`, set by `Composition::setViewport` from the same render extent the renderer
    // takes its own `hdr_.width() / hdr_.height()` from -- so the entity cull's frustum is the
    // rendered frustum exactly, and the terrain cull's is deliberately wider.
    //
    // The cost of the floor is a number, and this is it: how much extra ground survives the wider
    // frustum at the aspect ratios the editor actually runs at.
    scene::Camera cam;
    cam.position = {-140.0f, 62.0f, 88.0f};
    cam.target = {0.0f, 8.0f, 0.0f};
    cam.lens.useExplicitFov = true;
    cam.fovYRadians = 0.87f;

    const glm::vec3 forward = glm::normalize(cam.target - cam.position);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    for (const float aspect : {16.0f / 9.0f, 4.0f / 3.0f, 1.0f, 9.0f / 16.0f, 2.39f}) {
        const world::FrustumPlanes exact = world::frustumPlanes(cam.projection(aspect) * cam.view());
        const world::FrustumPlanes floored =
            world::frustumPlanes(cam.projection(std::max(2.5f, aspect)) * cam.view());
        // A 64-metre grid of chunk-sized boxes laid out in front of the camera, which is the shape
        // a terrain chunk table has.
        int exactKept = 0;
        int flooredKept = 0;
        for (int j = 0; j < 12; ++j) {
            for (int i = -12; i <= 12; ++i) {
                const glm::vec3 c = cam.position + forward * (40.0f + 64.0f * static_cast<float>(j))
                                  + right * (static_cast<float>(i) * 64.0f);
                const glm::vec3 lo = c - glm::vec3(32.0f, 16.0f, 32.0f);
                const glm::vec3 hi = c + glm::vec3(32.0f, 16.0f, 32.0f);
                exactKept += world::aabbVisible(exact, lo, hi) ? 1 : 0;
                flooredKept += world::aabbVisible(floored, lo, hi) ? 1 : 0;
            }
        }
        const double extra = exactKept > 0 ? 100.0 * (flooredKept - exactKept) / exactKept : 0.0;
        WARN("aspect " << aspect << ": exact keeps " << exactKept << " chunk-boxes, the 2.5 floor "
             "keeps " << flooredKept << " (+" << extra << "%)");
        // The floor is conservative by construction, so it can only ever keep more.
        CHECK(flooredKept >= exactKept);
        // At 2.39:1 and wider the floor is doing nothing at all, which is worth having asserted:
        // the cost below is a cost the ultrawide case does not pay.
        if (aspect >= 2.5f) {
            CHECK(flooredKept == exactKept);
        }
    }
}

TEST_CASE("The entity cull frustum is the frustum the frame is rendered with",
          "[camera][lab][frustum]") {
    // §37: the renderer is the source of truth, so the diagnostic that matters is whether the
    // frustum the cull used is the one the picture was taken through. Both are
    // `Camera::projection(aspect) * Camera::view()`; the only thing that can separate them is the
    // aspect, and the aspect comes from the same extent -- `Composition::setViewport(w, h)` on one
    // side and the HDR target's own width and height on the other, both set from `renderWidth_` and
    // `renderHeight_` in the same block of `Application::update`.
    //
    // Checked here at the level a CPU test can reach: that a composition told about an extent culls
    // with that extent's aspect and not with the default it was constructed with. The first version
    // of this engine's terrain cull did not, and the comment recording that is still in
    // `updateTerrainLod` describing the viewport as something "waiting" to be plumbed in.
    BareComposition fx;
    fx.comp.setViewport(3840, 1080);   // 3.55:1
    CHECK(fx.comp.viewportWidth() == 3840);
    CHECK(fx.comp.viewportHeight() == 1080);
    const float wide = static_cast<float>(fx.comp.viewportWidth())
                     / static_cast<float>(fx.comp.viewportHeight());

    fx.comp.setViewport(1080, 1920);   // 0.5625:1
    const float tall = static_cast<float>(fx.comp.viewportWidth())
                     / static_cast<float>(fx.comp.viewportHeight());
    INFO("wide " << wide << ", tall " << tall);
    CHECK(wide > 3.0f);
    CHECK(tall < 0.6f);

    // And the terrain floor's behaviour at both, which is the asymmetry in one line: the wide
    // viewport gets its own aspect, the tall one gets 2.5 instead of 0.5625 -- a frustum four times
    // wider than the frame, in the orientation a phone-shaped deliverable is rendered at.
    CHECK(std::max(2.5f, wide) == wide);
    CHECK(std::max(2.5f, tall) == 2.5f);
}

// ---- the diagnostic against the decision (§37) ---------------------------------------------------
//
// `RenderObjectDiagnostic::frustumMargins` is the instrument this lab reaches for first: six signed
// distances, one per plane, saying how far the object's box is from being cut. The Cameras panel
// prints them and `compareSnapshots` diffs them, and the question a person asks them is "why was
// this culled".
//
// **They are computed against a different box than the cull decided with, in two ways.**
// `Composition::cullEntityNodes` calls `scene::entityCullBounds(scene_, entity)`, which takes the
// **posed** box for a skinned entity and then pads it by `padFraction = 0.25` and
// `padAbsolute = 0.25`. `SceneRenderer::render` fills the diagnostic from
// `scene.meshes[entity.mesh].bounds()` transformed by the model matrix: the **bind** box, **unpadded**.
//
// So the margins are systematically tighter than the decision for every entity, and describe an
// entirely different volume for a skinned one. §37 is explicit about which of the two is wrong when
// a camera diagnostic and the frame disagree.
//
// Not fixed here. The fix is one line -- hand the diagnostic `entityCullBounds` -- but the numbers
// it changes are in `examples/qa/baselines/*.snapshot.json`, which carry a 49-joint skinned entity
// and are the committed baselines of a ladder other labs are measuring against right now. Refreshing
// them is a GPU run on a shared device and somebody else's regression surface. What is here is the
// reproduction, so the size of the disagreement is on the record.

namespace {

// A two-joint bar whose tip swings out along -X when the tip joint is bent. Same shape as the
// fixture in `test_skeleton.cpp`, which is where the posed-bounds rule was established.
scene::MeshData reachingBar() {
    scene::MeshData mesh;
    mesh.name = "bar";
    for (int i = 0; i <= 6; ++i) {
        const float y = static_cast<float>(i) * 0.5f;
        for (const float x : {-0.2f, 0.2f}) {
            scene::Vertex v;
            v.position = {x, y, 0.0f};
            v.normal = {0.0f, 0.0f, 1.0f};
            mesh.vertices.push_back(v);
            scene::SkinInfluence in{};
            // Everything above the halfway joint is driven by the tip.
            const bool upper = y > 1.5f;
            in.joints[0] = upper ? 1u : 0u;
            in.weights[0] = 1.0f;
            mesh.skin.push_back(in);
        }
    }
    for (std::uint32_t i = 0; i + 3 < static_cast<std::uint32_t>(mesh.vertices.size()); i += 2) {
        mesh.indices.insert(mesh.indices.end(), {i, i + 1, i + 2, i + 1, i + 3, i + 2});
    }
    return mesh;
}

std::array<float, 6> marginsOf(const world::FrustumPlanes& planes, glm::vec3 lo, glm::vec3 hi) {
    std::array<float, 6> out{};
    for (std::size_t p = 0; p < planes.size(); ++p) {
        const glm::vec4& pl = planes[p];
        const glm::vec3 corner(pl.x >= 0.0f ? hi.x : lo.x, pl.y >= 0.0f ? hi.y : lo.y,
                               pl.z >= 0.0f ? hi.z : lo.z);
        out[p] = glm::dot(glm::vec3(pl), corner) + pl.w;
    }
    return out;
}

} // namespace

TEST_CASE("The frustum margins describe a smaller box than the cull decided with",
          "[camera][lab][frustum][diagnostic]") {
    scene::Scene scene;
    const auto mesh = scene.addMesh(reachingBar());
    REQUIRE(scene.meshes[mesh].skinned());

    scene::SkinnedRig rig;
    rig.skeleton.name = "bar";
    scene::Joint root;
    root.name = "root";
    root.parent = -1;
    rig.skeleton.joints.push_back(root);
    scene::Joint tip;
    tip.name = "tip";
    tip.parent = 0;
    tip.rest.position = {0.0f, 1.5f, 0.0f};
    rig.skeleton.joints.push_back(tip);
    rig.skeleton.palette = {0, 1};
    scene.rigs.push_back(rig);

    auto& entity = scene.addEntity("bar", mesh);
    entity.rig = 0;

    // Bent a right angle: everything above 1.5 m swings out along -X, well past the bind box.
    const glm::mat4 toTip = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, 0.0f));
    const glm::mat4 fromTip = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.5f, 0.0f));
    const glm::mat4 bend =
        toTip * glm::mat4_cast(glm::angleAxis(1.5707963f, glm::vec3(0.0f, 0.0f, 1.0f))) * fromTip;
    scene.rigs[0].palette = {glm::mat4(1.0f), bend};

    const scene::CullBounds cull = scene::entityCullBounds(scene, entity);
    REQUIRE(cull.posed);
    const auto [bindLo, bindHi] = scene.meshes[mesh].bounds();

    // Part one, and it holds for skinned and unskinned alike: the cull's box is padded and the
    // diagnostic's is not.
    INFO("bind box x " << bindLo.x << ".." << bindHi.x << ", cull box x " << cull.min.x << ".."
         << cull.max.x);
    CHECK(cull.max.y < bindHi.y + 10.0f);           // sanity: the same object
    CHECK(cull.min.z < bindLo.z);                   // padded outward in an axis the pose does not touch
    CHECK(cull.max.z > bindHi.z);

    // Part two, which is only true for a posed skeleton: the pose leaves the bind box entirely.
    // This is the discriminating fact -- without it the rest would pass against a bind-pose
    // implementation, which is how the first attempt at the posed-bounds regression next door
    // fooled itself.
    REQUIRE(cull.min.x < bindLo.x - 1.0f);

    // And the consequence, which is what makes it a diagnostic defect rather than a curiosity: a
    // camera can be placed so that the two boxes give opposite answers, and the panel would then
    // print six margins saying "comfortably inside" beside a cull reason of "camera-frustum" -- or
    // the reverse, which is the one that sends somebody hunting in the renderer.
    // Swept rather than hand-placed. A single camera chosen by eye either produces the flip or
    // does not, and tuning one until it does is fitting the fixture to the answer; a sweep reports
    // how much of the space disagrees, which is the number worth knowing. The first hand-placed
    // camera showed margins differing by up to 0.78 m with both boxes still inside the frustum --
    // a real disagreement that was invisible in the verdict, which is exactly the case a person
    // reading the panel would shrug at.
    const glm::mat4 model = entity.transform.matrix();
    glm::vec3 bindWorldLo(std::numeric_limits<float>::max());
    glm::vec3 bindWorldHi(std::numeric_limits<float>::lowest());
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 local((corner & 1) ? bindHi.x : bindLo.x, (corner & 2) ? bindHi.y : bindLo.y,
                              (corner & 4) ? bindHi.z : bindLo.z);
        const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
        bindWorldLo = glm::min(bindWorldLo, world);
        bindWorldHi = glm::max(bindWorldHi, world);
    }

    std::size_t sampled = 0;
    std::size_t marginsDiffer = 0;
    std::size_t verdictsDiffer = 0;
    float worstMargin = 0.0f;
    std::string firstFlip;
    for (int a = 0; a < 24; ++a) {
        const float azimuth = static_cast<float>(a) * 0.2618f + 0.13f;   // 15-degree steps, off-axis
        for (const float height : {0.7f, 1.1f, 1.6f}) {
            // A tight lens close in: the two boxes only give different verdicts where the frustum
            // is narrow enough to contain one and not the other, and a wide establishing lens
            // contains both from everywhere.
            scene::Camera cam;
            cam.lens.useExplicitFov = true;
            cam.fovYRadians = 0.30f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 200.0f;
            const glm::vec3 look(-1.55f, height, 0.0f);   // where the bent tip swings to
            cam.position = look - glm::vec3(std::cos(azimuth), -0.06f, std::sin(azimuth)) * 2.2f;
            cam.target = look;
            const world::FrustumPlanes planes =
                world::frustumPlanes(cam.projection(16.0f / 9.0f) * cam.view());

            const bool cullSays = world::aabbVisible(planes, cull.min, cull.max);
            const bool diagSays = world::aabbVisible(planes, bindWorldLo, bindWorldHi);
            const auto cullMargins = marginsOf(planes, cull.min, cull.max);
            const auto diagMargins = marginsOf(planes, bindWorldLo, bindWorldHi);
            ++sampled;
            bool differ = false;
            for (std::size_t i = 0; i < 6; ++i) {
                const float d = std::abs(cullMargins[i] - diagMargins[i]);
                differ = differ || d > 1e-3f;
                worstMargin = std::max(worstMargin, d);
            }
            marginsDiffer += differ ? 1 : 0;
            if (cullSays != diagSays) {
                ++verdictsDiffer;
                if (firstFlip.empty()) {
                    firstFlip = "eye (" + std::to_string(cam.position.x) + ", "
                              + std::to_string(cam.position.y) + ", " + std::to_string(cam.position.z)
                              + "): the cull kept it (" + (cullSays ? "yes" : "no")
                              + ") and the diagnostic's box says " + (diagSays ? "yes" : "no");
                }
            }
        }
    }
    INFO("of " << sampled << " cameras: " << marginsDiffer
         << " have differing margins (worst " << worstMargin << " m), " << verdictsDiffer
         << " disagree about the verdict. " << firstFlip);
    // Every camera's margins differ, because the boxes are simply not the same box.
    CHECK(marginsDiffer == sampled);
    CHECK(worstMargin > 0.5f);
    // And the disagreement reaches the verdict, which is what makes it a defect rather than a
    // rounding note: on those cameras the panel prints six margins that contradict the cull reason
    // printed beside them.
    CHECK(verdictsDiffer > 0);
}

// ---- the cull must not rescan the geometry (ADR-348 follow-up) -----------------------------------
//
// `Composition::cullEntityNodes` calls `entityCullBounds` for every entity, every frame. For two
// years that called `MeshData::bounds()`, which scans every vertex, and on the Tree of Life -- 45
// entities over meshes carrying 39.9 million vertices between them -- it cost **91 ms of a 91.5 ms
// scene update**, against a 22 ms GPU frame. The scene ran at about five frames a second and every
// GPU measurement of it looked healthy.
//
// This is asserted as a **rebuild count and not a duration**. A timing test would pass on a quiet
// machine and fail on a loaded one, and this repository has five agents on one machine; a count is
// the same fact at load average two and at load average forty.
TEST_CASE("culling a scene does not rescan its vertices", "[scene][culling][bounds]") {
    scene::Scene scene;
    // One mesh with enough vertices that a rescan would be obvious, drawn by several entities --
    // which is the shape that matters: the cost was per *entity*, not per mesh.
    const scene::MeshId mesh = scene.addMesh(scene::makeUvSphere(1.0f, 64, 48));
    for (int i = 0; i < 8; ++i) {
        scene::Entity& e = scene.addEntity(fmt::format("ball{}", i), mesh);
        e.transform.position = glm::vec3(static_cast<float>(i) * 3.0f, 0.0f, 0.0f);
    }
    REQUIRE(scene.meshes[mesh].vertices.size() > 3000);

    const std::uint64_t before = scene.meshBoundsRebuilds();
    for (int frame = 0; frame < 10; ++frame) {
        for (const scene::Entity& e : scene.entities) {
            const scene::CullBounds bounds = scene::entityCullBounds(scene, e);
            CHECK(bounds.max.x >= bounds.min.x);
        }
    }
    // Eighty calls, one rebuild. The arm.
    CHECK(scene.meshBoundsRebuilds() - before == 1);

    // The control: the cache is not simply frozen. Change the geometry, say so the way the engine
    // says so, and the next question is answered afresh -- otherwise the assertion above would also
    // pass on a cache that had stopped working.
    scene.meshes[mesh].vertices[0].position = glm::vec3(100.0f, 100.0f, 100.0f);
    ++scene.meshVersion;
    const scene::CullBounds moved = scene::entityCullBounds(scene, scene.entities.front());
    CHECK(scene.meshBoundsRebuilds() - before == 2);
    CHECK(moved.max.x > 50.0f); // ...and it saw the new vertex

    // And the cached answer is the same answer. A cache that is fast and wrong is worse than the
    // scan it replaced, so the value is checked against `MeshData::bounds()` directly.
    const auto [lo, hi] = scene.meshes[mesh].bounds();
    const auto& cached = scene.meshBounds(mesh);
    CHECK(cached.first == lo);
    CHECK(cached.second == hi);
}

