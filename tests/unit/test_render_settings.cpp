// Milestone 1.0: render settings (ADR-020) — ranges, frame counts, patterns, JSON.

#include "app/render_settings.hpp"

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
