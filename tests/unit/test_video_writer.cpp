#include "assets/video_writer.hpp"
#include "audio/audio_file.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::uint32_t kWidth = 64;
constexpr std::uint32_t kHeight = 64;
constexpr double kFps = 30.0;
constexpr std::size_t kFrames = 30;

std::filesystem::path tempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("avgen_video_test_" + name);
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& name)
        : path(tempPath(name)) {
        std::filesystem::remove(path);
    }
    ~TempFile() { std::filesystem::remove(path); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

// A scratch directory removed with its contents, even when a section leaves early via SKIP().
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& name)
        : path(tempPath(name)) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// A diagonal gradient that scrolls every frame, so encoders get real motion to work on.
std::vector<std::uint8_t> gradientFrame(std::size_t index, std::uint32_t width = kWidth,
                                        std::uint32_t height = kHeight) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(width) * height * 4);
    const auto shift = static_cast<std::uint32_t>(index * 8);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* p = px.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            p[0] = static_cast<std::uint8_t>((x * 4 + shift) & 0xff);
            p[1] = static_cast<std::uint8_t>((y * 4 + shift * 2) & 0xff);
            p[2] = static_cast<std::uint8_t>(((x + y) * 2 + shift) & 0xff);
            p[3] = 255;
        }
    }
    return px;
}

assets::VideoSettings smallClip(const std::string& codec) {
    assets::VideoSettings s;
    s.width = kWidth;
    s.height = kHeight;
    s.fps = kFps;
    s.codec = codec;
    return s;
}

// Opens, writes kFrames gradient frames and finishes; returns the writer for inspection.
std::unique_ptr<assets::VideoWriter> writeClip(const std::filesystem::path& path,
                                               const assets::VideoSettings& settings) {
    auto writer = assets::openVideoWriter(path, settings);
    REQUIRE(writer.has_value());
    for (std::size_t i = 0; i < kFrames; ++i) {
        REQUIRE((*writer)->writeFrame(gradientFrame(i)).has_value());
    }
    const auto finished = (*writer)->finish();
    INFO((finished ? std::string() : finished.error().message));
    REQUIRE(finished.has_value());
    return std::move(*writer);
}

void checkClip(const std::filesystem::path& path, const assets::VideoWriter& writer, bool expectAudio) {
    CHECK(writer.framesWritten() == kFrames);
    REQUIRE(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) > 1024);
    if (!assets::hasNativeVideo()) {
        return;
    }
    const auto info = assets::probeVideo(path);
    INFO((info ? std::string() : info.error().message));
    REQUIRE(info.has_value());
    CHECK(info->width == kWidth);
    CHECK(info->height == kHeight);
    CHECK(info->frames == kFrames);
    CHECK_THAT(info->durationSeconds, WithinAbs(1.0, 0.02));
    CHECK(info->hasAudio == expectAudio);
}

// Writes an executable shell script (a stand-in for ffmpeg on machines without one).
void writeScript(const std::filesystem::path& path, const std::string& body) {
    {
        std::ofstream script(path);
        script << "#!/bin/sh\n" << body;
    }
    std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
}

// `seconds` of a 440 Hz stereo tone at 48 kHz as a float WAV.
void writeToneWav(const std::filesystem::path& path, double seconds = 1.0) {
    const auto tone = testsupport::sine(440.0f, 48000, static_cast<std::size_t>(48000.0 * seconds), 0.5f);
    const auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(tone, 2), 2, 48000);
    REQUIRE(file.writeWav(path).has_value());
}

} // namespace

TEST_CASE("video writer: describeVideoBackends reports what is available", "[assets][video]") {
    const std::string description = assets::describeVideoBackends();
    CHECK(!description.empty());
    CHECK(description.find("native:") != std::string::npos);
    CHECK(description.find("ffmpeg:") != std::string::npos);
    CHECK(assets::nativeCodecs().empty() == !assets::hasNativeVideo());
}

