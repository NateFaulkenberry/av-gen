// The Visibility / Culling Lab, against the draw list the GPU actually built (§37).
//
// The trap this file exists to avoid: a `visibilityReasonFor()` helper that re-derives the answer
// from the same inputs the renderer uses agrees with the renderer by construction and proves
// nothing. So nothing here compares one CPU function against another. Every assertion is against
// `readVisibleIndices` -- the compacted per-level list that shaders/cull.wgsl wrote and that the
// drawIndexedIndirect reads its instance through -- or against lit pixels in the rendered frame.
// If the diagnostic says VISIBLE and the index is not in that list, this fails.
//
// The fixture is a production asset (§29): Glowmere's `canopy` layer is CommonTree_1 normalised to
// 14 m, and that is what is instanced here, isolated onto a bare camera with no terrain, no
// vegetation and no lighting rig to hide behind.
#include "assets/gltf_loader.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 360;
constexpr float kCanopyHeight = 14.0f;

// The production tree, its parts merged into one source mesh. Merged deliberately: the question
// here is whether the cull sphere covers the asset, and ADR-108 already makes the lead's sphere
// cover every part, so one mesh is the same geometry the decision is made about.
struct TreeSource {
    std::shared_ptr<scene::MeshData> mesh;
    glm::vec3 lo{0.0f};
    glm::vec3 hi{0.0f};
    float sourceScale = 1.0f;
    bool valid = false;
};

TreeSource loadTree() {
    TreeSource out;
    const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "assets" / "quaternius" / "glTF" / "CommonTree_1.gltf";
    if (!fs::is_regular_file(path)) {
        return out;
    }
    scene::Scene s;
    if (!assets::loadGltf(path, s, {}) || s.meshes.empty()) {
        return out;
    }
    auto merged = std::make_shared<scene::MeshData>();
    merged->name = "CommonTree_1";
    for (const scene::MeshData& part : s.meshes) {
        const auto base = static_cast<std::uint32_t>(merged->vertices.size());
        merged->vertices.insert(merged->vertices.end(), part.vertices.begin(), part.vertices.end());
        for (const std::uint32_t index : part.indices) {
            merged->indices.push_back(base + index);
        }
    }
    if (merged->vertices.empty() || merged->indices.empty()) {
        return out;
    }
    const auto [lo, hi] = merged->bounds();
    out.mesh = merged;
    out.lo = lo;
    out.hi = hi;
    out.sourceScale = kCanopyHeight / std::max(hi.y - lo.y, 1e-4f);
    out.valid = true;
    return out;
}

scene::ProceduralGeometry treeObject(const TreeSource& tree, std::vector<glm::vec3> positions) {
    scene::ProceduralGeometry g;
    g.name = "canopy";
    g.source.kind = scene::PrimitiveKind::Mesh;
    g.source.asset = "CommonTree_1.gltf";
    g.source.assetMesh = tree.mesh;
    g.meshHash = 0xC0FFEEull;
    g.structureVersion = 1;
    g.sourceTransform.scale = glm::vec3(tree.sourceScale);
    g.material.baseColor = {0.5f, 0.7f, 0.4f};
    g.material.roughness = 0.7f;
    // Glowmere's `canopy` ladder, verbatim (composition.cpp, examples/world/*.scene.json).
    g.lod.cull = true;
    g.lod.maxDistance = 520.0f;
    g.lod.minScreenRadius = 1.0f;
    g.lod.lodCount = scene::kMaxLodLevels;
    g.lod.lodByScreenSize = true;
    g.lod.lodDistances[0] = 28.0f;
    g.lod.lodDistances[1] = 11.0f;
    g.lod.lodDistances[2] = 4.0f;
    // ADR-082's stability terms off: this file is about whether the decision is right, and
    // hysteresis makes it depend on the previous frame. Determinism first (§10).
    g.lod.lodSpread = 0.0f;
    g.lod.lodHysteresis = 0.0f;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        scene::InstanceRecord r{};
        r.position = glm::vec4(positions[i], 1.0f);
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
        r.random = {0.25f, 0.5f, 0.75f, 0.125f};
        r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(i)};
        r.emissive = {0.0f, 0.0f, 0.0f, 0.0f};
        g.instances.push_back(r);
    }
    return g;
}

scene::Scene sceneWith(scene::ProceduralGeometry object) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.6f, -0.7f));
    key.intensity = 4.0f;
    s.addLight(key);
    s.procedurals.push_back(std::move(object));
    return s;
}

int litPixels(const gpu::Image8& img) {
    int count = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            if (p[0] + p[1] + p[2] > 15) {
                ++count;
            }
        }
    }
    return count;
}

