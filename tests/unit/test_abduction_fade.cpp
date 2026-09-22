// The abduction fade, measured on the animal's own drawn meshes -- played, and seeked.
//
// WHY THIS FILE EXISTS AND WHAT IT ADDS TO THE TWO THAT WERE ALREADY HERE.
//
// The owner reported "animals are not fading out at the top of the animation like we added", and
// the two tests nearest the fade both passed. Neither could have seen it:
//
//   * `test_abduction_alignment.cpp` measures the *glow*. Its per-lift record holds `peakGlow` and
//     `lastGlow`, sampled from `nodes/<animal>/emissiveBoost`, and the words "fade" and "fades
//     away" in its comments are about that parameter. It never reads `opacity` at all -- and it
//     loads `glowmere-valley-2.scene.json`, not the film's project, so the numbers it is
//     measuring are not the ones the film runs (ADR-264: a project's parameters are applied over
//     its scene). A fade that had stopped being written, or stopped reaching the renderer, would
//     leave every assertion in that file green.
//
//   * `test_abduction_sequence.cpp` does read `opacity`, and reads it well: continuity, the
//     ordering against `retire`, the fraction of the climb, and -- in "the fade reaches the
//     flattened scene and is given back" -- the count of blended entities in the scene. But that
//     last one is an *aggregate*: `blended` counts every entity in the flattened scene whose alpha
//     mode is Blend, and `minOpacity` is the least opaque of them, whoever they are. Measured, the
//     multicam draws four blended entities before anything fades (alien visors, at 0.444), so the
//     count moving and the minimum falling are both satisfiable by something that is not the
//     animal.
//
// So what this file measures is the animal's OWN meshes: for each lift, the entities of the
// flattened scene that belong to the bound target's node, and their material opacity and alpha
// mode. That is the last thing a CPU test can see before the GPU, and it is specific.
//
// It also measures the two things the report turned out to be about, one in each direction:
//
//   1. The fade works. Every lift of the film reaches zero opacity on the animal's own meshes, is
//      partway through at the middle of its own fade window, and the *seeked* path agrees with the
//      played one -- which is the arm the merge put at risk (ADR-671 replays the director inside
//      `seek`, ADR-700 resumes it from a checkpoint). Asserted rather than assumed, because it is
//      the arm that had no test before and because "it still works" is a claim that needs
//      evidence too.
//   2. The shadow does not. A node under 1.0 opacity is promoted to `AlphaMode::Blend`, and the
//      renderer excluded blended entities from the shadow pass, so the animal stopped casting on
//      the first frame of its dissolve. `casterEligibility` is the function the shadow pass asks;
//      this checks its verdict on the animal on every frame of every lift, which is the cheap
//      half of that assertion. The expensive half -- that the shadow on the ground actually
//      fades, in pixels -- is `tests/rendering/test_abduction_fade_gpu.cpp`.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "rendering/shadow_math.hpp"
#include "scene/composition.hpp"
#include "stage/staging.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path filmProject() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
}

