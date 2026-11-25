#include "api/describe.hpp"
#include "api/engine_host.hpp"
#include "api/jsonrpc_server.hpp"
#include "api/version.hpp"
#include "app/application.hpp"
#include "app/offline_render.hpp"
#include "app/reactivity_cli.hpp"
#include "app/route_audit_cli.hpp"
#include "app/sonic_trace_cli.hpp"
#include "app/space_validate_cli.hpp"
#include "app/input_meter_cli.hpp"
#include "app/jump_trace_cli.hpp"
#include "audio/audio_input.hpp"
#include "control/midi.hpp"
#include "core/env_overrides.hpp"
#include "core/log.hpp"
#include "labs/lab.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <filesystem>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

// The executable this process is running, absolute and with symlinks resolved; argv[0] when the OS cannot say.
std::filesystem::path runningExecutable(const char* argv0) {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code ec;
        const std::filesystem::path path = std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()), ec);
        return ec ? std::filesystem::path(buffer.c_str()) : path;
    }
#else
    std::error_code ec;
    if (auto path = std::filesystem::read_symlink("/proc/self/exe", ec); !ec) {
        return path;
    }
#endif
    return std::filesystem::path(argv0);
}

} // namespace

int main(int argc, char** argv) {
    // ADR-2150: the version and the published contracts need no window, GPU, engine or settings file.
    if (argc > 1 && std::string(argv[1]) == "--version") {
        return avgen::api::runVersionCommand();
    }
    if (argc > 1 && std::string(argv[1]) == "--describe") {
        return avgen::api::runDescribeCommand(argc, argv);
    }
    // ADR-2155: the external interface. Before parseArgs and Application, so neither runs: no window, no
    // settings file, no OSC or MIDI listener. Each builds an api::EngineHost and one api::Session. ADR-2159: the host
    // gets its GPU half here, which creates a device only when the first render job starts.
    // ADR-2162: `avgen mcp` is the same server speaking MCP.
    if (argc > 1 && (std::string(argv[1]) == "serve" || std::string(argv[1]) == "call" || std::string(argv[1]) == "mcp")) {
        avgen::log::init(avgen::log::Level::Info); // stderr; stdout is the protocol
        // ADR-2160: the shaders are found beside the executable (<exe>/shaders), so name the file that is running,
        // not argv[0]: started through PATH, argv[0] is a bare "avgen" with no directory at all.
        auto gpu = avgen::app::gpuRenderBackendFactory(avgen::app::GpuBackendOptions{runningExecutable(argv[0]), {}, true});
        const std::string sub = argv[1];
        if (sub == "mcp") {
            return avgen::api::runServeCommand(argc, argv, std::move(gpu), avgen::api::ServeProtocol::Mcp);
        }
        return sub == "serve" ? avgen::api::runServeCommand(argc, argv, std::move(gpu))
                              : avgen::api::runCallCommand(argc, argv, std::move(gpu));
    }
    // ADR-1051: the spatial validator has its own arguments and needs no window, GPU or engine.
    if (argc > 1 && std::string(argv[1]) == "--validate-space") {
        return avgen::app::runSpaceValidateCommand(argc, argv);
    }
    // ADR-1053: the jump trace plays a project offline (no window, no GPU) and reports transforms that step.
    if (argc > 1 && std::string(argv[1]) == "--trace-jumps") {
        return avgen::app::runJumpTraceCommand(argc, argv);
    }
    auto options = avgen::app::parseArgs(argc, argv);
    if (!options) {
        std::fprintf(stderr, "%s\n", options.error().message.c_str());
        return 1;
    }
    if (options->showHelp) {
        std::printf("%s", avgen::app::usageText().c_str());
        // ADR-2155: the two sub-commands that run without the application.
        std::printf("\nsub-commands (no window, no GPU, no settings file, no listeners):\n%s\n%s\n%s",
                    avgen::api::serveUsage(), avgen::api::mcpUsage(), avgen::api::callUsage());
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
                std::string streams;
                for (const std::uint32_t n : d.streamChannels) {
                    streams += (streams.empty() ? "" : "+") + std::to_string(n);
                }
                std::printf("  %s%s\n", d.name.c_str(), d.isDefault ? " (default)" : "");
                std::printf("      id %s", d.id.c_str());
                if (!d.streamChannels.empty()) {
                    std::printf(", %u input channel(s) in %zu stream(s) (%s), %.0f Hz", d.channels(),
                                d.streamChannels.size(), streams.c_str(), d.sampleRate);
                }
                std::printf("\n");
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
    if (options->inputMeter) {
        return avgen::app::runInputMeter(*options->inputMeter, options->inputMeterSeconds);
    }
    // ADR-902: the route audit runs before the application exists, so it needs no window and no GPU.
    if (options->auditRoutes) {
        return avgen::app::runRouteAuditCommand(options->project.value_or(std::filesystem::path{}),
                                                *options->auditRoutes,
                                                options->fpsGiven ? options->offlineFps : 0.0);
    }
    // ADR-927: the default reactivity proposal, the same way: no window, no GPU, the file untouched.
    if (options->proposeReactivity) {
        return avgen::app::runProposeReactivityCommand(options->project.value_or(std::filesystem::path{}),
                                                       *options->proposeReactivity,
                                                       options->fpsGiven ? options->offlineFps : 0.0);
    }
    // ADR-1020: the Sonic Garden's signals, the same way.
    if (options->sonicTrace) {
        return avgen::app::runSonicTraceCommand(options->project.value_or(std::filesystem::path{}), *options->sonicTrace,
                                                options->fpsGiven ? options->offlineFps : 0.0, options->traceFinals);
    }
    avgen::log::info("{} starting ({} mode)", avgen::api::versionLine(), options->headless ? "headless" : "live");
    // ADR-1803: an exported A/B arm changes the program; say so where every run's log starts.
    if (const std::string env = avgen::core::avgenEnvironmentLine(); !env.empty()) {
        avgen::log::warn("environment overrides: {}", env);
    }

    // ADR-1392: the application is on the heap so the time its teardown takes can be measured and logged. "Quitting
    // is slow after opening several projects" was a report nothing could put a number on.
    auto app = std::make_unique<avgen::app::Application>();
    if (auto r = app->init(*options, std::filesystem::path(argv[0])); !r) {
        avgen::log::error("initialisation failed: {}", r.error().message);
        return 1;
    }
    const int code = app->run();
    const auto shutdownStart = std::chrono::steady_clock::now();
    app.reset();
    avgen::log::info("shutdown: {:.0f} ms to tear the application down",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shutdownStart).count());
    return code;
}
