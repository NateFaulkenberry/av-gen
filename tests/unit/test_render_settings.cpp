// Milestone 1.0: render settings (ADR-020) — ranges, frame counts, patterns, JSON.

#include "app/render_settings.hpp"

#include "rendering/render_quality.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

using namespace avgen::app;
using Catch::Matchers::WithinAbs;

TEST_CASE("Render settings resolve the range and frame count", "[render][settings]") {
    RenderSettings s;
    s.fps = 30.0;
    CHECK(s.resolvedEnd(12.5, 0.0) == 12.5);      // audio wins
    CHECK(s.resolvedEnd(0.0, 4.0) == 4.0);        // then the timeline
    CHECK(s.resolvedEnd(0.0, 0.0) == 10.0);       // then 10 s
    s.endSeconds = 2.0;
    CHECK(s.resolvedEnd(12.5, 4.0) == 2.0);       // explicit end wins
    CHECK(s.frameCount(2.0) == 60);
    CHECK(s.frameCount(2.0 + 1.0 / 30.0) == 61);
    CHECK(s.frameCount(0.0) == 1);                // never zero
    s.startSeconds = 1.0;
    CHECK(s.frameCount(2.0) == 30);
    s.endSeconds = 0.5;
    CHECK(s.resolvedEnd(0, 0) == 1.0);            // end clamped to start
}

TEST_CASE("Render settings validate and infer the output kind", "[render][settings]") {
    RenderSettings s;
    s.outputPath = "out";
    CHECK(s.validate().has_value());
    s.width = 1279;
    s.output = RenderOutput::Video;
    CHECK_FALSE(s.validate().has_value());        // odd size for video
    s.output = RenderOutput::PngSequence;
    CHECK(s.validate().has_value());
    s.fps = 0.0;
    CHECK_FALSE(s.validate().has_value());
    s.fps = 24.0;
    s.quality = 101;
    CHECK_FALSE(s.validate().has_value());
    s.quality = 50;
    s.pattern = "frame.png";                       // no placeholder
    CHECK_FALSE(s.validate().has_value());
    s.pattern = "f_{:04d}.png";
    CHECK(s.validate().has_value());
    CHECK(s.frameFile("dir", 7).filename() == "f_0007.png");
    CHECK(RenderSettings::outputForPath("x.mov") == RenderOutput::Video);
    CHECK(RenderSettings::outputForPath("x.MP4") == RenderOutput::Video);
    CHECK(RenderSettings::outputForPath("frames") == RenderOutput::PngSequence);
    CHECK(RenderSettings::outputForPath("frames.png") == RenderOutput::PngSequence);
}

TEST_CASE("Render settings know the EXR sequence kind", "[render][settings][json]") {
    RenderSettings s;
    s.output = RenderOutput::ExrSequence;
    CHECK(std::string(renderOutputName(s.output)) == "exr");
    CHECK(isSequence(s.output));
    CHECK_FALSE(isSequence(RenderOutput::Video));
    // The default pattern follows the kind; an explicit pattern is kept.
    s.normalisePattern();
    CHECK(s.pattern == "frame_{:06d}.exr");
    CHECK(s.frameFile("out", 7).filename() == "frame_000007.exr");
    s.output = RenderOutput::PngSequence;
    s.normalisePattern();
    CHECK(s.pattern == "frame_{:06d}.png");
    s.pattern = "shot_{:04d}.exr";
    s.output = RenderOutput::ExrSequence;
    s.normalisePattern();
    CHECK(s.pattern == "shot_{:04d}.exr");
    CHECK(s.validate().has_value());

    auto parsed = RenderSettings::fromJson(nlohmann::json{{"output", "exr"}});
    REQUIRE(parsed.has_value());
    CHECK(parsed->output == RenderOutput::ExrSequence);
    CHECK(parsed->pattern == "frame_{:06d}.exr");
    CHECK(parsed->toJson()["output"] == "exr");
    auto back = RenderSettings::fromJson(parsed->toJson());
    REQUIRE(back.has_value());
    CHECK(back->output == RenderOutput::ExrSequence);
    CHECK(RenderSettings::fromJson(nlohmann::json{{"output", "png"}})->output == RenderOutput::PngSequence);
    CHECK_FALSE(RenderSettings::fromJson(nlohmann::json{{"output", "tiff"}}).has_value());
}