bool farmAssetsPresent() {
    return fs::is_regular_file(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb");
}

// The bound animal's own meshes, on one frame.
struct Drawn {
    int meshes = 0;
    int blended = 0;
    int casters = 0;   // `casterEligibility` says the shadow pass would draw it
    float opacity = -1.0f; // the least opaque of them; they share one node's scale
};

Drawn drawnOf(const scene::Composition& comp, const std::string& node) {
    Drawn d;
    for (std::size_t i = 0; i < comp.scene().entities.size(); ++i) {
        const scene::CompositionNode* owner = comp.nodeForEntity(i);
        if (owner == nullptr || owner->name != node) {
            continue;
        }
        const scene::Entity& e = comp.scene().entities[i];
        ++d.meshes;
        if (e.material.alphaMode == scene::AlphaMode::Blend) {
            ++d.blended;
        }
        if (rendering::casts(rendering::casterEligibility(e))) {
            ++d.casters;
        }
        d.opacity = d.opacity < 0.0f ? e.material.opacity : std::min(d.opacity, e.material.opacity);
    }
    return d;
}

struct Frame {
    double t = 0.0;
    std::string beat;
    std::string target;
    float base = -1.0f;  // what the director wrote
    float value = -1.0f; // and what it is after the modulation routes, which is what is applied
    Drawn drawn;
};

// One lift, reduced to the handful of numbers the report is about.
struct Lift {
    std::string animal;
    double start = 0.0;
    double end = 0.0;
    int frames = 0;
    bool hasParameter = false;
    float lowestBase = 2.0f;
    float lowestDrawn = 2.0f;
    double lowestAt = 0.0;
    double firstBelowOne = -1.0;
    float worstParamToDrawn = 0.0f; // the biggest gap between the final value and the material
    int fadingFrames = 0;           // frames with 0 < opacity < 1
    int blendedWhileFading = 0;
    int castingWhileFading = 0;
};

// The film, played once. Static because three cases ask for it and it is a project load plus
// three thousand simulated frames; 52 s is the first three complete lifts (10.25, 28.43, 46.62 s),
// which is what makes the per-lift assertions below plural without paying for the whole piece.
struct Film {
    app::Engine engine{app::EngineMode::Offline};
    scene::Composition* comp = nullptr;
    std::vector<Frame> frames;
    std::vector<Lift> lifts;
    bool ok = false;

    Film() {
        if (!farmAssetsPresent()) {
            return;
        }
        auto loaded = engine.loadProject(filmProject());
        if (!loaded.has_value()) {
            return;
        }
        comp = engine.composition();
        if (comp == nullptr) {
            return;
        }
        comp->setViewport(1600, 900);
        // ADR-186's offline setting, which is what the shipped render block asks for
        // ("limits": "unlimited"): every entity every frame, however far from the view.
        comp->scene().detailLimits.entityDistanceCull = false;
        play();
        reduce();
        ok = true;
    }

    static constexpr double kHz = 60.0;
    static constexpr double kSeconds = 52.0;

    Frame sample(const FrameTime& time) {
        Frame f;
        f.t = time.renderTime;
        f.beat = std::string(comp->director().beat("abduction"));
        f.target = std::string(comp->director().binding("abduction", "target"));
        if (!f.target.empty()) {
            if (const params::IParameter* p =
                    engine.params().find("nodes/" + f.target + "/opacity")) {
                f.base = p->baseComponent(0);
                f.value = p->finalComponent(0);
            }
            f.drawn = drawnOf(*comp, f.target);
        }
        return f;
    }

    void play() {
        FrameTime time;
        const auto count = static_cast<int>(kSeconds * kHz);
        frames.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            time.renderTime = static_cast<double>(i) / kHz;
            time.deltaTime = i == 0 ? 0.0 : 1.0 / kHz;
            time.frameIndex = static_cast<std::uint64_t>(i);
            engine.update(time);
            frames.push_back(sample(time));
        }
    }

    void reduce() {
        std::string current;
        for (const Frame& f : frames) {
            if (f.beat != "abduct" || f.target.empty()) {
                current.clear();
                continue;
            }
            if (f.target != current) {
                current = f.target;
                lifts.push_back(Lift{.animal = current, .start = f.t});
            }
            Lift& l = lifts.back();
            l.end = f.t;
            ++l.frames;
            if (f.base < 0.0f) {
                continue;
            }
            l.hasParameter = true;
            if (f.base < l.lowestBase) {
                l.lowestBase = f.base;
                l.lowestAt = f.t;
            }
            if (l.firstBelowOne < 0.0 && f.base < 0.999f) {
                l.firstBelowOne = f.t;
            }
            if (f.drawn.meshes > 0) {
                l.lowestDrawn = std::min(l.lowestDrawn, f.drawn.opacity);
                l.worstParamToDrawn =
                    std::max(l.worstParamToDrawn, std::abs(f.drawn.opacity - f.value));
            }
            if (f.base > 0.0f && f.base < 0.999f) {
                ++l.fadingFrames;
                if (f.drawn.blended == f.drawn.meshes && f.drawn.meshes > 0) {
                    ++l.blendedWhileFading;
                }
                if (f.drawn.casters == f.drawn.meshes && f.drawn.meshes > 0) {
                    ++l.castingWhileFading;
                }
            }
        }
    }

    // The lifts that finished inside the window. A lift the run ended in the middle of says
    // nothing either way about where its fade got to.
    [[nodiscard]] std::vector<Lift> complete() const {
        std::vector<Lift> out;
        for (const Lift& l : lifts) {
            if (l.frames > 250) { // a lift is 4.6 s at 60 Hz = 276 frames
                out.push_back(l);
            }
        }
        return out;
    }
};

