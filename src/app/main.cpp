#include "app/application.hpp"
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
    avgen::log::init(options->logLevel);
    avgen::log::info("avgen 0.1.0 starting ({} mode)", options->headless ? "headless" : "live");

    avgen::app::Application app;
    if (auto r = app.init(*options, std::filesystem::path(argv[0])); !r) {
        avgen::log::error("initialisation failed: {}", r.error().message);
        return 1;
    }
    return app.run();
}
