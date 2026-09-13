// The object cap, on a real device (ADR-128, renderer-upgrade Phase E).
//
// `kMaxObjects = 256` was a hard ceiling on *simultaneously visible* entities that Glowmere's own
// 278-entity scene already exceeded; it survived only because view-frustum culling kept a typical
// camera's set to about half of it. The replacement is an allocation policy rather than a bigger
// number, and the exit criterion for the phase is a hard one: a scene with more than 256 visible
// entities renders correctly.
//
// "Correctly" is asked of the identifier target rather than of a pixel count, because a pixel count
// is exactly what would not notice. Two traps are being avoided on purpose:
//
//   * A scene with a big floor in it covers most of the frame legitimately, so "two thirds of the
//     pixels are claimed" stays true even when every claim is wrong. testing::denseEntityGrid has
//     no floor for that reason, and these tests check *which* entity claimed each pixel.
//   * `packPickId(PickSpace::Entity, 0)` is literally `0`, so entity zero's pick id and "nothing
//     was drawn here" are the same bits in the low half of the texel. What separates them is the
//     material id occupying the high 16 bits and being **one-based**. So the sky is checked to be
//     claimed by *nobody* -- a fully zero texel -- and entity zero is checked to be claimed by
//     entity zero, with a non-zero material id. Either half alone passes while the bug is present.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "scene/scene_types.hpp"
#include "support/dense_scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 480;
constexpr int kGridSide = 19;
constexpr std::uint32_t kEntities = kGridSide * kGridSide; // 361

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

void renderOnce(rendering::SceneRenderer& renderer, const scene::Scene& s, std::uint64_t frame = 0) {
    FrameTime time{};
    time.renderTime = 0.0;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = frame;
    REQUIRE(renderer.renderFrame(s, time, kWidth, kHeight).has_value());
}

// The identifier target's two halves, named. Material ids are one-based at the point they are
// written (scene_renderer.cpp writes `thisEntity + 1`), which is the whole reason a drawn entity
// zero is distinguishable from an untouched texel.
struct Claim {
    std::uint32_t objectId = 0;
    std::uint32_t materialId = 0;
    [[nodiscard]] bool empty() const { return objectId == 0 && materialId == 0; }
};
Claim claimOf(std::uint32_t texel) { return {texel & 0xFFFFu, texel >> 16}; }

std::vector<std::uint32_t> readIds(gpu::Context& context, rendering::SceneRenderer& renderer) {
    auto ids = gpu::readTextureR32Uint(context, renderer.identifierTexture(), kWidth, kHeight);
    REQUIRE(ids.has_value());
    return std::move(*ids);
}

// Opt-in eyeball: `AVGEN_CAPACITY_CAPTURE=/tmp/dense.ppm avgen_render_tests "[capacity]"` writes the
// shaded frame out. The assertions below are about the identifier target, which is the right thing
// to assert on and the wrong thing to look at -- and a reviewer being able to look at the fixture is
// what stops it quietly degenerating into a scene where the answers are right for the wrong reason.
void captureIfAsked(gpu::Context& context, rendering::SceneRenderer& renderer, const scene::Scene& s) {
    const char* path = std::getenv("AVGEN_CAPACITY_CAPTURE");
    if (path == nullptr) {
        return;
    }
    FrameTime time{};
    time.deltaTime = 1.0 / 60.0;
    auto image = renderer.renderToImage(s, time, kWidth, kHeight);
    REQUIRE(image.has_value());
    (void)gpu::writePpm(*image, std::filesystem::path(path));
    (void)context;
}

} // namespace