const Film& film() {
    static const Film f;
    return f;
}

} // namespace

// ---- the fade, on the animal's own meshes --------------------------------------------------------

TEST_CASE("the abducted animal's own meshes fade to nothing by the top of the lift",
          "[stage][abduction][fade]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    const Film& f = film();
    REQUIRE(f.ok);
    const std::vector<Lift> lifts = f.complete();
    for (const Lift& l : lifts) {
        fmt::print("  {:<10} t={:7.3f}..{:7.3f} ({:3d} f)  lowest param={:6.4f} drawn={:6.4f} at "
                   "{:7.3f}  first<1 at {:7.3f}  param-to-material gap {:.4f}  fading {:3d} f, "
                   "blended {:3d}, casting {:3d}\n",
                   l.animal, l.start, l.end, l.frames, l.lowestBase, l.lowestDrawn, l.lowestAt,
                   l.firstBelowOne, l.worstParamToDrawn, l.fadingFrames, l.blendedWhileFading,
                   l.castingWhileFading);
    }
    // Three complete lifts in 52 s, which is what makes every loop below plural. A run that had
    // lifted nothing would satisfy them all (ADR-182).
    INFO(fmt::format("{} complete lift(s) of {}", lifts.size(), f.lifts.size()));
    REQUIRE(lifts.size() >= 3);

    for (const Lift& l : lifts) {
        INFO(fmt::format("{} lifted from t={:.3f} to t={:.3f}", l.animal, l.start, l.end));
        // There is something to fade at all. `test_abduction_sequence` found a cast where two
        // birds had no node, so the director abducted them invisibly; that is the failure this
        // guards, and it is the reason the gap below is worth measuring.
        REQUIRE(l.hasParameter);
        REQUIRE(l.lowestDrawn <= 1.0f);
        // It got all the way out, in the parameter AND on the material. The pair is the point:
        // the number reaching zero while the material does not is exactly the shape of "the fade
        // is computed and nobody applies it".
        CHECK(l.lowestBase <= 0.01f);
        CHECK(l.lowestDrawn <= 0.01f);
        // And the material *is* the parameter, not a number of its own: every farm GLB is
        // authored at opacity 1, so `rest * scale` is the scale. Against the FINAL and not the
        // base, which is the distinction a first version of this assertion got wrong and failed
        // on by 0.0008: the scene routes reach `nodes/<animal>/opacity` like any other parameter,
        // so the base the director wrote and the value the material gets differ by up to 0.021 on
        // these lifts, and it is the second of them that is applied.
        CHECK(l.worstParamToDrawn < 0.002f);
        // The fade took time rather than being one frame of ramp. `fadeSeconds` is 1.2 s.
        CHECK(l.lowestAt - l.firstBelowOne > 0.8);
        // Every frame it was partway through, it was in the blend pipeline. Without the
        // promotion `pbr_shade.wgsl` discards an OPAQUE material's alpha and the animal renders
        // solid at every value the fade takes.
        REQUIRE(l.fadingFrames > 30);
        CHECK(l.blendedWhileFading == l.fadingFrames);
    }
}

// ---- and the shadow, which is the half that was broken -------------------------------------------