TEST_CASE("video writer: native backend writes every codec", "[assets][video]") {
    if (!assets::hasNativeVideo()) {
        SKIP("no native video backend on this platform");
    }
    for (const auto& codec : assets::nativeCodecs()) {
        DYNAMIC_SECTION(codec << " into .mov") {
            TempFile out(codec + ".mov");
            const auto writer = writeClip(out.path, smallClip(codec));
            CHECK(writer->backendName() == "native");
            CHECK(writer->path() == out.path);
            checkClip(out.path, *writer, false);
        }
    }
    for (const std::string codec : {"h264", "hevc"}) {
        DYNAMIC_SECTION(codec << " into .mp4") {
            TempFile out(codec + ".mp4");
            const auto writer = writeClip(out.path, smallClip(codec));
            checkClip(out.path, *writer, false);
        }
    }
}

TEST_CASE("video writer: native backend accepts a non-integer frame rate", "[assets][video]") {
    if (!assets::hasNativeVideo()) {
        SKIP("no native video backend on this platform");
    }
    TempFile out("ntsc.mp4");
    auto settings = smallClip("h264");
    settings.fps = 29.97;
    auto writer = assets::openVideoWriter(out.path, settings);
    REQUIRE(writer.has_value());
    for (std::size_t i = 0; i < kFrames; ++i) {
        REQUIRE((*writer)->writeFrame(gradientFrame(i)).has_value());
    }
    REQUIRE((*writer)->finish().has_value());
    const auto info = assets::probeVideo(out.path);
    REQUIRE(info.has_value());
    CHECK(info->frames == kFrames);
    CHECK_THAT(info->durationSeconds, WithinAbs(30.0 / 29.97, 0.02));
}

TEST_CASE("video writer: native backend muxes audio", "[assets][video]") {
    if (!assets::hasNativeVideo()) {
        SKIP("no native video backend on this platform");
    }
    TempFile wav("tone.wav");
    writeToneWav(wav.path);

    SECTION("AAC into h264 .mp4") {
        TempFile out("audio.mp4");
        auto settings = smallClip("h264");
        settings.audio = wav.path;
        const auto writer = writeClip(out.path, settings);
        checkClip(out.path, *writer, true);
    }
    SECTION("PCM into ProRes .mov with an offset") {
        TempFile out("audio.mov");
        auto settings = smallClip("prores422");
        settings.audio = wav.path;
        settings.audioOffsetSeconds = 0.25;
        const auto writer = writeClip(out.path, settings);
        checkClip(out.path, *writer, true);
    }
    SECTION("a missing audio file is an error") {
        TempFile out("missing_audio.mp4");
        auto settings = smallClip("h264");
        settings.audio = tempPath("does_not_exist.wav");
        const auto writer = assets::openVideoWriter(out.path, settings);
        REQUIRE(!writer.has_value());
        CHECK(writer.error().message.find("audio") != std::string::npos);
    }
}

// A render whose video outlasts its soundtrack. AVAssetWriter holds the video input closed until
// every track has either supplied data past the video's timestamp or been marked finished, so
// running out of audio used to deadlock the writer: it presented as "the video input accepted no
// data for 30 s", about twenty-six frames from the end of a 2700 frame render, on every codec, and
// it made a full-length render with sound impossible. Nothing caught it because every other audio
// test writes a clip shorter than its tone.
TEST_CASE("video writer: video that outlasts its audio still finishes", "[assets][video]") {
    if (!assets::hasNativeVideo()) {
        SKIP("no native video backend on this platform");
    }
    TempFile wav("short_tone.wav");
    writeToneWav(wav.path, 0.05);           // far shorter than kFrames at kFps
    for (const char* codec : {"h264", "prores422"}) {
        DYNAMIC_SECTION(codec) {
            TempFile out(std::string("outlasts_") + codec + (std::string(codec) == "h264" ? ".mp4" : ".mov"));
            auto settings = smallClip(codec);
            settings.width = 1280; settings.height = 720;
            settings.audio = wav.path;
            auto writer = assets::openVideoWriter(out.path, settings);
            REQUIRE(writer.has_value());
            for (std::size_t i = 0; i < 400; ++i) {
                const auto wrote = (*writer)->writeFrame(std::vector<std::uint8_t>(1280*720*4, static_cast<std::uint8_t>(i)));
                INFO((wrote ? std::string() : wrote.error().message));
                REQUIRE(wrote.has_value());
            }
            const auto finished = (*writer)->finish();
            INFO((finished ? std::string() : finished.error().message));
            REQUIRE(finished.has_value());
            CHECK(std::filesystem::file_size(out.path) > 0);
        }
    }
}

