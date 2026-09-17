#include "app/application.hpp"
#include "audio/audio_input.hpp"
#include "control/midi.hpp"
#include "core/log.hpp"
#include "labs/lab.hpp"

#include <cstdio>
#include <string>
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
    // `--labs` prints the suite and exits, beside the other two enumerations, because it answers
    // the same kind of question: what is there. Each lab's boundary is printed with it -- a list of
    // fourteen names would tell somebody which labs exist and not which one their bug belongs to,
    // and choosing the wrong lab is the expensive mistake, not failing to find the list.
    if (options->listLabs) {
        std::printf("AV Gen Engineering Labs (docs/engineering-labs.md)\n\n");
        for (const avgen::labs::LabDescriptor& lab : avgen::labs::labs()) {
            std::printf("  %-12s %s  [%s]\n", std::string(lab.key).c_str(),
                        std::string(lab.title).c_str(),
                        std::string(avgen::labs::statusName(lab.status)).c_str());
            std::printf("    asks      %s\n", std::string(lab.question).c_str());
            std::printf("    owns      %s\n", std::string(lab.owns).c_str());
            std::printf("    not its   %s\n", std::string(lab.doesNotOwn).c_str());
            std::printf("    decided   %s\n", std::string(lab.decides).c_str());
            if (!lab.fixture.empty()) {
                std::printf("    fixture   %s\n", std::string(lab.fixture).c_str());
            }
            if (!lab.cases.empty()) {
                std::printf("    cases     %s\n", std::string(lab.cases).c_str());
            }
            std::printf("\n");
        }
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