TEST_CASE("the abducted animal still casts a shadow while it is fading",
          "[stage][abduction][fade][shadows]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    const Film& f = film();
    REQUIRE(f.ok);
    const std::vector<Lift> lifts = f.complete();
    REQUIRE(lifts.size() >= 3);

    for (const Lift& l : lifts) {
        INFO(fmt::format("{}: {} frame(s) partway through the fade, {} of them still a shadow "
                         "caster",
                         l.animal, l.fadingFrames, l.castingWhileFading));
        REQUIRE(l.fadingFrames > 30);
        // Every one of them. Before the fix this was 0 of 71: `casterEligibility` returned
        // `StyleExcluded` for any blended material, so the promotion that makes the fade visible
        // deleted the shadow on the first frame of the dissolve -- an animal in a beam with no
        // shadow for the 1.13 s before it was gone. The verdict now falls out with the body: the
        // depth pass discards an ordered fraction of the caster's texels, so a body at 40%
        // opacity casts 40% of a shadow, and only a body at zero casts none.
        CHECK(l.castingWhileFading == l.fadingFrames);
    }
}

// ---- the seeked path, which has to be the played one ---------------------------------------------

TEST_CASE("a seek into the fade lands on the same opacity a play does",
          "[stage][abduction][fade][seek]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    const Film& f = film();
    REQUIRE(f.ok);

    // Every frame the played run was partway through a fade, in the first lift.
    std::vector<const Frame*> fading;
    for (const Frame& fr : f.frames) {
        if (fr.beat == "abduct" && fr.base > 0.0f && fr.base < 0.999f) {
            fading.push_back(&fr);
        }
    }
    REQUIRE(fading.size() >= 60);
    const double from = fading.front()->t;

    // A fresh engine, because the point is a jump into a film nothing has played: `seekSeconds`
    // resets the director and replays it with the entities (ADR-671), resuming from the nearest
    // simulation checkpoint (ADR-700). That is the path a render that starts mid-film takes and
    // the path a scrub takes, and before ADR-671 it landed somewhere else entirely.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(filmProject()).has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    comp->setViewport(1600, 900);
    comp->scene().detailLimits.entityDistanceCull = false;

    int compared = 0;
    int partway = 0;
    float worst = 0.0f;
    for (int k = 0; k <= 10; ++k) {
        const double t = from + 0.1 * k;
        const Frame* played = nullptr;
        for (const Frame* fr : fading) {
            if (std::abs(fr->t - t) < 0.5 / Film::kHz) {
                played = fr;
                break;
            }
        }
        if (played == nullptr) {
            continue; // past the end of the fade
        }
        engine.seekSeconds(t);
        FrameTime time;
        time.renderTime = t;
        time.deltaTime = 1.0 / Film::kHz;
        time.frameIndex = static_cast<std::uint64_t>(t * Film::kHz);
        engine.update(time);

        const std::string target(comp->director().binding("abduction", "target"));
        INFO(fmt::format("seeked to t={:.3f}", t));
        REQUIRE(target == played->target);
        const params::IParameter* p = engine.params().find("nodes/" + target + "/opacity");
        REQUIRE(p != nullptr);
        const float seeked = p->finalComponent(0);
        const Drawn drawn = drawnOf(*comp, target);
        fmt::print("  t={:7.3f} {:<10} seek={:6.4f} (material {:6.4f})  played={:6.4f}\n", t, target,
                   seeked, drawn.opacity, played->value);
        worst = std::max(worst, std::abs(seeked - played->value));
        ++compared;
        if (seeked > 0.02f && seeked < 0.98f) {
            ++partway;
        }
        // The material the seeked frame would be rendered with, not only the number.
        CHECK(std::abs(drawn.opacity - seeked) < 0.002f);
    }
    // The sampled times have to actually be inside the fade, or "they agree" is two engines
    // agreeing that nothing is happening. Yesterday's broken oracle was exactly this shape.
    INFO(fmt::format("{} time(s) compared, {} of them partway through the fade; worst "
                     "seek-vs-play disagreement {:.4f}",
                     compared, partway, worst));
    REQUIRE(compared >= 8);
    REQUIRE(partway >= 5);
    // One 60 Hz step of the eased curve is about 0.05 at its steepest, and the replay's own step
    // is 1/60 s; a bound of one step is what "the same second" can mean between two integrations.
    CHECK(worst <= 0.06f);
}