TEST_CASE("Render settings round-trip JSON and reject bad fields", "[render][settings][json]") {
    RenderSettings s;
    s.width = 640;
    s.height = 360;
    s.fps = 25.0;
    s.startSeconds = 1.5;
    s.endSeconds = 9.0;
    s.output = RenderOutput::Video;
    s.outputPath = "renders/show.mov";
    s.codec = "prores422";
    s.backend = "native";
    s.quality = 65;
    s.muxAudio = false;
    s.encoderThreads = 3;
    const auto j = s.toJson();
    auto back = RenderSettings::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->width == 640);
    CHECK(back->height == 360);
    CHECK_THAT(back->fps, WithinAbs(25.0, 1e-12));
    CHECK_THAT(back->startSeconds, WithinAbs(1.5, 1e-12));
    CHECK_THAT(back->endSeconds, WithinAbs(9.0, 1e-12));
    CHECK(back->output == RenderOutput::Video);
    CHECK(back->outputPath == "renders/show.mov");
    CHECK(back->codec == "prores422");
    CHECK(back->backend == "native");
    CHECK(back->quality == 65);
    CHECK_FALSE(back->muxAudio);
    CHECK(back->encoderThreads == 3);

    // Missing fields keep defaults; wrong types and values are errors.
    auto partial = RenderSettings::fromJson(nlohmann::json{{"fps", 24}});
    REQUIRE(partial.has_value());
    CHECK(partial->width == 1920);
    CHECK_THAT(partial->fps, WithinAbs(24.0, 1e-12));
    CHECK_FALSE(RenderSettings::fromJson(nlohmann::json{{"width", "wide"}}).has_value());
    CHECK_FALSE(RenderSettings::fromJson(nlohmann::json{{"output", "gif"}}).has_value());
    CHECK_FALSE(RenderSettings::fromJson(nlohmann::json{{"muxAudio", 1}}).has_value());
    CHECK_FALSE(RenderSettings::fromJson(nlohmann::json{{"fps", -1}}).has_value());
    CHECK_FALSE(RenderSettings::fromJson(nlohmann::json::array()).has_value());
}

// Choosing an output file used to change the output *kind* under the person choosing it.
//
// The Choose... button opened the project save dialog, whose filter is "json", so macOS appended
// `.json` to whatever name was typed. `outputForPath` then read that extension, found no video in
// it, and answered PngSequence -- so picking Video, clicking Choose and typing a name left you with
// a PNG sequence called "my-take.json". Both halves are fixed: the dialog matches the kind, and the
// path is no longer allowed to contradict a kind that has been chosen.
TEST_CASE("A chosen path never silently changes the output kind", "[render][settings]") {
    // An extension that identifies a kind still wins: typing "take.mov" means a video whatever the
    // radio button said, and that is the one case where the path is the better evidence.
    CHECK(RenderSettings::outputForPath("take.mov", RenderOutput::PngSequence) == RenderOutput::Video);
    CHECK(RenderSettings::outputForPath("take.MOV", RenderOutput::ExrSequence) == RenderOutput::Video);
    CHECK(RenderSettings::outputForPath("frames.png", RenderOutput::Video) == RenderOutput::PngSequence);
    CHECK(RenderSettings::outputForPath("frames.exr", RenderOutput::Video) == RenderOutput::ExrSequence);

    // An extension that says nothing keeps the kind that was chosen -- a bare folder name, and the
    // `.json` a project save dialog appends.
    for (const char* path : {"my-take", "renders/tonight", "my-take.json", "my-take.txt"}) {
        INFO(path);
        CHECK(RenderSettings::outputForPath(path, RenderOutput::Video) == RenderOutput::Video);
        CHECK(RenderSettings::outputForPath(path, RenderOutput::ExrSequence) == RenderOutput::ExrSequence);
        CHECK(RenderSettings::outputForPath(path, RenderOutput::PngSequence) == RenderOutput::PngSequence);
    }

    // ...and a video gets a container the muxer understands, so a name typed without one still
    // names a movie rather than a file nothing can play.
    CHECK(RenderSettings::withVideoExtension("my-take").string() == "my-take.mov");
    CHECK(RenderSettings::withVideoExtension("my-take.json").string() == "my-take.mov");
    CHECK(RenderSettings::withVideoExtension("my-take.mp4").string() == "my-take.mp4");   // left alone
    CHECK(RenderSettings::withVideoExtension("my-take.MOV").string() == "my-take.MOV");
    CHECK(RenderSettings::withVideoExtension("").string().empty());
    // The directory is kept: only the extension changes.
    CHECK(RenderSettings::withVideoExtension("renders/tonight.json").parent_path().string() == "renders");

    // A sequence is a directory and a video is a file, which is what decides whether Choose... asks
    // for a folder or a file name.
    CHECK(isSequence(RenderOutput::PngSequence));
    CHECK(isSequence(RenderOutput::ExrSequence));
    CHECK_FALSE(isSequence(RenderOutput::Video));
}