TEST_CASE("A scene with more than 256 visible entities draws every one of them",
          "[gpu][renderer][capacity]") {
    // The phase's hard number. 361 entities, none culled, none sharing a slot with another.
    static_assert(kEntities > rendering::SceneRenderer::kInitialObjects,
                  "the fixture has to exceed the initial allocation or it tests nothing");

    auto context = makeContext();
    gpu::ShaderLibrary shaders(*context, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene s = testing::denseEntityGrid(kGridSide);
    REQUIRE(s.entities.size() == kEntities);
    renderOnce(renderer, s);

    // The buffer grew rather than the draws being dropped. Both halves matter: a renderer that
    // simply raised the constant would pass the first and a renderer that silently skipped
    // entities would pass the second.
    CHECK(renderer.objectCapacity() >= kEntities);
    CHECK(renderer.stats().entities == kEntities);

    // Every entity owns at least one pixel of the frame, and the index it claims is its own. The
    // grid pitch against the box size is what makes this answerable: no box hides another.
    captureIfAsked(*context, renderer, s);
    const std::vector<std::uint32_t> ids = readIds(*context, renderer);
    std::set<std::uint32_t> drawn;
    std::uint32_t background = 0;
    for (const std::uint32_t texel : ids) {
        const Claim claim = claimOf(texel);
        if (claim.empty()) {
            ++background;
            continue;
        }
        // A claimed pixel names an entity, in the entity numbering, with a one-based material id
        // that agrees with it. A slot collision -- two entities writing the same object slot --
        // shows up here as a material id that does not match the object id.
        INFO("texel " << texel << " object " << claim.objectId << " material " << claim.materialId);
        REQUIRE(scene::pickSpaceOf(claim.objectId) == scene::PickSpace::Entity);
        const std::uint32_t index = scene::pickIndexOf(claim.objectId);
        REQUIRE(index < kEntities);
        REQUIRE(claim.materialId == index + 1);
        drawn.insert(index);
    }
    // Naming the missing entities matters: "349 of 361" cannot tell a cap that truncated the tail
    // from a fixture whose framing lost an edge row, and those want opposite fixes.
    std::string missing;
    for (std::uint32_t i = 0; i < kEntities; ++i) {
        if (drawn.count(i) == 0) {
            missing += std::to_string(i) + " ";
        }
    }
    INFO(drawn.size() << " of " << kEntities << " entities claimed a pixel; missing: " << missing);
    CHECK(drawn.size() == kEntities);

    // And there is real sky to have been claimed by nobody -- the assertion below is only worth
    // making because the fixture has no floor. Under a tenth of the frame and the sky check is
    // measuring a sliver rather than a background.
    CHECK(background > (kWidth * kHeight) / 10);
}

TEST_CASE("The sky over a dense scene is claimed by nobody, and entity zero is claimed by entity zero",
          "[gpu][renderer][capacity]") {
    // The trap, stated as a test. `packPickId(PickSpace::Entity, 0) == 0`, so if the material id
    // ever stopped being one-based, every sky texel would read as a hit on entity zero and every
    // pixel count in the suite would stay exactly the same.
    static_assert(scene::packPickId(scene::PickSpace::Entity, 0) == 0u,
                  "entity zero's pick id is zero; the material id is what makes a drawn texel non-empty");

    auto context = makeContext();
    gpu::ShaderLibrary shaders(*context, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene s = testing::denseEntityGrid(kGridSide);
    renderOnce(renderer, s);
    const std::vector<std::uint32_t> ids = readIds(*context, renderer);

    // The top row of the frame is above the horizon in this fixture: nothing in the scene reaches
    // it, so every texel there must be untouched -- not "entity zero", not anything.
    for (std::uint32_t x = 0; x < kWidth; ++x) {
        INFO("sky texel at x=" << x << " reads " << ids[x]);
        REQUIRE(ids[x] == 0u);
    }

    // Entity zero is drawn, and it is drawn as entity zero: object id 0 with material id 1. If this
    // is absent the sky test above has become vacuous -- it would pass on a renderer that drew
    // nothing at all.
    bool sawEntityZero = false;
    for (const std::uint32_t texel : ids) {
        if (texel == (0u | (1u << 16))) {
            sawEntityZero = true;
            break;
        }
    }
    CHECK(sawEntityZero);
}

TEST_CASE("The object buffer grows between frames without disturbing the frame that follows",
          "[gpu][renderer][capacity]") {
    // Growth replaces the buffer and every bind group that names it, including the skinned path's
    // group 1 (ADR-086), which holds its own reference. A renderer that grew the buffer but left a
    // bind group pointing at the old one is the failure mode this exercises: render small, render
    // large, render small again on the same renderer, and check the last frame is still right.
    auto context = makeContext();
    gpu::ShaderLibrary shaders(*context, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene small = testing::denseEntityGrid(8); // 64 entities
    renderOnce(renderer, small, 0);
    CHECK(renderer.objectCapacity() == rendering::SceneRenderer::kInitialObjects);
    CHECK(renderer.stats().entities == 64u);

    const scene::Scene large = testing::denseEntityGrid(kGridSide);
    renderOnce(renderer, large, 1);
    const std::uint32_t grown = renderer.objectCapacity();
    CHECK(grown >= kEntities);
    CHECK(renderer.stats().entities == kEntities);

    // Back to the small scene: the capacity does not shrink (a camera turn that drops the count is
    // about to raise it again, and reallocating on that is churn), and the frame is unaffected.
    renderOnce(renderer, small, 2);
    CHECK(renderer.objectCapacity() == grown);
    CHECK(renderer.stats().entities == 64u);

    const std::vector<std::uint32_t> ids = readIds(*context, renderer);
    std::set<std::uint32_t> drawn;
    for (const std::uint32_t texel : ids) {
        const Claim claim = claimOf(texel);
        if (claim.empty()) {
            continue;
        }
        REQUIRE(claim.materialId == scene::pickIndexOf(claim.objectId) + 1);
        drawn.insert(scene::pickIndexOf(claim.objectId));
    }
    CHECK(drawn.size() == 64u);
}