TEST_CASE("video writer: a wrong-size frame is a sticky error", "[assets][video]") {
    if (!assets::hasNativeVideo()) {
        SKIP("no native video backend on this platform");
    }
    TempFile out("bad_frame.mp4");
    auto writer = assets::openVideoWriter(out.path, smallClip("h264"));
    REQUIRE(writer.has_value());
    REQUIRE((*writer)->writeFrame(gradientFrame(0)).has_value());
    const auto bad = (*writer)->writeFrame(gradientFrame(1, kWidth / 2, kHeight));
    REQUIRE(!bad.has_value());
    CHECK(bad.error().message.find("expected") != std::string::npos);
    CHECK(!(*writer)->writeFrame(gradientFrame(2)).has_value());
    CHECK((*writer)->framesWritten() == 1);
    const auto finished = (*writer)->finish();
    REQUIRE(!finished.has_value());
    CHECK(finished.error().message == bad.error().message);
    CHECK(!std::filesystem::exists(out.path));
}

TEST_CASE("video writer: native backend rejects impossible settings", "[assets][video]") {
    if (!assets::hasNativeVideo()) {
        SKIP("no native video backend on this platform");
    }
    SECTION("odd dimensions with h264") {
        TempFile out("odd.mp4");
        auto settings = smallClip("h264");
        settings.width = 63;
        const auto writer = assets::openVideoWriter(out.path, settings);
        REQUIRE(!writer.has_value());
        CHECK(writer.error().message.find("even") != std::string::npos);
        CHECK(!std::filesystem::exists(out.path));
    }
    SECTION("ProRes into .mp4") {
        TempFile out("prores.mp4");
        const auto writer = assets::openVideoWriter(out.path, smallClip("prores4444"));
        REQUIRE(!writer.has_value());
        CHECK(writer.error().message.find(".mov") != std::string::npos);
    }
    SECTION("unknown codec on the native backend") {
        TempFile out("unknown.mov");
        auto settings = smallClip("libvpx-vp9");
        settings.backend = "native";
        const auto writer = assets::openVideoWriter(out.path, settings);
        REQUIRE(!writer.has_value());
        CHECK(writer.error().message.find("prores4444") != std::string::npos);
    }
    SECTION("unknown container on the native backend") {
        TempFile out("clip.webm");
        auto settings = smallClip("h264");
        settings.backend = "native";
        const auto writer = assets::openVideoWriter(out.path, settings);
        REQUIRE(!writer.has_value());
        CHECK(writer.error().message.find(".webm") != std::string::npos);
    }
    SECTION("zero frames") {
        TempFile out("empty.mp4");
        auto writer = assets::openVideoWriter(out.path, smallClip("h264"));
        REQUIRE(writer.has_value());
        CHECK(!(*writer)->finish().has_value());
    }
}

TEST_CASE("video writer: settings are validated before choosing a backend", "[assets][video]") {
    TempFile out("invalid.mp4");
    SECTION("unknown backend") {
        auto settings = smallClip("h264");
        settings.backend = "quicktime";
        const auto writer = assets::openVideoWriter(out.path, settings);
        REQUIRE(!writer.has_value());
        CHECK(writer.error().message.find("quicktime") != std::string::npos);
    }
    SECTION("zero size") {
        auto settings = smallClip("h264");
        settings.height = 0;
        CHECK(!assets::openVideoWriter(out.path, settings).has_value());
    }
    SECTION("non-positive frame rate") {
        auto settings = smallClip("h264");
        settings.fps = 0.0;
        CHECK(!assets::openVideoWriter(out.path, settings).has_value());
    }
    SECTION("ffmpeg backend with a bogus hint") {
        auto settings = smallClip("libx264");
        settings.backend = "ffmpeg";
        settings.ffmpegPath = tempPath("no_such_ffmpeg");
        if (!assets::findFfmpeg().empty()) {
            SKIP("ffmpeg is installed; the search falls back to it");
        }
        const auto writer = assets::openVideoWriter(out.path, settings);
        REQUIRE(!writer.has_value());
        CHECK(writer.error().message.find("ffmpeg not found") != std::string::npos);
    }
}

