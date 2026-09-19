// Cinematic integration (Image/Look §52.1, §68) on the GPU, and §60/§87: with integration at zero
// the image is unchanged.
//
// The claim is proved here the way the spec demands and not the way that is easy. Two things make
// the easy version worthless:
//
//   * **The chain was never bit-identical to its own input.** `PostProcessor::run` always encodes
//     the composite -- it carries the grade -- and `fs_composite` at default settings is not the
//     identity: `pow(x, 1.0)` lowers to `exp2(log2(x))` and runs twice, `max(colour, 1e-5)` lifts
//     true black, and the 0.18 divide/multiply does not cancel in binary float. So "unchanged"
//     cannot mean "equal to the scene HDR". It means *equal to the same chain without this
//     feature*, which is a differential claim. See docs/image-look-audit.md §1.4.
//   * **A control identical to its arm proves only that the control did not fire** (ADR-182). So
//     every assertion that a hash is *unchanged* is paired here with one that shows the same hash
//     moving when a real setting moves -- and not one representative setting, but each of the four
//     amounts on its own, because four subsystems in one session shipped built, tested, measured
//     and unreachable.
//
// The hash is over the float buffer *before* tone mapping (`renderToImageFloat` reads
// `hdrOutput_`, the post chain's own output), which is what the spec asks for: a difference the
// tone curve would compress away is still a difference.
//
// The end-to-end half of this proof -- the same rendered PNG's sha256 against a binary built at the
// parent commit, where this feature does not exist at all -- is not a Catch2 test and cannot be:
// it needs two binaries. It is recorded in docs/image-look-audit.md §5.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>
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

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
}

// A scene with enough in it that every one of the four controls has something to act on: real
// depth (so the atmospheric pass has a distance that varies across the frame and a sky it must
// leave alone), a bright emitter against a dark surround (so light wrap has an edge and local
// contrast has a range), and saturated colour (so colour integration has chroma to pull).
//
// A flat frame would let three of the four controls pass this test while doing nothing.
scene::Scene lookScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.02f, 0.03f, 0.06f};
    s.environment.brightness = 1.0f;
    s.camera.position = {0.0f, 1.2f, 7.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};

    const auto cube = s.addMesh(scene::makeCube(0.8f));
    auto& near = s.addEntity("near-emitter", cube);
    near.transform.position = {-1.4f, 0.0f, 2.0f};
    near.material.baseColor = {0.05f, 0.05f, 0.05f};
    near.material.emissiveColor = {1.0f, 0.75f, 0.35f};
    near.material.emissiveIntensity = 24.0f;

    auto& mid = s.addEntity("mid-diffuse", cube);
    mid.transform.position = {0.9f, 0.0f, -4.0f};
    mid.material.baseColor = {0.15f, 0.55f, 0.35f};

    auto& far = s.addEntity("far-diffuse", cube);
    far.transform.position = {0.0f, 0.0f, -24.0f};
    far.transform.scale = {6.0f, 6.0f, 1.0f};
    far.material.baseColor = {0.6f, 0.25f, 0.2f};

    // Deliberately leave the rest of the chain at its shipped defaults rather than switching it
    // off. §60's claim is about the *whole* image, and a disabled-path proof taken on a chain that
    // is itself mostly disabled is a weaker statement than the one being made.
    return s;
}

std::uint64_t hashOf(rendering::SceneRenderer& renderer, const scene::Scene& scene) {
    FrameTime time{};
    auto img = renderer.renderToImageFloat(scene, time, 192, 128);
    REQUIRE(img.has_value());
    return gpu::hashImage(*img);
}