// Every index the GPU compacted, over all four levels: the union of what the indirect draws read.
std::vector<std::uint32_t> drawnIndices(rendering::ProceduralRenderer& procedurals, const std::string& name) {
    std::vector<std::uint32_t> all;
    for (int level = 0; level < scene::kMaxLodLevels; ++level) {
        auto list = procedurals.readVisibleIndices(name, level);
        if (!list) {
            continue;
        }
        all.insert(all.end(), list->begin(), list->end());
    }
    std::sort(all.begin(), all.end());
    return all;
}

} // namespace

TEST_CASE("A production tree with its crown in frame is in the draw list", "[gpu][culling][visibility]") {
    const TreeSource tree = loadTree();
    if (!tree.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s = sceneWith(treeObject(tree, {glm::vec3(0.0f)}));
    s.camera.fovYRadians = glm::radians(50.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 2000.0f;

    // The framing from the unit reproduction: a camera 40 m out and high enough that the tree's
    // foot falls below the bottom of frame while its crown does not.
    const float range = 40.0f;
    const float halfFovTan = std::tan(s.camera.fovYRadians * 0.5f);
    const float fixedRadius = rendering::sourceCullRadius(tree.lo, tree.hi) * tree.sourceScale;
    const float oldRadius = std::max(0.5f * glm::length(tree.hi - tree.lo), 1e-4f) * tree.sourceScale;
    REQUIRE(oldRadius < fixedRadius);
    const float outside = 0.5f * (oldRadius + fixedRadius);
    const float height = range * halfFovTan + outside / std::cos(s.camera.fovYRadians * 0.5f);
    s.camera.position = {0.0f, height, range};
    s.camera.target = {0.0f, height, 0.0f};

    renderer.procedurals().setViewport(kWidth, kHeight);
    FrameTime t{};
    auto img = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    CHECK(ctx->errorCount() == 0);

    INFO("camera " << height << " m up, " << range << " m out; cull sphere " << fixedRadius
                   << " m (the rule before this was " << oldRadius << " m)");

    SECTION("the GPU's own compacted list contains the instance") {
        // Not "the CPU thinks it should be visible" -- the list the drawIndexedIndirect reads.
        const std::vector<std::uint32_t> drawn = drawnIndices(renderer.procedurals(), "canopy");
        REQUIRE(drawn.size() == 1);
        CHECK(drawn[0] == 0u);
    }

    SECTION("and the frame has the tree in it, where an out-of-frame one puts nothing") {
        // The renderer is the source of truth (§37): pixels, not counters. Paired with a control
        // rather than compared to a threshold -- "more than N lit pixels" is a number somebody
        // chose, and the claim that matters is that this framing draws the tree and a framing that
        // does not contain it draws nothing (ADR-182).
        const int shown = litPixels(*img);
        scene::Scene away = s;
        // Far enough above that the whole tree, sphere and all, is below the bottom plane.
        const float clear = 3.0f * fixedRadius;
        const float high = range * halfFovTan + clear / std::cos(away.camera.fovYRadians * 0.5f);
        away.camera.position = {0.0f, high, range};
        away.camera.target = {0.0f, high, 0.0f};
        auto empty = renderer.renderToImage(away, t, kWidth, kHeight);
        REQUIRE(empty.has_value());
        const int hidden = litPixels(*empty);
        INFO(shown << " lit pixels with the crown in frame, " << hidden << " with the tree below it");
        CHECK(shown > 0);
        CHECK(hidden == 0);
        CHECK(shown > hidden);

        // A hazard this control turned up, recorded here because a lab that hides it is worse than
        // no lab (§37). `readVisibleIndices` is NOT a reliable account of the control frame: when
        // ProceduralRenderer::update proves the whole object is rejected (objectFullyCulled) it
        // skips the object's four cull dispatches *and* its indirect draws, so the visible list and
        // the stats buffer keep whatever the last frame that did run the pass left in them. Nothing
        // is drawn -- the pixels above are the proof -- but a tool that reads the list and reports
        // "these instances were drawn" is reading history.
        //
        // So the right diagnostic for this frame is the object-level one, which names the decision
        // that was actually taken, and it is asserted instead.
        const glm::mat4 vp =
            away.camera.projection(static_cast<float>(kWidth) / static_cast<float>(kHeight)) * away.camera.view();
        rendering::CullCamera awayCull;
        awayCull.position = away.camera.position;
        awayCull.projScale = rendering::cullProjScale(away.camera.effectiveFovY(), kHeight);
        CHECK(rendering::proceduralVisibility(away.procedurals[0], rendering::frustumPlanes(vp), awayCull,
                                              glm::mat4(1.0f),
                                              rendering::instanceBounds(away.procedurals[0].instances),
                                              fixedRadius, true, false, true) ==
              rendering::VisibilityReason::ObjectFullyCulled);
    }

    SECTION("the reason code agrees with the list") {
        const glm::mat4 viewProj =
            s.camera.projection(static_cast<float>(kWidth) / static_cast<float>(kHeight)) * s.camera.view();
        rendering::CullCamera cull;
        cull.position = s.camera.position;
        cull.projScale = rendering::cullProjScale(s.camera.effectiveFovY(), kHeight);
        const auto v = rendering::instanceVisibility(s.procedurals[0].lod, rendering::frustumPlanes(viewProj),
                                                     cull, glm::vec3(0.0f), fixedRadius);
        CHECK(v.reason == rendering::VisibilityReason::Visible);
        CHECK_FALSE(drawnIndices(renderer.procedurals(), "canopy").empty());
    }
}

TEST_CASE("The reason code says VISIBLE exactly for the instances the GPU drew",
          "[gpu][culling][visibility]") {
    // The §37 property, swept: a grove spanning in-frame, off-frame, too far and too small, with
    // the diagnostic's verdict checked against the compacted list index by index. A diagnostic that
    // reported VISIBLE for everything -- the failure ADR-182 warns about -- fails this immediately,
    // and so does one that reported nothing visible, because both arms are counted.
    const TreeSource tree = loadTree();
    if (!tree.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    std::vector<glm::vec3> positions;
    for (int x = -9; x <= 9; ++x) {
        for (int z = -9; z <= 9; ++z) {
            positions.emplace_back(static_cast<float>(x) * 28.0f, 0.0f, static_cast<float>(z) * 28.0f);
        }
    }
    const std::size_t count = positions.size();
    scene::Scene s = sceneWith(treeObject(tree, std::move(positions)));
    s.camera.fovYRadians = glm::radians(50.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 2000.0f;
    // Off centre and turned, so the frustum cuts the grove at an angle rather than along a row:
    // an axis-aligned camera would put a whole rank of instances exactly on a plane, which is the
    // one case where a floating-point tie could make the CPU and the GPU disagree for a reason
    // that is not a bug.
    s.camera.position = {37.0f, 22.0f, 133.0f};
    s.camera.target = {-11.0f, 4.0f, -40.0f};

    renderer.procedurals().setViewport(kWidth, kHeight);
    FrameTime t{};
    auto img = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    CHECK(ctx->errorCount() == 0);
    CHECK(litPixels(*img) > 200); // the frame really does contain trees

    const std::vector<std::uint32_t> drawn = drawnIndices(renderer.procedurals(), "canopy");
    // Both arms reached: the sweep saw the pass keep things and reject things. Without this the
    // comparison below could be vacuously true.
    REQUIRE(!drawn.empty());
    REQUIRE(drawn.size() < count);

    const glm::mat4 viewProj =
        s.camera.projection(static_cast<float>(kWidth) / static_cast<float>(kHeight)) * s.camera.view();
    const rendering::FrustumPlanes planes = rendering::frustumPlanes(viewProj);
    rendering::CullCamera cull;
    cull.position = s.camera.position;
    cull.projScale = rendering::cullProjScale(s.camera.effectiveFovY(), kHeight);
    const float radius = rendering::sourceCullRadius(tree.lo, tree.hi) * tree.sourceScale;
    // The ladder measures projected size with the tight sphere about the source's box, not with the
    // conservative sphere the rejection tests use (rendering/visibility.hpp, shaders/cull.wgsl).
    // Passed explicitly here because this case checks the *rung* as well as the verdict, and with
    // the two numbers conflated it would report the wrong level for every non-centred asset -- and
    // a tree standing on its own origin is the only asset in this file.
    const float lodRadius = std::max(0.5f * glm::length(tree.hi - tree.lo), 1e-4f) * tree.sourceScale;

    std::size_t saidVisible = 0;
    std::size_t saidCulled = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec3 centre(s.procedurals[0].instances[i].position);
        const auto v =
            rendering::instanceVisibility(s.procedurals[0].lod, planes, cull, centre, radius, lodRadius);
        const bool inDrawList = std::binary_search(drawn.begin(), drawn.end(), static_cast<std::uint32_t>(i));
        INFO("instance " << i << " at (" << centre.x << ", " << centre.z << ") reported "
                         << rendering::visibilityReasonName(v.reason) << ", drawn=" << inDrawList);
        REQUIRE((v.reason == rendering::VisibilityReason::Visible) == inDrawList);
        if (inDrawList) {
            // The rung the diagnostic names must be the rung the instance was compacted into.
            auto list = renderer.procedurals().readVisibleIndices("canopy", v.lodLevel);
            REQUIRE(list.has_value());
            CHECK(std::find(list->begin(), list->end(), static_cast<std::uint32_t>(i)) != list->end());
            ++saidVisible;
        } else {
            ++saidCulled;
        }
    }
    CHECK(saidVisible > 0);
    CHECK(saidCulled > 0);
    CHECK(saidVisible == drawn.size());
}