// ADR-147 gave a render its own tier and defaulted it to offline; the editor then gained a control
// for it. The control itself is ImGui and cannot be asserted here, so what is pinned is everything
// underneath it: the default, the round-trip, and the names the combo offers.
//
// This matters more than a normal round-trip test. A batch render spent a long time coming out
// byte-identical to an interactive Realtime frame because nothing asked for a tier, and the way
// that stayed invisible was that no test compared a deliverable against the tier it claimed.
TEST_CASE("a render carries its quality tier", "[render][settings][tier]") {
    SECTION("the default is offline, because a render is a deliverable") {
        const RenderSettings fresh;
        CHECK(fresh.tier == "offline");
    }

    SECTION("every name the editor's combo offers is one the renderer accepts") {
        // The combo in control_panel.cpp lists exactly these. If a name here stopped resolving,
        // the control would silently select a tier the render then refuses.
        for (const char* name : {"preview", "realtime", "high", "offline"}) {
            avgen::rendering::QualityTier tier = avgen::rendering::QualityTier::Realtime;
            INFO("tier name: " << name);
            CHECK(avgen::rendering::qualityTierFromName(name, tier));
            CHECK(std::string(avgen::rendering::qualityTierName(tier)) == name);
        }
    }

    SECTION("it survives a round trip, and an older file without one still loads") {
        RenderSettings s;
        s.tier = "high";
        const auto parsed = RenderSettings::fromJson(s.toJson());
        REQUIRE(parsed.has_value());
        CHECK(parsed->tier == "high");

        // A project written before the field existed: the reader must not reject it, and must not
        // silently downgrade the render either.
        nlohmann::json older = s.toJson();
        older.erase("tier");
        const auto legacy = RenderSettings::fromJson(older);
        REQUIRE(legacy.has_value());
        CHECK(legacy->tier == "offline");
    }
}

// ADR-186: which distance-based detail reductions a render runs under. The word resolves in one
// place so the job, the panel and a test cannot each have their own idea of what "tier" means.
TEST_CASE("Render limits resolve against the render's own tier", "[render][settings][limits]") {
    RenderSettings s;
    CHECK(s.limits == "tier");
    CHECK(s.tier == "offline");

    // The default deliverable: offline, so the reductions that hide something a viewer would
    // otherwise see are lifted -- and the LOD ladder, which is not one of those, is kept.
    //
    // ADR-191. The ladder chooses a representation by projected screen size, and at the sizes it
    // acts on the simpler mesh is the renderer's only prefilter for geometry smaller than the
    // sampling grid. Lifting it drew sub-pixel foliage at full frequency with one sample per pixel
    // and raised flickering area 57%.
    SECTION("tier, at the offline tier, lifts everything except the LOD ladder") {
        const avgen::scene::DetailLimits r = s.resolvedLimits();
        CHECK(r.anyLifted());
        CHECK_FALSE(r.proceduralDistanceCull); // nothing vanishes for being far away
        CHECK(r.proceduralLodRungs);           // but it is still allowed to be simpler out there
        CHECK_FALSE(r.rigDistanceRate);
        CHECK_FALSE(r.entityDistanceCull);
    }

    // A fast proof render is a preview of the live picture, and a preview that quietly drew the
    // far field at full detail would not be previewing what the viewport shows.
    SECTION("tier, below offline, keeps the live picture") {
        s.tier = "realtime";
        CHECK_FALSE(s.resolvedLimits().anyLifted());
        s.tier = "high";
        CHECK_FALSE(s.resolvedLimits().anyLifted());
        s.tier = "preview";
        CHECK_FALSE(s.resolvedLimits().anyLifted());
    }

    SECTION("the two explicit words override the tier in both directions") {
        s.limits = "live"; // offline tier, live limits
        CHECK_FALSE(s.resolvedLimits().anyLifted());
        s.tier = "realtime";
        s.limits = "unlimited"; // realtime tier, lifted anyway
        CHECK(s.resolvedLimits().anyLifted());
        // And "unlimited" still means all four, including the ladder the tier default keeps --
        // otherwise there would be no way to ask for the far field without it at all.
        CHECK_FALSE(s.resolvedLimits().proceduralLodRungs);
    }

    SECTION("a word nobody understands fails validation rather than the render") {
        s.limits = "infinite";
        CHECK_FALSE(s.validate().has_value());
        s.limits = "unlimited";
        CHECK(s.validate().has_value());
    }

    SECTION("it survives the project file") {
        s.limits = "unlimited";
        const auto back = RenderSettings::fromJson(s.toJson());
        REQUIRE(back.has_value());
        CHECK(back->limits == "unlimited");
        CHECK(back->resolvedLimits().anyLifted());
    }

    // An older project has no "limits" key at all, and must keep meaning what it meant.
    SECTION("a project written before this existed still renders") {
        nlohmann::json j = s.toJson();
        j.erase("limits");
        const auto back = RenderSettings::fromJson(j);
        REQUIRE(back.has_value());
        CHECK(back->limits == "tier");
    }
}

