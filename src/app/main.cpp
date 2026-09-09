#include "app/application.hpp"
#include "audio/audio_input.hpp"
#include "control/midi.hpp"
#include "core/log.hpp"

#include <cstdio>
#include <filesystem>

int main(int argc, char** argv) {
    auto options = avgen::app::parseArgs(argc, argv);
    if (!options) {
        std::fprintf(stderr, "%s\n", options.error().message.c_str());
        return 1;
    }
    if (options->showHelp) {
        std::printf("%s", avgen::app::usageText().c_str());
        return 0;
    }
    if (options->listAudioDevices || options->listMidi) {
        if (options->listAudioDevices) {
            std::printf("audio capture devices:\n");
            for (const auto& d : avgen::audio::listCaptureDevices()) {
                std::printf("  %s%s\n", d.name.c_str(), d.isDefault ? " (default)" : "");
            }
        }
        if (options->listMidi) {
            std::printf("MIDI inputs%s:\n", avgen::control::hasMidiBackend() ? "" : " (no backend on this platform)");
            for (const auto& d : avgen::control::listMidiInputs()) {
                std::printf("  %s%s\n", d.name.c_str(), d.virtualSource ? " (virtual)" : "");
            }
        }
        return 0;
    }
    avgen::log::init(options->logLevel);
    avgen::log::info("avgen 0.1.0 starting ({} mode)", options->headless ? "headless" : "live");

    avgen::app::Application app;
    if (auto r = app.init(*options, std::filesystem::path(argv[0])); !r) {
        avgen::log::error("initialisation failed: {}", r.error().message);
        return 1;
    }
    return app.run();
}
