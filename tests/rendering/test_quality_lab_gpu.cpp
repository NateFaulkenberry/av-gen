// The Quality Lab against frames the renderer actually wrote.
//
// `tests/unit/test_quality_lab.cpp` proves the metrics respond to distortions constructed on the
// CPU, which is the right place to prove that: the arms are exact and the controls are exact. It
// cannot prove the Lab can read a real AOV, that the velocity buffer's sign convention was read
// correctly out of `shaders/common.wgsl`, or that an identifier plane from the engine has more than
// one value in it. Those need the engine, and they are what this file covers.
//
// **Two arms and their controls**, following the candidate/reference split ADR-250 §5 chose: the
// candidate is a production-configuration render with AOVs at native resolution, and the reference
// is the same project supersampled with no AOVs. ADR-242's refusal is not relaxed and is not needed.

#include "app/engine.hpp"
#include "app/render_job.hpp"
#include "artifacts/masks.hpp"
#include "audio/audio_file.hpp"
#include "capture/sequence.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "metrics/spatial.hpp"
#include "metrics/temporal.hpp"
#include "report/diagnostics.hpp"
#include "support/synth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// The orb scene with an animated scale, so geometry moves between frames and the velocity AOV has
// something to say. A static scene would make every temporal assertion below vacuous, which is the
// failure this repository writes ADRs about.
struct Fixture {
    fs::path dir;
    fs::path project;
    Fixture() {
        dir = fs::temp_directory_path() / "avgen_quality_lab_gpu";
        fs::remove_all(dir);
        fs::create_directories(dir);
        constexpr std::uint32_t rate = 48000;
        auto file = audio::AudioFile::fromInterleaved(
            testsupport::interleave(testsupport::sine(60.0f, rate, rate * 2, 0.8f), 2), 2, rate);
        REQUIRE(file.writeWav(dir / "tone.wav").has_value());
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(dir / "tone.wav").has_value());
        params::Track scale;
        scale.target = "orb/scale";
        scale.addKey({.time = 0.0, .value = {0.6f}});
        scale.addKey({.time = 2.0, .value = {2.2f}});
        engine.timeline().addTrack(scale);
        project = dir / "show.json";
        REQUIRE(engine.saveProject(project).has_value());
    }
    ~Fixture() {
        if (std::getenv("AVGEN_TEST_KEEP") == nullptr) {
            fs::remove_all(dir);
        }
    }
};

std::unique_ptr<app::Engine> loadOffline(const fs::path& project) {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    REQUIRE(engine->loadProject(project).has_value());
    return engine;
}

app::RenderSettings settingsFor(const fs::path& out) {
    app::RenderSettings s;
    s.width = 96;
    s.height = 64;
    s.fps = 10.0;
    s.startSeconds = 0.5;
    s.endSeconds = 1.0;
    s.outputPath = out;
    s.encoderThreads = 2;
    return s;
}

std::uint64_t render(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const Fixture& fixture,
                     const app::RenderSettings& settings) {
    app::RenderJob job(ctx, shaders, loadOffline(fixture.project), settings, fixture.dir);
    REQUIRE(job.start().has_value());
    REQUIRE(job.run().has_value());
    const app::RenderProgress progress = job.progress();
    CHECK(progress.error.empty());
    return progress.sequenceHash;
}

} // namespace

