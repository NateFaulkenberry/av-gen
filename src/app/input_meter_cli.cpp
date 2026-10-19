#include "app/input_meter_cli.hpp"

#include "audio/audio_input.hpp"
#include "audio/capture_coreaudio.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace avgen::app {

int runInputMeter(const std::string& device, double seconds) {
    const auto devices = audio::listCaptureDevices();
    const audio::AudioDeviceInfo* chosen = nullptr;
    for (const auto& d : devices) {
        if (d.id == device || d.name == device) {
            chosen = &d;
        }
    }
    if (chosen == nullptr) {
        for (const auto& d : devices) {
            if (chosen == nullptr && d.name.find(device) != std::string::npos) {
                chosen = &d;
            }
        }
    }
    if (chosen == nullptr) {
        std::fprintf(stderr, "input meter: no capture device matches '%s' (see --list-audio-devices)\n", device.c_str());
        return 2;
    }
    audio::AudioInput input;
#if defined(__APPLE__)
    // One source per channel, so every channel has its own stream and peak, exactly as live sources would.
    std::vector<audio::InputSource> sources;
    for (std::uint32_t c = 0; c < std::max<std::uint32_t>(chosen->channels(), 1); ++c) {
        sources.push_back({.key = "in" + std::to_string(c + 1), .label = "", .channel = c});
    }
    input.setSources(sources);
    auto opened = input.openBackend(std::make_unique<audio::CoreAudioCapture>(chosen->id));
#else
    auto opened = input.open(chosen->name);
#endif
    if (!opened) {
        std::fprintf(stderr, "input meter: %s\n", opened.error().message.c_str());
        return 1;
    }
    std::printf("input meter: '%s' (%s), %u channel(s), %u Hz -- peak dBFS per channel, once a second\n",
                input.deviceName().c_str(), chosen->id.c_str(), input.channels(), input.sampleRate());
    std::vector<float> held(input.channels(), 0.0f);
    std::vector<float> drain(1 << 15);
    const auto start = std::chrono::steady_clock::now();
    auto nextPrint = start + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() - start < std::chrono::duration<double>(seconds)) {
        // The streams are drained so they never fill (this is the analysis side's job in the engine).
        for (std::size_t i = 0; i < input.sources().size(); ++i) {
            if (auto* s = input.sourceStream(i)) {
                while (s->read(drain).count > 0) {
                }
            }
        }
        while (input.analysisStream().read(drain).count > 0) {
        }
        for (std::uint32_t c = 0; c < input.channels(); ++c) {
            held[c] = std::max(held[c], input.channelPeak(c));
        }
        if (std::chrono::steady_clock::now() >= nextPrint) {
            const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::printf("%5.1f s ", t);
            for (std::uint32_t c = 0; c < input.channels(); ++c) {
                const float p = held[c];
                if (p > 0.0f) {
                    std::printf(" %2u:%6.1f", c + 1, 20.0 * std::log10(static_cast<double>(p)));
                } else {
                    std::printf(" %2u:  -inf", c + 1);
                }
                held[c] = 0.0f;
            }
            const auto st = input.stats();
            std::printf("  | blocks %llu drops %llu gaps %llu overloads %llu cb %.1f/%.1f us%s\n",
                        static_cast<unsigned long long>(st.blocks), static_cast<unsigned long long>(st.droppedBlocks),
                        static_cast<unsigned long long>(st.gaps), static_cast<unsigned long long>(st.overloads),
                        st.callbackMeanMicros, st.callbackMaxMicros, input.connected() ? "" : "  DISCONNECTED");
            std::fflush(stdout);
            nextPrint += std::chrono::seconds(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}

} // namespace avgen::app