// The float buffer behind that hash, kept so a mismatch can say *how* the two frames differ.
//
// A hash is the right thing to assert -- it is exact, and §60 is an exactness claim -- but it is
// the wrong thing to fail with: two unequal 64-bit numbers tell you nothing about whether a
// disabled path leaked one ulp into every pixel or one pass into a corner. ADR-362's `byteDiff`
// gives a count, a first offset and a maximum delta, and it does it without stringifying eight
// megabytes inside a failing assertion handler -- which is the bug that made a real failure in
// `test_wind_gpu.cpp` present as a bus error with a `FAILED:` line and no `with expansion:`.
std::vector<std::uint8_t> bytesOf(rendering::SceneRenderer& renderer, const scene::Scene& scene) {
    FrameTime time{};
    auto img = renderer.renderToImageFloat(scene, time, 192, 128);
    REQUIRE(img.has_value());
    const auto* raw = reinterpret_cast<const std::uint8_t*>(img->rgba.data());
    return {raw, raw + img->rgba.size() * sizeof(float)};
}

} // namespace

TEST_CASE("With cinematic integration at zero the pre-tonemap image is unchanged (§60)",
          "[gpu][post][look]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene base = lookScene();
    REQUIRE_FALSE(base.post.look.active()); // the defaults, restated where it matters

    // ---- the reference: the chain with the feature present and every amount at zero ------------
    const std::uint64_t zero = hashOf(renderer, base);
    const std::vector<std::uint8_t> zeroBytes = bytesOf(renderer, base);

    // Rendering it again must give the same hash, or this whole test is measuring noise rather
    // than the feature. (ADR-037's determinism guarantee, borrowed as a sanity check.)
    {
        const auto d = testing::byteDiff(zeroBytes, bytesOf(renderer, base));
        INFO("zero arm rendered twice: " << d.describe());
        CHECK(d.identical());
    }
    CHECK(hashOf(renderer, base) == zero);

    // ---- the arm that must not move: the *shape* parameters, whose amounts are still zero ------
    // These have non-zero defaults because a shape has no meaningful zero, so they are the one way
    // an "off" system could still touch the image. If moving them moves the hash, some pass is
    // being encoded that should not be.
    {
        scene::Scene s = base;
        s.post.look.atmosphericDistance = 35.0f;
        s.post.look.localContrastRadius = 96.0f;
        s.post.look.atmosphericTint = {1.0f, 0.0f, 0.0f}; // a colour nothing in this scene is
        REQUIRE_FALSE(s.post.look.active());
        const auto d = testing::byteDiff(zeroBytes, bytesOf(renderer, s));
        INFO("shape parameters moved while every amount is zero: " << d.describe());
        CHECK(d.identical());
        CHECK(hashOf(renderer, s) == zero);
    }

    // ---- the controls: each of the four, on its own, must move the hash ------------------------
    // This is the half that stops the assertions above from being vacuous, and it is done one
    // control at a time on purpose. A single combined perturbation would pass even if three of the
    // four were dead -- which is exactly the shape of the four subsystems that shipped unreachable.
    struct Arm {
        const char* name;
        float scene::ImageLookIntegration::*amount;
        float value;
    };
    const Arm arms[] = {
        {"post/look/atmospheric", &scene::ImageLookIntegration::atmospheric, 0.6f},
        {"post/look/colour", &scene::ImageLookIntegration::colour, 0.8f},
        {"post/look/localContrast", &scene::ImageLookIntegration::localContrast, 0.7f},
        {"post/look/lightWrap", &scene::ImageLookIntegration::lightWrap, 0.9f},
    };
    std::vector<std::uint64_t> armHashes;
    for (const Arm& arm : arms) {
        scene::Scene s = base;
        s.post.look.*(arm.amount) = arm.value;
        REQUIRE(s.post.look.active());
        const std::uint64_t moved = hashOf(renderer, s);
        INFO(arm.name << " at " << arm.value);
        CHECK(moved != zero);
        armHashes.push_back(moved);
    }

    // And no two of them are the same change wearing different names -- which would mean one
    // parameter was wired to another's effect, the failure a combined test cannot see either.
    for (std::size_t i = 0; i < armHashes.size(); ++i) {
        for (std::size_t j = i + 1; j < armHashes.size(); ++j) {
            INFO(arms[i].name << " vs " << arms[j].name);
            CHECK(armHashes[i] != armHashes[j]);
        }
    }

    // ---- the cost, stated as a count rather than a duration -------------------------------------
    // §82/§83 want the performance of this, and the load-bearing performance claim is not a
    // millisecond: it is that a scene which does not ask for the feature encodes no pass for it.
    // A pass count is immune to machine contention in a way a wall-clock number is not, so it is
    // the number worth pinning in a test -- this repository has bought the lesson that a timing
    // assertion taken on a shared machine is a flaky test with a respectable-looking face.
    {
        FrameTime time{};
        auto off = renderer.renderToImageFloat(base, time, 192, 128);
        REQUIRE(off.has_value());
        const std::uint32_t passesOff = renderer.stats().post.passes;

        scene::Scene on = base;
        on.post.look.atmospheric = 0.5f;
        auto onImg = renderer.renderToImageFloat(on, time, 192, 128);
        REQUIRE(onImg.has_value());
        const std::uint32_t passesAtmos = renderer.stats().post.passes;

        scene::Scene both = base;
        both.post.look.atmospheric = 0.5f;
        both.post.look.localContrast = 0.5f;
        auto bothImg = renderer.renderToImageFloat(both, time, 192, 128);
        REQUIRE(bothImg.has_value());
        const std::uint32_t passesBoth = renderer.stats().post.passes;

        INFO("post passes: off " << passesOff << ", atmospheric " << passesAtmos << ", both "
                                 << passesBoth);
        // Atmospheric is one pass. The late look stage is four: a quarter-resolution downsample,
        // two separable blur halves, and the combine.
        CHECK(passesAtmos == passesOff + 1);
        CHECK(passesBoth == passesOff + 5);
    }

    // ---- and back to zero, after all of that ---------------------------------------------------
    // The pool is reused across renders and the look stage acquires four textures of its own, so
    // "off" after "on" is a different allocation history from "off" first. It must still be the
    // same image.
    CHECK(hashOf(renderer, base) == zero);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The atmospheric pass leaves the sky alone", "[gpu][post][look]") {
    // §68.1's one correctness property that a hash cannot express. A pixel at the far plane *is*
    // the horizon; tinting it toward the horizon colour a second time washes the frame and destroys
    // the depth cue the control exists to create. So the background must come through bit-identical
    // however strong the effect is, while the geometry must not.
    //
    // Bloom is switched off for this test and the scene is dim, and both are load-bearing. The
    // first version of this test used `lookScene()` as it stands and failed -- correctly. The
    // atmospheric pass runs at stage 1b, *before* bloom, so pulling a bright emitter toward the
    // tint changes the bloom halo that is then composited over the sky, and the sky pixel moves
    // without `fs_look_atmos` having touched it. Measuring the sky in the final composite therefore
    // measures bloom's spread, not the pass's background test. Turning bloom off is what isolates
    // the property being asserted.
    //
    // (The same first version also asserted that the frame gained red. It lost 28 540 units of it,
    // and that was right too: the emitter sits at radiance 24 and the tint at 8, so mixing toward
    // the tint correctly *darkens* it. "More of the tint colour" is not the same claim as "brighter",
    // and on an HDR frame they routinely point in opposite directions.)
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};

    scene::Scene s;
    s.environment.backgroundColor = {0.02f, 0.03f, 0.06f};
    s.environment.brightness = 1.0f;
    s.camera.position = {0.0f, 0.8f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.post.bloomEnabled = false;   // nothing may spread across the frame; see above
    s.post.bloomIntensity = 0.0f;
    const auto cube = s.addMesh(scene::makeCube(1.2f));
    auto& body = s.addEntity("body", cube);
    body.material.baseColor = {0.35f, 0.35f, 0.35f};

    auto before = renderer.renderToImageFloat(s, time, 192, 128);
    REQUIRE(before.has_value());

    s.post.look.atmospheric = 1.0f;
    s.post.look.atmosphericDistance = 4.0f;          // the cube is well past this
    s.post.look.atmosphericTint = {3.0f, 0.0f, 0.0f}; // far brighter than anything in this scene
    auto after = renderer.renderToImageFloat(s, time, 192, 128);
    REQUIRE(after.has_value());

    // ---- the background is bit-identical, in all four corners -------------------------------
    const std::pair<std::uint32_t, std::uint32_t> corners[] = {
        {2, 2}, {before->width - 3, 2}, {2, before->height - 3},
        {before->width - 3, before->height - 3},
    };
    for (const auto& [x, y] : corners) {
        const float* b = before->pixel(x, y);
        const float* a = after->pixel(x, y);
        INFO("corner " << x << "," << y << " before " << b[0] << "," << b[1] << "," << b[2] << " after " << a[0]
                       << "," << a[1] << "," << a[2]);
        CHECK(b[0] == a[0]);
        CHECK(b[1] == a[1]);
        CHECK(b[2] == a[2]);
    }

    // ---- and the geometry did change, or the checks above are vacuous -------------------------
    // Counted, not summed: a sum can cancel, and on an HDR frame a tint that is dimmer than what it
    // replaces moves a pixel a long way in the negative direction. What is being asserted is that a
    // substantial part of the frame moved at all, and that where it moved it moved toward the tint.
    std::size_t changed = 0;
    std::size_t towardTint = 0;
    for (std::uint32_t y = 0; y < after->height; ++y) {
        for (std::uint32_t x = 0; x < after->width; ++x) {
            const float* b = before->pixel(x, y);
            const float* a = after->pixel(x, y);
            if (b[0] == a[0] && b[1] == a[1] && b[2] == a[2]) {
                continue;
            }
            ++changed;
            // The tint is pure red, so a pixel pulled toward it gains red and loses green.
            if (a[0] > b[0] && a[1] <= b[1]) {
                ++towardTint;
            }
        }
    }
    INFO("pixels changed " << changed << " of " << (after->width * after->height) << ", toward the tint "
                           << towardTint);
    CHECK(changed > 200);
    CHECK(towardTint > changed / 2);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Wiring the identifier target changes no frame at the default sharpenId (ADR-372)",
          "[gpu][post][sharpen]") {
    // `post/output/sharpenId` was registered, round-tripping, shader-complete and unable to affect
    // any frame, because nothing assigned `PostFrameInputs::identifier`. Wiring it is one line, and
    // the line has to be default-neutral or it re-bases every render in the repository for a
    // feature nobody asked to turn on.
    //
    // It is neutral by construction -- `fs_sharpen` masks on `identifierAvailable > 0.5 && maskId
    // != 0u`, and `sharpenId` defaults to 0 -- but "by construction" is what the four unreachable
    // subsystems were also said to be. So: prove the default is unchanged, and prove the control
    // now does something, which is the arm that would have failed before this change.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene base = lookScene();
    base.post.sharpen = 0.8f;   // sharpening on, so the mask has something to suppress
    base.post.sharpenId = 0;    // ... and unmasked, which is the default

    const std::uint64_t unmasked = hashOf(renderer, base);
    CHECK(hashOf(renderer, base) == unmasked); // deterministic, or the rest measures noise

    // A non-zero identifier now restricts sharpening to pixels carrying it. Whatever that id
    // selects, it cannot select everything, so the frame must differ from the unmasked one. Before
    // the renderer supplied the target this returned the unmasked frame for every id.
    scene::Scene masked = base;
    masked.post.sharpenId = 1;
    const std::uint64_t maskedHash = hashOf(renderer, masked);
    INFO("sharpenId 0 vs 1 with sharpen at 0.8");
    CHECK(maskedHash != unmasked);

    // And with sharpening off, the id is inert whatever it is set to -- the mask cannot invent
    // work for a pass that does not run.
    scene::Scene off = base;
    off.post.sharpen = 0.0f;
    const std::uint64_t sharpenOff = hashOf(renderer, off);
    scene::Scene offMasked = off;
    offMasked.post.sharpenId = 7;
    CHECK(hashOf(renderer, offMasked) == sharpenOff);
    CHECK(ctx->errorCount() == 0);
}