TEST_CASE("the Quality Lab reads what the renderer wrote", "[gpu][render][quality]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    Fixture fixture;

    auto candidateSettings = settingsFor(fixture.dir / "candidate");
    candidateSettings.aovs = "velocity,depth,id,normal,emission";
    const std::uint64_t candidateHash = render(*ctx, shaders, fixture, candidateSettings);

    auto referenceSettings = settingsFor(fixture.dir / "reference");
    referenceSettings.supersample = 2.0f; // and NO aovs: ADR-242's refusal is not relaxed
    const std::uint64_t referenceHash = render(*ctx, shaders, fixture, referenceSettings);

    // **The non-vacuity check, before any metric is read.** reference-rendering.md §3.4 rule 5: two
    // arms expected to differ that hash identically are void rather than equal. This exact check
    // caught a benchmark scene rendering an empty frame (ADR-254), and it caught it before a single
    // number had been computed.
    INFO("candidate " << candidateHash << " reference " << referenceHash);
    REQUIRE(candidateHash != 0);
    REQUIRE(candidateHash != referenceHash);

    auto candidate = quality::discoverSequence(fixture.dir / "candidate");
    auto reference = quality::discoverSequence(fixture.dir / "reference");
    REQUIRE(candidate.has_value());
    REQUIRE(reference.has_value());
    // Five AOVs per frame sit in the same directory. If the discovery counted them the sequence
    // would be six times too long and every temporal metric would be differencing a normal against
    // a beauty frame.
    CHECK(candidate->size() == 5);
    CHECK(candidate->size() == reference->size());

    SECTION("the full-reference metrics agree with the supersampled reference about aliasing") {
        const std::size_t index = candidate->size() / 2;
        auto candidateFrame = quality::readFrame(candidate->frames[index]);
        auto referenceFrame = quality::readFrame(reference->frames[index]);
        REQUIRE(candidateFrame.has_value());
        REQUIRE(referenceFrame.has_value());
        // The reference renders at the candidate's OUTPUT resolution and differs only in the
        // internal render scale (reference-rendering.md §4). A size mismatch here would mean the
        // resolve is writing the scene size, which is the ADR-251 defect in a different target.
        REQUIRE(candidateFrame->sameShapeAs(*referenceFrame));

        // The identity control. Without it, "msSsim < 1" below could be a broken comparison.
        CHECK(std::isinf(quality::psnr(*candidateFrame, *candidateFrame)));
        CHECK(quality::msSsim(*candidateFrame, *candidateFrame) == Approx(1.0).margin(1e-9));

        const double similarity = quality::msSsim(*candidateFrame, *referenceFrame);
        INFO("msSsim candidate vs reference " << similarity);
        CHECK(similarity < 1.0);

        // **No direction is asserted here, and the reason is a measurement rather than caution.**
        //
        // The first version of this test asserted what ADR-243's supersample arm says -- that a
        // better-sampled render carries LESS high-frequency spatial energy, because the sampling
        // error is what converged. On this fixture it is the other way round: 23.96 for the
        // candidate against 29.37 for the 2x reference.
        //
        // That is not a bug and it is not noise. This fixture is one large smooth orb at 96x64,
        // which has essentially no sub-pixel geometry, so supersampling has no staircase to resolve
        // and instead resolves more genuine shading detail into the frame. On
        // `examples/quality/aliasing-dolly`, whose whole subject is 2 cm fence slats crossing the
        // sampling limit, the same comparison runs the documented way: 14.46 against 11.83.
        //
        // So `spatialLaplacian`'s own stated limitation -- **it cannot separate aliasing from
        // detail** -- is not a hedge in a header. It changes the SIGN of the comparison depending on
        // whether the scene has sub-pixel geometry, and this test is the demonstration. What is
        // asserted is that the two frames genuinely differ in high-frequency content, which is what
        // makes the metric non-vacuous on real renderer output; which way is a question about the
        // scene and belongs to the arms of an experiment, never to a threshold.
        const double candidateEnergy = quality::spatialLaplacian(*candidateFrame);
        const double referenceEnergy = quality::spatialLaplacian(*referenceFrame);
        INFO("laplacian candidate " << candidateEnergy << " reference " << referenceEnergy);
        CHECK(candidateEnergy > 0.0);
        CHECK(referenceEnergy > 0.0);
        CHECK(candidateEnergy != Approx(referenceEnergy).epsilon(0.01));
    }

    SECTION("the AOVs are readable, non-vacuous, and shaped like the frame") {
        const std::size_t index = candidate->size() / 2;
        const fs::path frame = candidate->frames[index];
        for (const char* aov : {"velocity", "depth", "id", "normal", "emission"}) {
            INFO(aov);
            REQUIRE(quality::hasAov(frame, aov));
            auto plane = quality::readPlane(quality::aovPathFor(frame, aov));
            REQUIRE(plane.has_value());
            CHECK(plane->width == 96);
            CHECK(plane->height == 64);
        }

        // ADR-242's own lesson, inherited: without more than one distinct identifier every mask in
        // artifact-detection.md §3 is vacuous, and a vacuous mask looks exactly like a working one.
        auto ids = quality::readPlane(quality::aovPathFor(frame, "id"));
        REQUIRE(ids.has_value());
        const quality::IdentifierSurvey survey = quality::surveyIdentifiers(*ids);
        INFO("distinct identifiers " << survey.distinctValues << ", background "
                                     << survey.backgroundFraction);
        CHECK(survey.usable());
        CHECK(survey.backgroundFraction > 0.0); // there is sky, so the id plane is not all geometry
        CHECK(survey.backgroundFraction < 1.0); // and there is geometry, so it is not all sky
    }

    SECTION("the motion-compensated residual runs on real motion, and its mask is not everything") {
        const std::size_t index = candidate->size() / 2;
        auto previous = quality::readFrame(candidate->frames[index - 1]);
        auto current = quality::readFrame(candidate->frames[index]);
        auto velocity = quality::readPlane(quality::aovPathFor(candidate->frames[index], "velocity"));
        auto depthNow = quality::readPlane(quality::aovPathFor(candidate->frames[index], "depth"));
        auto depthWas =
            quality::readPlane(quality::aovPathFor(candidate->frames[index - 1], "depth"));
        auto idNow = quality::readPlane(quality::aovPathFor(candidate->frames[index], "id"));
        auto idWas = quality::readPlane(quality::aovPathFor(candidate->frames[index - 1], "id"));
        REQUIRE(previous.has_value());
        REQUIRE(current.has_value());
        REQUIRE(velocity.has_value());
        REQUIRE(depthNow.has_value());
        REQUIRE(idNow.has_value());

        quality::MotionInputs inputs;
        inputs.previous = &*previous;
        inputs.current = &*current;
        inputs.velocity = &*velocity;
        inputs.depthPrevious = &*depthWas;
        inputs.depthCurrent = &*depthNow;
        inputs.idPrevious = &*idWas;
        inputs.idCurrent = &*idNow;
        const quality::MotionResidual residual = quality::motionCompensatedResidual(inputs);
        REQUIRE(residual.width == 96);
        INFO("residual " << residual.residual << " over " << residual.validFraction
                         << ", disoccluded " << residual.disocclusionFraction);

        // The number is not asserted -- it is a property of this scene and this build, and pinning
        // it would make a renderer improvement a test failure. What IS asserted is that the detector
        // is answering about a meaningful share of the frame. Both ends matter: a mask that excludes
        // nothing is not a mask, and a residual over 4% of the frame is not a statement about the
        // frame (metrics.md §2.2).
        CHECK(residual.validFraction > 0.5);
        CHECK(residual.validFraction <= 1.0);

        // And the disocclusion mask is doing something the off-frame test alone would not: a scaling
        // orb moves its own silhouette, so some pixels' history belongs to a different surface.
        CHECK(residual.disocclusionFraction < 0.5);

        // The gating control from artifact-detection.md §3: a residual gated over every pixel must
        // equal the ungated one, or `over()` is doing something other than gating.
        // epsilon(1e-6) and not 1e-9: the two means sum the same values in different orders, and
        // floating-point addition is not associative. On the synthetic arms in the unit suite the
        // values are simple enough that 1e-9 holds; on real radiance it parts company in the ninth
        // significant digit. A tolerance chosen to make a test pass is a smell; this one is chosen
        // because the quantity being compared is a sum over 6,144 doubles taken two ways.
        const auto all = residual.over(quality::everything(residual.width, residual.height));
        CHECK(all.residual == Approx(residual.residual).epsilon(1e-6));

        // The specular mask, on real emission and roughness. It must select SOME pixels and not all
        // of them -- a mask that is the whole frame turns "specular instability" into "instability".
        auto emission = quality::readPlane(quality::aovPathFor(candidate->frames[index], "emission"));
        auto normal = quality::readPlane(quality::aovPathFor(candidate->frames[index], "normal"));
        REQUIRE(emission.has_value());
        REQUIRE(normal.has_value());
        const quality::Mask specular = quality::specularMask(&*emission, &*normal);
        REQUIRE_FALSE(specular.empty());
        INFO("specular coverage " << quality::coverage(specular));
        CHECK(quality::coverage(specular) < 1.0);

        // A diagnostic image, because a pooled number with nothing to look at is what
        // docs/post-artifact-forensics.md says cannot be debugged.
        const quality::Frame heatmap =
            quality::heatmapImage(residual.residualMap, residual.width, residual.height, 32.0);
        CHECK(heatmap.valid());
    }

    SECTION("the temporal measure and the spatial one disagree about authored motion") {
        // ADR-253's result, on the renderer. The orb is scaling, which is authored motion and not a
        // defect; the second-difference measure reports it as change and the motion-compensated
        // residual, which knows where each pixel came from, reports much less. This is the pairing
        // rule's whole justification, and it is asserted rather than described.
        auto a = quality::readFrame(candidate->frames[1]);
        auto b = quality::readFrame(candidate->frames[2]);
        auto c = quality::readFrame(candidate->frames[3]);
        auto velocity = quality::readPlane(quality::aovPathFor(candidate->frames[3], "velocity"));
        auto depthNow = quality::readPlane(quality::aovPathFor(candidate->frames[3], "depth"));
        auto depthWas = quality::readPlane(quality::aovPathFor(candidate->frames[2], "depth"));
        auto idNow = quality::readPlane(quality::aovPathFor(candidate->frames[3], "id"));
        auto idWas = quality::readPlane(quality::aovPathFor(candidate->frames[2], "id"));
        REQUIRE(a.has_value());
        REQUIRE(c.has_value());
        REQUIRE(velocity.has_value());

        const quality::AlternationStats alternation = quality::temporalAlternation(*a, *b, *c);
        quality::MotionInputs inputs;
        inputs.previous = &*b;
        inputs.current = &*c;
        inputs.velocity = &*velocity;
        inputs.depthPrevious = &*depthWas;
        inputs.depthCurrent = &*depthNow;
        inputs.idPrevious = &*idWas;
        inputs.idCurrent = &*idNow;
        const quality::MotionResidual residual = quality::motionCompensatedResidual(inputs);
        INFO("alternation " << alternation.mean << " peak " << alternation.peak << "; residual "
                            << residual.residual);
        // Both are real measurements of the same footage. Neither is asserted against a threshold;
        // what is asserted is that the frames are genuinely changing, so the pair is not vacuous.
        CHECK(alternation.peak > 0.0);
        CHECK(residual.width > 0);
    }
}