TEST_CASE("video writer: findFfmpeg honours a hint", "[assets][video]") {
    // A stand-in executable: the search only checks that the file exists and is executable.
    TempDir dir("ffmpeg_hint_dir");
    const auto fake = dir.path / "ffmpeg";
    writeScript(fake, "exit 0\n");
    CHECK(assets::findFfmpeg(fake) == fake);
    CHECK(assets::findFfmpeg(dir.path) == fake);
}

// Regression: with audio muxed, AVAssetWriter interleaves media and stops accepting video until the
// audio input has been fed to roughly the same time. Frames come from another thread (as RenderJob
// does) and the test fails with a message instead of hanging.
TEST_CASE("video writer: long clips with audio do not stall when fed from another thread",
          "[assets][video]") {
    if (!assets::hasNativeVideo()) {
        SKIP("no native video backend on this platform");
    }
    constexpr std::uint32_t width = 640;
    constexpr std::uint32_t height = 360;
    constexpr std::size_t frames = 90; // 3 s at 30 fps
    TempFile wav("long_tone.wav");
    writeToneWav(wav.path, 5.0);

    const auto run = [&](const std::string& codec, const std::string& ext, bool withAudio = true) {
        TempFile out("long_" + codec + ext);
        auto settings = smallClip(codec);
        settings.width = width;
        settings.height = height;
        if (withAudio) {
            settings.audio = wav.path;
        }
        auto opened = assets::openVideoWriter(out.path, settings);
        REQUIRE(opened.has_value());
        std::unique_ptr<assets::VideoWriter> writer = std::move(*opened);

        // The writer times out internally when an input stalls, so the future always completes and
        // its destructor never blocks forever; the 20 s deadline turns a stall into a failure message.
        auto result = std::async(std::launch::async, [&writer] {
            std::string error;
            for (std::size_t i = 0; i < frames; ++i) {
                if (const auto r = writer->writeFrame(gradientFrame(i, width, height)); !r) {
                    error = r.error().message;
                    break;
                }
            }
            return error;
        });
        if (result.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
            FAIL("writeFrame stalled for 20 s (" << codec << "): the writer blocks until audio is fed");
        }
        const std::string feederError = result.get();
        INFO(feederError);
        REQUIRE(feederError.empty());
        REQUIRE(writer->finish().has_value());
        CHECK(writer->framesWritten() == frames);

        const auto info = assets::probeVideo(out.path);
        REQUIRE(info.has_value());
        CHECK(info->width == width);
        CHECK(info->height == height);
        CHECK(info->frames == frames);
        CHECK_THAT(info->durationSeconds, WithinAbs(3.0, 0.02));
        CHECK(info->hasAudio == withAudio);
    };

    SECTION("H.264 into .mp4 without audio (control)") {
        run("h264", ".mp4", false);
    }
    SECTION("ProRes 422 into .mov") {
        run("prores422", ".mov");
    }
    SECTION("H.264 into .mp4") {
        run("h264", ".mp4");
    }
}

