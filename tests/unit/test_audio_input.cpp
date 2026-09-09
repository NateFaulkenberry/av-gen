#include "audio/audio_input.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace avgen;
using namespace avgen::audio;

namespace {

void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Opens the default capture device or skips the test (no input, denied microphone permission,
// backend failure in CI). macOS may prompt for microphone access on the first run.
void requireOpen(AudioInput& input) {
    if (listCaptureDevices().empty()) {
        SKIP("no audio capture device");
    }
    auto r = input.open();
    if (!r) {
        SKIP("cannot open the default capture device (permission/backend): " << r.error().message);
    }
}

} // namespace

TEST_CASE("listCaptureDevices does not crash", "[audio][input]") {
    const auto devices = listCaptureDevices();
    std::size_t defaults = 0;
    for (const auto& d : devices) {
        CHECK_FALSE(d.name.empty());
        if (d.isDefault) {
            ++defaults;
        }
    }
    CHECK(defaults <= 1);
}

TEST_CASE("AudioInput gain clamps and the closed state is inert", "[audio][input]") {
    AudioInput input;
    CHECK(input.gain() == 1.0f);
    input.setGain(-2.0f);
    CHECK(input.gain() == 0.0f);
    input.setGain(1000.0f);
    CHECK(input.gain() <= 16.0f);
    CHECK(input.gain() > 1.0f);
    input.setGain(0.5f);
    CHECK(input.gain() == 0.5f);

    CHECK_FALSE(input.isOpen());
    CHECK(input.deviceName().empty());
    CHECK(input.sampleRate() == 0);
    CHECK(input.channels() == 0);
    CHECK(input.framesCaptured() == 0);
    CHECK(input.lastPeak() == 0.0f);
    input.close(); // no-op
    CHECK_FALSE(input.open("no-such-capture-device-avgen-xyz").has_value());
    CHECK_FALSE(input.isOpen());
}

TEST_CASE("AudioInput captures from the default device into the analysis stream",
          "[audio][input][device]") {
    AudioInput input;
    requireOpen(input);
    CHECK(input.isOpen());
    CHECK_FALSE(input.deviceName().empty());
    CHECK(input.sampleRate() > 0);
    CHECK(input.channels() > 0);

    sleepMs(300);
    const auto frames = input.framesCaptured();
    CHECK(frames > 0);
    // Roughly 300 ms of audio, allowing for start-up latency and buffer granularity.
    CHECK(frames < input.sampleRate()); // less than a second
    CHECK(input.lastPeak() >= 0.0f);
    CHECK(input.lastPeak() <= 1.0f);

    auto& stream = input.analysisStream();
    std::vector<float> buf(4096);
    auto first = stream.read(buf);
    CHECK(first.count > 0);
    CHECK(first.discontinuity);
    CHECK(first.startFrame == 0);
    std::size_t total = first.count;
    for (int i = 0; i < 100; ++i) {
        auto r = stream.read(buf);
        if (r.count == 0) {
            break;
        }
        CHECK_FALSE(r.discontinuity);
        total += r.count;
    }
    CHECK(total > 0);
    CHECK(total <= frames + 8192);

    SECTION("close stops the device and reopen restarts from frame 0") {
        input.close();
        CHECK_FALSE(input.isOpen());
        CHECK(input.sampleRate() == 0);
        const auto frozen = input.framesCaptured();
        sleepMs(50);
        CHECK(input.framesCaptured() == frozen);

        auto again = input.open();
        if (!again) {
            SKIP("cannot reopen the default capture device: " << again.error().message);
        }
        CHECK(input.isOpen());
        while (stream.read(buf).count > 0) {
        }
        sleepMs(100);
        CHECK(input.framesCaptured() > 0);
        CHECK(input.framesCaptured() < frozen + input.sampleRate());
        input.close();
        CHECK_FALSE(input.isOpen());
    }

    SECTION("a named device is selected by case-insensitive substring") {
        const auto devices = listCaptureDevices();
        REQUIRE_FALSE(devices.empty());
        std::string name = devices.front().name;
        for (auto& c : name) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        auto r = input.open(name.substr(0, std::min<std::size_t>(name.size(), 4)));
        if (!r) {
            SKIP("cannot open '" << name << "': " << r.error().message);
        }
        CHECK(input.isOpen());
        CHECK_FALSE(input.deviceName().empty());
    }
}