// ADR-212. Supersampling: an offline render may spend pixels a realtime one cannot.
TEST_CASE("supersample is validated against the renderer's own ceiling", "[render][settings]") {
    RenderSettings s;
    // Off by default, so a project that never heard of this renders exactly as it did before.
    CHECK(s.supersample == 1.0f);
    CHECK(s.validate().has_value());

    s.supersample = 2.0f;
    CHECK(s.validate().has_value());
    s.supersample = 1.5f;
    CHECK(s.validate().has_value());

    // The ceiling is `QualitySettings::renderScale`'s clamp of 2. A larger number would be silently
    // truncated by the renderer, and a setting that quietly means something other than what it says
    // is worse than one that is refused.
    s.supersample = 4.0f;
    CHECK_FALSE(s.validate().has_value());
    // Below 1 is not "supersampling less", it is the downscale `--canvas-scale` already owns.
    s.supersample = 0.5f;
    CHECK_FALSE(s.validate().has_value());
    s.supersample = 0.0f;
    CHECK_FALSE(s.validate().has_value());
}

TEST_CASE("supersample round-trips through a project", "[render][settings]") {
    RenderSettings s;
    s.supersample = 2.0f;
    s.width = 1280;
    s.height = 720;
    const nlohmann::json j = s.toJson();
    REQUIRE(j.contains("supersample"));
    // The literal, read out of the document before any loader sees it.
    CHECK(j.at("supersample").get<float>() == 2.0f);

    auto back = RenderSettings::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->supersample == 2.0f);

    // A document written before this existed loads onto the default rather than failing, because
    // every project in the wild is one of those.
    nlohmann::json older = j;
    older.erase("supersample");
    auto old = RenderSettings::fromJson(older);
    REQUIRE(old.has_value());
    CHECK(old->supersample == 1.0f);

    // And a value of the wrong type is refused rather than coerced to zero.
    nlohmann::json bad = j;
    bad["supersample"] = "two";
    CHECK_FALSE(RenderSettings::fromJson(bad).has_value());
}

// ---- the shadow AOV's preconditions (ADR-255, ADR-182) ------------------------------------------

TEST_CASE("--aov shadow refuses the configurations where it would be a constant",
          "[render][settings][aov][adr255]") {
    // ADR-255 asked for one clause; running the arm produced a second. Both are here because a
    // refusal nobody can exercise is a refusal nobody knows still works -- and because the shape
    // being refused is the one this repository keeps writing ADRs about: a valid file, of the
    // right size, in the right format, containing something that is not what its name says.
    avgen::scene::Scene scene;

    SECTION("a scene with no lights at all") {
        // The scene as flattened, not as authored: Composition adds a default key light to a scene
        // that declares none, so this is the state only a scene with authored non-directional
        // lighting reaches.
        CHECK_FALSE(shadowAovPreconditions(scene, "").has_value());
    }

    SECTION("a scene lit only by point lights") {
        avgen::scene::PunctualLight lamp;
        lamp.type = avgen::scene::PunctualLight::Type::Point;
        lamp.castsShadow = true;
        scene.lights.push_back(lamp);
        auto r = shadowAovPreconditions(scene, "");
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("directional") != std::string::npos);
    }

    SECTION("a directional light that does not cast") {
        avgen::scene::PunctualLight key;
        key.type = avgen::scene::PunctualLight::Type::Directional;
        key.castsShadow = false;
        scene.lights.push_back(key);
        CHECK_FALSE(shadowAovPreconditions(scene, "").has_value());
    }

    SECTION("a directional light that is switched off") {
        avgen::scene::PunctualLight key;
        key.type = avgen::scene::PunctualLight::Type::Directional;
        key.castsShadow = true;
        key.enabled = false;
        scene.lights.push_back(key);
        CHECK_FALSE(shadowAovPreconditions(scene, "").has_value());
    }

    // The arm that makes every refusal above mean something: the configuration that must PASS.
    avgen::scene::PunctualLight key;
    key.type = avgen::scene::PunctualLight::Type::Directional;
    key.castsShadow = true;
    scene.lights.push_back(key);

    SECTION("a casting directional light is what it needs") {
        CHECK(shadowAovPreconditions(scene, "").has_value());
        CHECK(shadowAovPreconditions(scene, "particles,water").has_value());
    }

    SECTION("...and the shadow passes must still be running") {
        // Measured, not reasoned: with `--disable shadows` the exported plane marked 28.9% of the
        // frame shadowed against 4.6% in the same render with shadows on, because the mask pass
        // samples an atlas the disabled passes never drew and an undrawn depth atlas reads as an
        // occluder in front of everything.
        auto r = shadowAovPreconditions(scene, "shadows");
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("--disable shadows") != std::string::npos);
        CHECK_FALSE(shadowAovPreconditions(scene, "particles,shadows").has_value());
    }
}