#ifndef _WIN32
TEST_CASE("video writer: ffmpeg backend drives an external process", "[assets][video]") {
    // A shell script plays ffmpeg, so the spawn / pipe / exit-status / stderr plumbing is covered
    // even where no ffmpeg is installed.
    TempDir dir("fake_ffmpeg_dir");
    const auto fake = dir.path / "ffmpeg";
    auto settings = smallClip("libx264");
    settings.backend = "ffmpeg";
    settings.ffmpegPath = fake;

    SECTION("frames reach stdin and the output is written") {
        // Consumes stdin, reports the byte count on stderr, writes 4 KB to the last argument.
        writeScript(fake,
                    "n=$(wc -c); for a in \"$@\"; do out=\"$a\"; done\n"
                    "echo \"fake ffmpeg got $n bytes\" >&2\nhead -c 4096 /dev/zero > \"$out\"\nexit 0\n");
        TempFile out("fake.mp4");
        const auto writer = writeClip(out.path, settings);
        CHECK(writer->backendName() == "ffmpeg");
        CHECK(writer->framesWritten() == kFrames);
        REQUIRE(std::filesystem::exists(out.path));
        CHECK(std::filesystem::file_size(out.path) == 4096);
    }
    SECTION("a failing encoder is reported with its stderr, without SIGPIPE") {
        // Exits without reading stdin: writes hit EPIPE once the pipe is full.
        writeScript(fake, "echo \"Unknown encoder 'nope'\" >&2\nexit 1\n");
        TempFile out("fake_fail.mp4");
        auto writer = assets::openVideoWriter(out.path, settings);
        REQUIRE(writer.has_value());
        for (std::size_t i = 0; i < kFrames; ++i) {
            if (!(*writer)->writeFrame(gradientFrame(i)).has_value()) {
                break;
            }
        }
        const auto finished = (*writer)->finish();
        REQUIRE(!finished.has_value());
        CHECK(finished.error().message.find("Unknown encoder") != std::string::npos);
        CHECK(!std::filesystem::exists(out.path));
    }
    SECTION("a missing executable is an open error") {
        settings.ffmpegPath = dir.path / "not_there";
        if (!assets::findFfmpeg().empty()) {
            SKIP("ffmpeg is installed; the search falls back to it");
        }
        CHECK(!assets::openVideoWriter(tempPath("never.mp4"), settings).has_value());
    }
}
#endif

TEST_CASE("video writer: ffmpeg backend encodes libx264 through a pipe", "[assets][video]") {
    const auto ffmpeg = assets::findFfmpeg();
    if (ffmpeg.empty()) {
        SKIP("ffmpeg not found (set AVGEN_FFMPEG or install it on PATH)");
    }
    SECTION("30 frames of libx264") {
        TempFile out("ffmpeg.mp4");
        auto settings = smallClip("libx264");
        settings.backend = "ffmpeg";
        const auto writer = writeClip(out.path, settings);
        CHECK(writer->backendName() == "ffmpeg");
        checkClip(out.path, *writer, false);
    }
    SECTION("odd dimensions with libx264") {
        TempFile out("ffmpeg_odd.mp4");
        auto settings = smallClip("libx264");
        settings.backend = "ffmpeg";
        settings.height = 63;
        const auto writer = assets::openVideoWriter(out.path, settings);
        REQUIRE(!writer.has_value());
        CHECK(writer.error().message.find("even") != std::string::npos);
    }
    SECTION("an unknown encoder fails with ffmpeg's message") {
        TempFile out("ffmpeg_bad.mp4");
        auto settings = smallClip("no_such_encoder");
        settings.backend = "ffmpeg";
        auto writer = assets::openVideoWriter(out.path, settings);
        REQUIRE(writer.has_value());
        // The process fails while parsing options; the failure surfaces on write or on finish.
        for (std::size_t i = 0; i < kFrames; ++i) {
            if (!(*writer)->writeFrame(gradientFrame(i)).has_value()) {
                break;
            }
        }
        const auto finished = (*writer)->finish();
        REQUIRE(!finished.has_value());
        CHECK(finished.error().message.find("ffmpeg") != std::string::npos);
    }
    SECTION("auto falls back to ffmpeg for a non-native codec") {
        TempFile out("ffmpeg_auto.mkv");
        auto settings = smallClip("libx264");
        settings.backend = "auto";
        const auto writer = writeClip(out.path, settings);
        CHECK(writer->backendName() == "ffmpeg");
        CHECK(std::filesystem::file_size(out.path) > 1024);
    }
}
