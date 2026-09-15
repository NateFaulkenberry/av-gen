#include "app/application.hpp"

#include "ai/scripted_provider.hpp"

#include "app/asset_browser.hpp"
#include "app/camera_director.hpp"
#include "app/world_director.hpp"
#include "app/examples.hpp"
#include "assets/video_writer.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

#include "assets/image.hpp"
#include "rendering/shader_layer.hpp"
#include "core/rng.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "platform/window.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/debug_visualizer.hpp"
#include "app/world_builder.hpp"
#include "ui/control_panel.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include "ui/imgui_layer.hpp"
#include "ui/ui_logic.hpp"

#include <SDL3/SDL.h>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>

// Generated at every build (src/CMakeLists.txt): the engine revision a benchmark record carries.
#include "avgen_build_info.hpp"

#include <unistd.h> // getpid, for the per-process benchmark session id

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <functional>
#include <numbers>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace avgen::app {

std::string usageText() {
    return "usage: avgen [options]\n"
           "  --audio <file>      load an audio file at start-up\n"
           "  --scene <file>      load a glTF/GLB scene (default: built-in orb)\n"
           "  --env <file>        load an equirectangular .hdr environment map\n"
           "  --composition <f>   load a scene composition file (avgen-scene JSON)\n"
           "  --export-bundle <d> copy every referenced asset into <d>/assets and write <d>/project.json\n"
           "  --render <out>      offline render (headless) to a PNG sequence directory or a video file\n"
           "                      (.mov/.mp4/...); size/fps/range/codec from the project's render settings\n"
           "  --format <kind>     render output kind: png (default for a directory), exr (scene-linear half\n"
           "                      EXR sequence, before tone mapping), or video\n"
           "  --range <a>:<b>     render time range in seconds (either side may be empty)\n"
           "  --codec <id>        video codec: prores4444, prores422, h264, hevc, or an ffmpeg encoder name\n"
           "  --quality <0-100>   video quality\n"
           "  --queue <file>      run a render queue (JSON list of projects and render settings), headless\n"
           "  --input [name]      analyse a live capture device (substring of its name; default device)\n"
           "  --osc-port <n>      OSC listen port (overrides the project's control map)\n"
           "  --list-audio-devices, --list-midi   enumerate inputs and exit\n"
           "  --output <d>[:fullscreen|:WxH]      add an output window on display index <d> (repeatable)\n"
           "  --example <name>    open a built-in example by name (see examples/index.json)\n"
           "  --syphon <name>     publish the frame as a Syphon server (macOS)\n"
           "  --ndi <name>        publish the frame as an NDI source (needs the NDI runtime installed)\n"
           "  --shader <file>     add a user shader layer behind the scene (repeatable)\n"
           "  --post <file>       add a user shader layer as a post effect (repeatable)\n"
           "  --project <file>    load a project (parameters, routes, sources, presets, shaders) at start-up\n"
           "  --generate <file>   compose a world from a recipe (see examples/recipes/) at start-up\n"
           "  --direct            cut the camera to the loaded track: folds the audio into\n"
           "                      musical sections and shoots the world's heroes\n"
           "  --director <k=v,..> --direct with the Auto-director panel's settings: mode=continuous|edited,\n"
           "                      minShot, minBuildShot, maxShot (s), wide, hero (mm), maxSpeed (m/s),\n"
           "                      maxSwing (deg/s), dwell (shots), seed,\n"
           "                      holdScenario (a staging scenario to stay with), holdRole, holdRelease (s)\n"
           "  --save-project <f>  write the project on exit\n"
           "  --play              start playback immediately\n"
           "  --frames <n>        exit after n frames\n"
           "  --stress <seed>     apply random slider-like actions every frame (seek, params, routes, volume)\n"
           "  --ai-prompt <text>  run one AI task at start-up against the configured provider\n"
           "  --ai-script <file>  run the AI control plane against a scripted provider (JSON with a\n"
           "                      'turns' array). Deterministic: no key, no network, real tools\n"
           "  --ui-script <arms>  drive the editor with a repeatable interaction: comma-separated from\n"
           "                      hover,sliders,panels,select,scrub,camera,tabs -- or idle, or all\n"
           "  --canvas-scale <f>  render the world at this fraction of the canvas's pixels (0.25-1)\n"
           "  --supersample <f>   offline render only: render the scene at this multiple of the output\n"
           "                      size and resolve down (1 = off, max 2). Buys back the sub-pixel\n"
           "                      detail a small output cannot sample.\n"
           "  --profile-cpu       print the main thread's per-phase frame distribution on exit\n"
           "  --profile-csv <f>   write one row per frame (every phase) to <f> on exit\n"
           "  --capture <file>    write the last frame as a PPM image\n"
           "  --debug-target <t>  display an auxiliary render target: normal|roughness|velocity|\n"
           "                      emission|ids|occlusion|depth|linear depth|depth edges|\n"
           "                      object depth|overdraw|fragment density\n"
           "  --tier <t>          quality tier: preview|realtime|high|offline\n"
           "  --render-limits <m> distance detail in a render: tier|live|unlimited\n"
           "  --disable <list>    switch phases off for cost attribution, or subsystems off for\n"
           "                      forensic isolation:\n"
           "                      shadows,ao,volume,post,shadowmask,\n"
           "                      culling,water,transparency,particles,animation,cameramotion,fxaa\n"
           "  --ab <phase>        headless A/B: run baseline and <phase>-disabled interleaved in\n"
           "                      this one process (A/B/A/B) and report the paired difference.\n"
           "                      --ab none compares the baseline with itself: the noise floor\n"
           "                      a phase may also be a quality arm (ADR-117), which changes a\n"
           "                      QualitySettings field instead of removing a pass:\n"
           "                      shadowrange, contact, pcss, maskfull,\n"
           "                      volumefull, volumepreview, volumequarter, volumesteps\n"
           "  --quality-arm <l>   apply quality arms (the --ab names) to an ordinary run, comma\n"
           "                      separated, so a frame can be captured under the arm the A/B\n"
           "                      timed -- a quality reduction has to be looked at, not only timed\n"
           "  --ab-blocks <n>     A/B pairs to run (default 2)\n"
           "  --bench-json <f>    write the run's machine-readable record (percentiles, counters,\n"
           "                      and the conditions that make it comparable) to <f>\n"
           "  --cluster-stats     report froxel-grid occupancy; CPU work inside the measured\n"
           "                      frames, so it perturbs the wall clock and the record says so\n"
           "  --headless          no window: offline mode, fixed-step clock, precomputed analysis\n"
           "  --fps <n>           offline frame rate (default 60)\n"
           "  --size <w>x<h>      window size in points (default: open maximised)\n"
           "  --log <level>       trace|debug|info|warn|error\n"
           "  --help\n";
}

Result<AppOptions> parseArgs(int argc, char** argv) {
    AppOptions options;
    auto need = [&](int i, const char* flag) -> Result<std::string> {
        if (i + 1 >= argc) {
            return fail("{} requires a value", flag);
        }
        return std::string(argv[i + 1]);
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.showHelp = true;
        } else if (arg == "--play") {
            options.autoplay = true;
        } else if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--audio") {
            auto v = need(i, "--audio");
            if (!v) return std::unexpected(v.error());
            options.audio = *v;
            ++i;
        } else if (arg == "--scene") {
            auto v = need(i, "--scene");
            if (!v) return std::unexpected(v.error());
            options.scene = *v;
            ++i;
        } else if (arg == "--input") {
            options.input = std::string();
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                options.input = std::string(argv[i + 1]);
                ++i;
            }
        } else if (arg == "--osc-port") {
            auto v = need(i, "--osc-port");
            if (!v) return std::unexpected(v.error());
            try {
                options.oscPort = std::stoi(*v);
            } catch (const std::exception&) {
                return fail("--osc-port expects an integer");
            }
            ++i;
        } else if (arg == "--example") {
            auto v = need(i, "--example");
            if (!v) return std::unexpected(v.error());
            options.example = *v;
            ++i;
        } else if (arg == "--syphon" || arg == "--ndi") {
            auto v = need(i, arg.c_str());
            if (!v) return std::unexpected(v.error());
            if (arg == "--syphon") options.syphon = *v; else options.ndi = *v;
            ++i;
        } else if (arg == "--output") {
            auto v = need(i, "--output");
            if (!v) return std::unexpected(v.error());
            options.outputs.push_back(*v);
            ++i;
        } else if (arg == "--list-audio-devices") {
            options.listAudioDevices = true;
        } else if (arg == "--list-midi") {
            options.listMidi = true;
        } else if (arg == "--render") {
            auto v = need(i, "--render");
            if (!v) return std::unexpected(v.error());
            options.render = *v;
            options.headless = true;
            ++i;
        } else if (arg == "--queue") {
            auto v = need(i, "--queue");
            if (!v) return std::unexpected(v.error());
            options.queue = *v;
            options.headless = true;
            ++i;
        } else if (arg == "--format") {
            auto v = need(i, "--format");
            if (!v) return std::unexpected(v.error());
            if (*v == "png" || *v == "sequence") {
                options.renderOutput = RenderOutput::PngSequence;
            } else if (*v == "exr") {
                options.renderOutput = RenderOutput::ExrSequence;
            } else if (*v == "video") {
                options.renderOutput = RenderOutput::Video;
            } else {
                return fail("--output expects png, exr or video");
            }
            ++i;
        } else if (arg == "--range") {
            auto v = need(i, "--range");
            if (!v) return std::unexpected(v.error());
            const auto colon = v->find(':');
            if (colon == std::string::npos) return fail("--range expects <a>:<b>");
            try {
                if (colon > 0) options.rangeStart = std::stod(v->substr(0, colon));
                if (colon + 1 < v->size()) options.rangeEnd = std::stod(v->substr(colon + 1));
            } catch (const std::exception&) {
                return fail("--range expects numbers");
            }
            ++i;
        } else if (arg == "--codec") {
            auto v = need(i, "--codec");
            if (!v) return std::unexpected(v.error());
            options.codec = *v;
            ++i;
        } else if (arg == "--quality") {
            auto v = need(i, "--quality");
            if (!v) return std::unexpected(v.error());
            try {
                options.quality = std::stoi(*v);
            } catch (const std::exception&) {
                return fail("--quality expects an integer");
            }
            ++i;
        } else if (arg == "--export-bundle") {
            auto v = need(i, "--export-bundle");
            if (!v) return std::unexpected(v.error());
            options.bundle = *v;
            ++i;
        } else if (arg == "--composition") {
            auto v = need(i, "--composition");
            if (!v) return std::unexpected(v.error());
            options.composition = *v;
            ++i;
        } else if (arg == "--env") {
            auto v = need(i, "--env");
            if (!v) return std::unexpected(v.error());
            options.environment = *v;
            ++i;
        } else if (arg == "--shader" || arg == "--post") {
            auto v = need(i, arg.c_str());
            if (!v) return std::unexpected(v.error());
            options.shaders.emplace_back(*v, arg == "--post");
            ++i;
        } else if (arg == "--project") {
            auto v = need(i, "--project");
            if (!v) return std::unexpected(v.error());
            options.project = *v;
            ++i;
        } else if (arg == "--direct") {
            options.directCamera = true;
        } else if (arg == "--director") {
            auto v = need(i, "--director");
            if (!v) return std::unexpected(v.error());
            options.directorSettings = *v;
            options.directCamera = true;
            ++i;
        } else if (arg == "--generate") {
            auto v = need(i, "--generate");
            if (!v) return std::unexpected(v.error());
            options.generateRecipe = *v;
            ++i;
        } else if (arg == "--save-project") {
            auto v = need(i, "--save-project");
            if (!v) return std::unexpected(v.error());
            options.saveProject = *v;
            ++i;
        } else if (arg == "--capture") {
            auto v = need(i, "--capture");
            if (!v) return std::unexpected(v.error());
            options.capture = *v;
            ++i;
        } else if (arg == "--debug-target") {
            auto v = need(i, "--debug-target");
            if (!v) return std::unexpected(v.error());
            options.debugTarget = *v;
            ++i;
        } else if (arg == "--tier") {
            auto v = need(i, "--tier");
            if (!v) return std::unexpected(v.error());
            options.qualityTier = *v;
            ++i;
        } else if (arg == "--render-limits") {
            auto v = need(i, "--render-limits");
            if (!v) return std::unexpected(v.error());
            options.renderLimits = *v;
            ++i;
        } else if (arg == "--disable") {
            auto v = need(i, "--disable");
            if (!v) return std::unexpected(v.error());
            options.disablePasses = *v;
            ++i;
        } else if (arg == "--ab") {
            auto v = need(i, "--ab");
            if (!v) return std::unexpected(v.error());
            options.abArm = *v;
            options.headless = true;
            ++i;
        } else if (arg == "--quality-arm") {
            auto v = need(i, "--quality-arm");
            if (!v) {
                return std::unexpected(v.error());
            }
            options.qualityArms = *v;
            ++i;
        } else if (arg == "--ab-blocks") {
            auto v = need(i, "--ab-blocks");
            if (!v) return std::unexpected(v.error());
            options.abBlocks = std::max(1, std::atoi(v->c_str()));
            ++i;
        } else if (arg == "--bench-json") {
            auto v = need(i, "--bench-json");
            if (!v) return std::unexpected(v.error());
            options.benchJson = *v;
            ++i;
        } else if (arg == "--cluster-stats") {
            options.clusterStats = true;
        } else if (arg == "--stress") {
            auto v = need(i, "--stress");
            if (!v) return std::unexpected(v.error());
            options.stressSeed = static_cast<std::uint64_t>(std::atoll(v->c_str()));
            if (options.stressSeed == 0) return fail("--stress seed must be > 0");
            ++i;
        } else if (arg == "--ai-prompt") {
            auto v = need(i, "--ai-prompt");
            if (!v) return std::unexpected(v.error());
            options.aiPrompt = *v;
            ++i;
        } else if (arg == "--ai-script") {
            auto v = need(i, "--ai-script");
            if (!v) return std::unexpected(v.error());
            options.aiScript = std::filesystem::path(*v);
            ++i;
        } else if (arg == "--ui-script") {
            auto v = need(i, "--ui-script");
            if (!v) return std::unexpected(v.error());
            if (!parseUiScript(*v)) {
                return fail("--ui-script '{}' is not an arm; expected some of {}", *v, uiScriptNames());
            }
            options.uiScript = *v;
            ++i;
        } else if (arg == "--supersample") {
            auto v = need(i, "--supersample");
            if (!v) return std::unexpected(v.error());
            options.supersample = std::strtof(v->c_str(), nullptr);
            if (options.supersample < 1.0f || options.supersample > 2.0f) {
                return fail("--supersample must be 1 (off) to 2, got '{}'", *v);
            }
            ++i;
        } else if (arg == "--canvas-scale") {
            auto v = need(i, "--canvas-scale");
            if (!v) return std::unexpected(v.error());
            options.canvasScale = std::strtof(v->c_str(), nullptr);
            if (options.canvasScale < 0.25f || options.canvasScale > 1.0f) {
                return fail("--canvas-scale must be between 0.25 and 1.0");
            }
            ++i;
        } else if (arg == "--profile-cpu") {
            options.profileCpu = true;
        } else if (arg == "--profile-csv") {
            auto v = need(i, "--profile-csv");
            if (!v) return std::unexpected(v.error());
            options.profileCsv = std::filesystem::path(*v);
            options.profileCpu = true;
            ++i;
        } else if (arg == "--frames") {
            auto v = need(i, "--frames");
            if (!v) return std::unexpected(v.error());
            options.frames = std::atoi(v->c_str());
            ++i;
        } else if (arg == "--fps") {
            auto v = need(i, "--fps");
            if (!v) return std::unexpected(v.error());
            options.offlineFps = std::atof(v->c_str());
            if (options.offlineFps <= 0.0) return fail("--fps must be positive");
            options.fpsGiven = true;
            ++i;
        } else if (arg == "--size") {
            auto v = need(i, "--size");
            if (!v) return std::unexpected(v.error());
            unsigned w = 0;
            unsigned h = 0;
            if (std::sscanf(v->c_str(), "%ux%u", &w, &h) != 2 || w == 0 || h == 0) {
                return fail("--size expects <w>x<h>");
            }
            options.width = w;
            options.height = h;
            options.sizeGiven = true;
            options.renderWidth = w;
            options.renderHeight = h;
            ++i;
        } else if (arg == "--log") {
            auto v = need(i, "--log");
            if (!v) return std::unexpected(v.error());
            const std::string level = *v;
            if (level == "trace") options.logLevel = log::Level::Trace;
            else if (level == "debug") options.logLevel = log::Level::Debug;
            else if (level == "info") options.logLevel = log::Level::Info;
            else if (level == "warn") options.logLevel = log::Level::Warn;
            else if (level == "error") options.logLevel = log::Level::Error;
            else return fail("unknown log level '{}'", level);
            ++i;
        } else {
            return fail("unknown argument '{}'\n{}", arg, usageText());
        }
    }
    return options;
}

Application::Application() = default;
Application::~Application() {
    // Destruction order matters: UI before GPU context, renderer before context, window last.
    // The control plane goes first of all: it cancels the running task and waits for it, and both
    // the panel (a raw pointer) and the engine (a reference) outlive it only if it goes now.
    ai_.reset();
    panel_.reset();
    worldBuilder_.reset();
    jobs_.reset();
    imgui_.reset();
    engine_.reset();
    renderer_.reset();
    shaders_.reset();
    context_.reset();
    window_.reset();
}

void Application::initControlPlane() {
    if (auto loaded = AppSettings::load(settingsPath_); !loaded) {
        log::warn("settings: {}", loaded.error().message);
    } else {
        settings_ = std::move(*loaded);
    }
    ai_ = std::make_unique<ai::ControlPlane>(*engine_, jobs_.get());
    // ADR-101: an assistant's edit lands in the same history as a person's, so Cmd+Z takes back
    // "the last thing that happened" rather than "the last thing *I* did". The snapshot sink stays
    // underneath -- it is what makes a task abortable, which is a different promise from undoable.
    aiEditSink_ = std::make_unique<EditHistoryTransactionSink>(*engine_, edits_,
                                                               ai_->transactionSink());
    ai_->setTransactionSink(aiEditSink_.get());
    ai_->settings() = settings_.ai;
    if (auto r = ai_->applySettings(); !r) {
        // Not an error: "no provider configured" is the ordinary state (ADR-065), and saying so at
        // info level is the difference between a feature that is off and a feature that is broken.
        log::info("ai: {}", r.error().message);
    }
}

int Application::runAiTask() {
    if (ai_ == nullptr) {
        log::error("ai: no control plane in this session");
        return 7;
    }
    if (options_.aiScript) {
        // The scripted provider (§52's one permitted mock). Every tool it calls is the real tool
        // against the real engine; only the model's judgement is replaced, which is what makes a
        // run of this repeatable and diffable.
        auto turns = ai::ScriptedProvider::loadScript(*options_.aiScript);
        if (!turns) {
            log::error("ai script: {}", turns.error().message);
            return 7;
        }
        ai_->setProvider(std::make_shared<ai::ScriptedProvider>(std::move(*turns)));
        log::info("ai: scripted provider from {}", options_.aiScript->string());
    }
    const std::string prompt =
        options_.aiPrompt.empty() ? std::string("follow the script") : options_.aiPrompt;
    auto task = ai_->submit(prompt);
    if (!task) {
        log::error("ai: could not start a task");
        return 7;
    }
    log::info("ai: \"{}\"", prompt);
    // This thread is the main thread, so it is the one that must service the queue -- exactly as
    // the frame loop does. Blocking here is correct for a batch flag and is why the flag exists.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration<double>(ai_->settings().limits.maxSeconds + 30.0);
    std::size_t reported = 0;
    while (!task->finished()) {
        ai_->pump();
        const std::vector<ai::Activity> activities = task->activities();
        for (std::size_t i = reported; i < activities.size(); ++i) {
            const ai::Activity& a = activities[i];
            if (a.kind == ai::ActivityKind::StateChanged) {
                continue;
            }
            log::info("ai: [{}] {}{}{}", ai::activityKindName(a.kind), a.title,
                      a.detail.empty() ? "" : " -- ", a.detail);
        }
        reported = activities.size();
        if (std::chrono::steady_clock::now() > deadline) {
            log::error("ai: task did not finish in time");
            task->requestCancel();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    ai_->pump();
    const ai::TaskOutcome outcome = task->outcome();
    for (const std::string& target : outcome.changedTargets) {
        log::info("ai: changed {}", target);
    }
    log::info("ai: {} -- {} tool call(s), {} value(s) changed{}",
              outcome.success ? "completed" : (outcome.cancelled ? "cancelled" : "failed"),
              outcome.toolCalls, outcome.changedTargets.size(),
              outcome.rolledBack ? ", rolled back" : "");
    if (!outcome.summary.empty()) {
        log::info("ai: {}", outcome.summary);
    }
    if (!outcome.success) {
        log::error("ai: {}", outcome.error);
        return 7;
    }
    return 0;
}

void Application::syncDirectorSettings() {
    if (engine_ == nullptr) {
        return;
    }
    AutoDirectorSettings& saved = engine_->autoDirector();
    // The engine's copy moved without the panel touching it, which means a project was loaded.
    // Checked first, because a load that lands on the same frame as a slider drag is the load
    // winning: the file is what the author saved and the drag is on a shot that no longer exists.
    if (saved != lastDirectorSync_) {
        cameraDirection_.settings = saved;
        lastDirectorSync_ = saved;
        return;
    }
    // Otherwise the panel moved it, and the project's copy follows. In memory, not to a file: a
    // project is written when somebody asks for it, so there is nothing here to debounce.
    if (cameraDirection_.settings != lastDirectorSync_) {
        saved = cameraDirection_.settings;
        lastDirectorSync_ = cameraDirection_.settings;
    }
}

void Application::saveSettings() {
    if (settingsPath_.empty()) {
        return;
    }
    if (ai_) {
        settings_.ai = ai_->settings();
    }
    if (panel_) {
        settings_.canvasRenderScale = panel_->canvasRenderScale;
    }
    if (auto r = settings_.save(settingsPath_); !r) {
        log::warn("settings: {}", r.error().message);
    }
}

Result<void> Application::init(const AppOptions& options, const std::filesystem::path& executablePath) {
    if (!options.headless) {
        recent_ = RecentFiles(platform::preferencesDirectory() / "recent.json");
        if (auto r = recent_.load(); !r) {
            log::warn("recent files: {}", r.error().message);
        }
    }
    options_ = options;
    engine_ = std::make_unique<Engine>(options.headless ? EngineMode::Offline : EngineMode::Live);
    if (options.headless) {
        // A headless run gets a control plane too, so `--ai-script` and `--ai-prompt` work without
        // a window. It *reads* the settings file -- an operator who configured a provider in the
        // app expects a batch run to use it -- and never writes one, because a render has no
        // business changing the user's configuration. The credential still comes from the keychain
        // or, on a machine where that cannot be read, from AVGEN_AI_<PROVIDER>_KEY.
        settingsPath_ = AppSettings::pathIn(platform::preferencesDirectory());
        jobs_ = std::make_unique<JobSystem>(2);
        initControlPlane();
        // No theme here. `imgui_` is only constructed alongside the window, further down, so in a
        // headless run this called a method on a null unique_ptr and took down every `--headless`
        // render, the `--render` batch path and `tools/review_frames.py` with it. There is also
        // nothing to theme: a run with no window draws no UI.
        settingsPath_.clear(); // read-only from here: saveSettings() is now a no-op
    }

    if (!options.headless) {
        platform::WindowDesc wdesc;
        wdesc.title = "avgen 0.2";
        wdesc.width = options.width;
        wdesc.height = options.height;
        // The world is the canvas, and a canvas the size of a dialogue box is not one. An explicit
        // --size still wins: that flag exists so a run can be reproduced at a stated size.
        wdesc.maximised = !options.sizeGiven;
        auto window = platform::Window::create(wdesc);
        if (!window) {
            return std::unexpected(window.error());
        }
        window_ = std::move(*window);
    }

    gpu::ContextDesc cdesc;
    cdesc.metalLayer = window_ ? window_->metalLayer() : nullptr;
    auto context = gpu::Context::create(cdesc);
    if (!context) {
        return std::unexpected(context.error());
    }
    context_ = std::move(*context);

    shaders_ = std::make_unique<gpu::ShaderLibrary>(*context_, gpu::ShaderLibrary::defaultSearchDirs(executablePath));
    renderer_ = std::make_unique<rendering::SceneRenderer>(*context_, *shaders_);
    mapper_ = std::make_unique<rendering::OutputMapper>(*context_, *shaders_);
    if (auto r = mapper_->init(); !r) {
        return r;
    }
    if (auto r = renderer_->init(); !r) {
        return std::unexpected(r.error());
    }
    // The 2D composition (ADR-083): installed as the renderer's overlay, so it draws over the
    // tone-mapped frame in every path the renderer already has -- window, offline render,
    // screenshot, projection output -- rather than in one of them.
    compositor_ = std::make_unique<rendering::CompositionRenderer>(*context_, *shaders_);
    if (auto r = compositor_->init(); !r) {
        return std::unexpected(r.error());
    }
    compositor_->setTimeline(&renderer_->timeline());
    compositor_->setInputProvider([this] {
        return rendering::CompositionRenderer::Input{&engine_->layers(),
                                                     engine_->timelineClock().seconds};
    });
    renderer_->setOverlay(compositor_.get());
    if (!options_.qualityTier.empty()) {
        rendering::QualityTier tier = rendering::QualityTier::Realtime;
        if (!rendering::qualityTierFromName(options_.qualityTier, tier)) {
            return fail("unknown quality tier '{}' (preview|realtime|high|offline)", options_.qualityTier);
        }
        renderer_->setQuality(tier);
    }
    // ADR-142. Applied after the tier, so `--tier high --quality-arm volumepreview` reads as
    // "High, except for this one reduction" -- which is the comparison a visual gate wants.
    if (!options_.qualityArms.empty()) {
        rendering::QualitySettings settings = renderer_->qualitySettings();
        std::string token;
        std::istringstream stream(options_.qualityArms);
        std::string applied;
        while (std::getline(stream, token, ',')) {
            if (token.empty()) {
                continue;
            }
            if (!rendering::SceneRenderer::setQualityArm(settings, token)) {
                return fail("unknown quality arm '{}' (one of: {})", token,
                            rendering::SceneRenderer::qualityArmNames());
            }
            applied += applied.empty() ? token : "," + token;
            log::info("quality arm '{}': {}", token, rendering::SceneRenderer::qualityArmDescription(token));
        }
        renderer_->setQualitySettings(settings);
    }
    if (!options_.disablePasses.empty()) {
        rendering::SceneRenderer::PassToggles toggles;
        std::string off;
        std::string token;
        std::istringstream stream(options_.disablePasses);
        while (std::getline(stream, token, ',')) {
            if (token.empty()) {
                continue;
            }
            // One table, shared with the panel and with the automatic bisection: a private
            // if-chain here is how an arm ends up reachable from the renderer and not the CLI.
            if (!rendering::SceneRenderer::setPassArm(toggles, token, false)) {
                return fail("--disable: unknown phase '{}' ({})", token,
                            rendering::SceneRenderer::passArmNames());
            }
            off += off.empty() ? token : ", " + token;
        }
        renderer_->setPassToggles(toggles);
        // Printed so the two arms of an A/B can never be confused for each other after the fact.
        log::info("A/B: phases disabled for this run: {}", off);
    }
    if (!options_.debugTarget.empty()) {
        static constexpr rendering::AuxDebugView kViews[] = {
            rendering::AuxDebugView::None,      rendering::AuxDebugView::Normal,
            rendering::AuxDebugView::Roughness, rendering::AuxDebugView::Velocity,
            rendering::AuxDebugView::Emission,  rendering::AuxDebugView::Ids,
            rendering::AuxDebugView::Occlusion, rendering::AuxDebugView::Depth,
            rendering::AuxDebugView::LinearDepth, rendering::AuxDebugView::DepthEdges,
            rendering::AuxDebugView::ObjectDepth, rendering::AuxDebugView::Overdraw,
            rendering::AuxDebugView::FragmentDensity};
        bool found = false;
        for (const auto view : kViews) {
            if (options_.debugTarget == rendering::auxDebugViewName(view)) {
                renderer_->setAuxDebugView(view);
                found = true;
            }
        }
        if (!found) {
            return fail("unknown debug target '{}'", options_.debugTarget);
        }
    }

    if (window_) {
        context_->configureSurface(window_->pixelWidth(), window_->pixelHeight());
        if (auto r = renderer_->resize(window_->pixelWidth(), window_->pixelHeight()); !r) {
            return std::unexpected(r.error());
        }
        // ImGui keeps the dock tree and the window geometry in its own ini beside the recent-files
        // list; which panels are open is avgen's and lives next to it (ADR-076).
        const std::filesystem::path prefs = platform::preferencesDirectory();
        auto imgui = ui::ImGuiLayer::create(*window_, *context_,
                                            prefs.empty() ? std::filesystem::path{}
                                                          : prefs / "editor-layout.ini");
        if (!imgui) {
            return std::unexpected(imgui.error());
        }
        imgui_ = std::move(*imgui);
        jobs_ = std::make_unique<JobSystem>(2);
        worldBuilder_ = std::make_unique<WorldBuilder>(*jobs_);

        // ---- the AI control plane (ADR-094) ----
        // Built unconditionally, configured only if the user has done so. With nothing configured
        // this costs one object holding a tool table and does nothing at all per frame beyond an
        // empty queue check -- ADR-065's rule that an optional subsystem must not degrade normal
        // operation when idle.
        settingsPath_ = AppSettings::pathIn(prefs);
        initControlPlane();
        // Where an assistant may make a project. The preferences directory rather than anywhere the
        // model names: the project tools take a *name* and resolve it under this root, so one bad
        // argument makes a folder here instead of writing across the machine. A session with no
        // preferences directory installs nothing and those tools refuse, which is the honest
        // failure -- better than defaulting to the working directory and surprising somebody.
        // What the assistant may *read*: the example and recipe folders it ships with, and the open
        // project's own directory. Reading is bounded separately from writing and more widely, but
        // it is still bounded -- without a list, an import tool is an arbitrary-file-read primitive
        // handed to a language model.
        {
            std::vector<std::filesystem::path> roots = exampleSearchDirs(executablePath);
            roots.push_back(std::filesystem::path(AVGEN_SOURCE_DIR) / "assets");
            if (!engine_->projectPath().empty()) {
                roots.push_back(engine_->projectPath().parent_path());
            }
            // Where a person keeps the file they are about to point at. Deliberate, and the
            // narrowest set that makes "import the track on my desktop" work at all: Desktop and
            // Downloads, read only, and nothing else of the home directory. An assistant cannot
            // write to either -- the only directory these tools write into is the projects root
            // above -- and `resolveContent` resolves `..` and symlinks before deciding a path is
            // inside one, so a name cannot climb out of them.
            if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
                const std::filesystem::path homeDir(home);
                for (const char* folder : {"Desktop", "Downloads"}) {
                    std::error_code folderEc;
                    const std::filesystem::path candidate = homeDir / folder;
                    if (std::filesystem::is_directory(candidate, folderEc)) {
                        roots.push_back(candidate);
                    }
                }
            }
            ai_->setContentRoots(roots);
            log::info("ai: {} content root(s) readable", roots.size());
        }
        if (!prefs.empty()) {
            const std::filesystem::path projects = prefs / "projects";
            std::error_code projectsEc;
            std::filesystem::create_directories(projects, projectsEc);
            if (!projectsEc) {
                ai_->setProjectsRoot(projects);
                log::info("ai: projects folder {}", projects.string());
            } else {
                log::warn("ai: cannot use a projects folder at {}: {}", projects.string(),
                          projectsEc.message());
            }
        }
        // Performance is renderer state, and `src/ai/` lives in avgen_core which cannot see the
        // renderer. So the application -- which owns both -- fills in a plain snapshot, and a
        // session without a renderer simply installs nothing and the tool says so honestly.
        ai_->setPerformanceSource([this] {
            ai::PerformanceSnapshot snapshot;
            if (renderer_ == nullptr) {
                return snapshot;
            }
            const rendering::RenderStats& stats = renderer_->stats();
            snapshot.available = true;
            snapshot.cpuFrameMs = lastCpuFrameMs_;
            snapshot.frameIntervalMs = lastFrameIntervalMs_;
            snapshot.fps = lastFps_;
            snapshot.gpuFrameMs = lastGpuFrameMs_;
            snapshot.drawCalls = stats.drawCalls;
            snapshot.triangles = stats.triangles;
            snapshot.width = stats.width;
            snapshot.height = stats.height;
            snapshot.visibleInstances = stats.visibleInstances;
            snapshot.culledInstances = stats.culledInstances;
            snapshot.lights = stats.lights;
            snapshot.entities = stats.entities;
            snapshot.shadowDraws = stats.shadowDraws;
            snapshot.particleSystems = stats.particles.systems;
            snapshot.proceduralObjects = stats.procedural.objects;
            for (const gpu::TimelineInterval& pass : renderer_->timeline().passes()) {
                snapshot.gpuPasses.push_back(ai::PassTime{pass.label, pass.ms});
            }
            std::sort(snapshot.gpuPasses.begin(), snapshot.gpuPasses.end(),
                      [](const ai::PassTime& a, const ai::PassTime& b) {
                          return a.milliseconds > b.milliseconds;
                      });
            return snapshot;
        });

        panel_ = std::make_unique<ui::ControlPanel>();
        // The world editor records into the application's history rather than one of its own, and
        // registers as the context that answers Copy, Delete and the rest for world objects.
        panel_->editor.attachEdits(edits_);
        panel_->edits = &edits_;
        edits_.addContext(panel_->editor);
        panel_->canvasRenderScale =
            options_.canvasScale != 1.0f ? options_.canvasScale : settings_.canvasRenderScale;
        panel_->ai.plane = ai_.get();
        panel_->settings.plane = ai_.get();
        panel_->settings.settings = &settings_;
        panel_->settings.onAppearanceChanged = [this](app::AppearanceTheme theme) {
            if (imgui_) {
                imgui_->applyTheme(theme);
            }
        };
        panel_->settings.canvasRenderScale = &panel_->canvasRenderScale;
        panel_->settings.settingsFile = settingsPath_.generic_string();
        panel_->settings.onChanged = [this] { saveSettings(); };
        panel_->ai.onOpenSettings = [this] {
            // Open the Settings panel on the AI section, which is the navigation §46 asks for when
            // nothing is configured.
            panel_->settings.show(ui::SettingsPanel::Section::Ai);
            if (bool* slot = panel_->layout().slot("Settings"); slot != nullptr) {
                *slot = true;
            }
        };
        panel_->setLayoutStore(prefs.empty() ? std::filesystem::path{} : prefs / "editor-layout.json",
                               imgui_->hadSavedLayout());
        panel_->jobs = jobs_.get();
        panel_->sequence.jobs = jobs_.get();
        panel_->builder = worldBuilder_.get();
        auto dialog = [this](platform::Window::DialogKind kind) {
            return [this, kind] {
                window_->openFileDialog(kind, [this](std::string path) {
                    if (!path.empty()) {
                        loadAny(path);
                    }
                });
            };
        };
        panel_->onDirectCamera = [this] {
            if (auto r = directCameraFromTrack(); !r) {
                log::warn("direct: {}", r.error().message);
                panel_->setStatus(r.error().message);
            } else {
                renderer_->resetTemporalHistory();
                panel_->setStatus("camera cut to the track -- it re-cuts as you star and unstar");
            }
        };
        // The panel edits the Auto-director's settings in place; the host owns them so a re-cut uses
        // what the user last chose (section 9).
        panel_->autoDirector = &cameraDirection_.settings;
        panel_->onClearCameraAutomation = [this] {
            // Removing the camera's automation rather than disabling the whole timeline: a project
            // may automate other things, and handing the camera back is not a reason to stop those.
            // The same call a viewport drag makes, so both mean exactly one thing.
            const std::size_t removed = releaseDirectedCamera(*engine_, cameraDirection_);
            panel_->setStatus("camera handed back to the viewport (" + std::to_string(removed) +
                              " track(s) removed)");
        };
        // One action, three routes (ADR-216): the File menu item, the O shortcut and the Sequencer's
        // Import Audio... button. The menu item and the shortcut are *not* duplicates of each other
        // -- one is discoverable and one is fast -- and the button is where a person looking at a
        // timeline goes to put a song on it. All three are this callback.
        panel_->onOpenAudio = dialog(platform::Window::DialogKind::Audio);
        panel_->sequence.onOpenAudio = panel_->onOpenAudio;
        panel_->onOpenScene = dialog(platform::Window::DialogKind::Scene);
        panel_->onOpenEnvironment = dialog(platform::Window::DialogKind::Environment);
        panel_->onOrbScene = [this] { engine_->loadOrbScene(); };
        panel_->onOpenProject = dialog(platform::Window::DialogKind::Any);
        panel_->onOpenShader = dialog(platform::Window::DialogKind::Shader);
        panel_->onOpenPostShader = [this] {
            window_->openFileDialog(platform::Window::DialogKind::Shader, [this](std::string path) {
                if (path.empty()) {
                    return;
                }
                if (auto id = engine_->addShaderLayer(path, shaders::LayerStage::Post); !id) {
                    log::error("shader: {}", id.error().message);
                    panel_->setStatus(id.error().message);
                }
            });
        };
        panel_->shaderErrorFor = [this](std::uint32_t id) { return renderer_->shaderStack().errorFor(id); };
        panel_->onSaveScene = [this] {
            window_->saveFileDialog(platform::Window::SaveKind::Project, [this](std::string path) {
                if (path.empty()) return;
                if (auto r = engine_->saveComposition(path); !r) {
                    panel_->setStatus(r.error().message);
                } else {
                    panel_->setStatus("scene saved " + std::filesystem::path(path).filename().string());
                }
            });
        };
        auto addAssetNode = [this](scene::NodeKind kind, platform::Window::DialogKind dialogKind) {
            return [this, kind, dialogKind] {
                window_->openFileDialog(dialogKind, [this, kind](std::string path) {
                    if (path.empty()) return;
                    scene::CompositionNode node;
                    node.kind = kind;
                    node.asset = path;
                    node.name = std::filesystem::path(path).stem().string();
                    if (auto r = engine_->addNode(std::move(node)); !r) {
                        panel_->setStatus(r.error().message);
                    }
                });
            };
        };
        panel_->onAddGltfNode = addAssetNode(scene::NodeKind::Gltf, platform::Window::DialogKind::Scene);
        panel_->onAddSceneNode = addAssetNode(scene::NodeKind::Scene, platform::Window::DialogKind::Any);
        auto saveTo = [this](const std::filesystem::path& path) {
            storeOutputsToProject();
            if (auto r = engine_->saveProject(path); !r) {
                log::error("save project: {}", r.error().message);
                panel_->setStatus(r.error().message);
            } else {
                panel_->setStatus("saved " + path.filename().string());
                rememberProject(path);
            }
        };
        panel_->onSaveProject = [this, saveTo] {
            window_->saveFileDialog(platform::Window::SaveKind::Project, [saveTo](std::string path) {
                if (!path.empty()) {
                    saveTo(path);
                }
            });
        };
        panel_->onSaveProjectHere = [this, saveTo] {
            if (!engine_->projectPath().empty()) {
                saveTo(engine_->projectPath());
            }
        };
        panel_->onNewProject = [this] {
            engine_->newProject();
            panel_->setStatus("new project");
        };
        panel_->onOpenRecent = [this](const std::filesystem::path& path) { loadAny(path); };
        panel_->onExportBundle = [this] {
            window_->saveFileDialog(platform::Window::SaveKind::Project, [this](std::string path) {
                if (path.empty()) {
                    return;
                }
                // The dialog picks a file name; the bundle is the folder of that name.
                std::filesystem::path dir(path);
                if (dir.extension() == ".json") {
                    dir.replace_extension();
                }
                if (auto r = engine_->exportBundle(dir); !r) {
                    log::error("bundle: {}", r.error().message);
                    panel_->setStatus(r.error().message);
                } else {
                    panel_->setStatus("bundle exported to " + dir.filename().string());
                    rememberProject(dir / "project.json");
                }
            });
        };
        recent_.pruneMissing();
        panel_->recentProjects = recent_.entries();
        if (auto examples = loadExamples(exampleSearchDirs(executablePath))) {
            panel_->examples = std::move(*examples);
        } else {
            log::warn("examples: {}", examples.error().message);
        }
        panel_->onOpenExample = [this](const ExampleInfo& ex) { loadAny(ex.file); };
        // The loader names the stage it is entering. Only the live editor installs this: offline
        // has no window to tell and `runHeadless` must not acquire a dependency on one.
        //
        // Nothing repaints while it fires, because the load owns the main thread -- see `loadAny`.
        // It earns its place for the two things it can still do: the stage the loader reached is on
        // screen the moment the frame resumes, so a load that *failed* says where, and
        // `Engine::lastLoadTimings` gets the per-stage breakdown that turned "opening a project
        // freezes the application" into a number.
        engine_->setLoadReporter([this](const Engine::LoadStage& at) {
            if (panel_ == nullptr) {
                return;
            }
            panel_->loading.active = true;
            panel_->loading.stage.assign(at.name);
            panel_->loading.index = at.index;
            panel_->loading.count = at.count;
        });
        // ---- asset browser (ADR-031): the example directories plus the current project's folder
        const auto assetDirs = [executablePath]() { return exampleSearchDirs(executablePath); };
        panel_->onRescanAssets = [this, assetDirs] {
            panel_->assets = scanAssets(assetDirs());
            std::vector<std::filesystem::path> roots = assetDirs();
            if (!engine_->projectPath().empty()) roots.push_back(engine_->projectPath().parent_path());
            if (auto catalog = assets::catalogAssets(roots, engine_->projectPath().parent_path())) {
                panel_->catalogAssets = std::move(*catalog);
            }
        };
        panel_->assets = scanAssets(assetDirs());
        {
            std::vector<std::filesystem::path> roots = assetDirs();
            if (!engine_->projectPath().empty()) roots.push_back(engine_->projectPath().parent_path());
            if (auto catalog = assets::catalogAssets(roots, engine_->projectPath().parent_path())) {
                panel_->catalogAssets = std::move(*catalog);
            }
        }
        panel_->onOpenAsset = [this](const AssetEntry& asset) { loadAny(asset.path); };
        // Art direction (ADR-041): the shipped looks live beside the examples.
        {
            std::vector<std::filesystem::path> lookDirs;
            for (const std::filesystem::path& dir : exampleSearchDirs(executablePath)) {
                lookDirs.push_back(dir / "looks");
            }
            auto looks = scanLooks(lookDirs);
            if (!looks.empty()) {
                log::info("looks: {} available", looks.size());
                engine_->setLooks(std::move(looks));
            }
        }
        // ---- offline rendering from the UI ----
        uiRender_ = engine_->renderSettings();
        panel_->renderSettings = &uiRender_;
        panel_->renderer = renderer_.get();
        panel_->videoBackends = assets::describeVideoBackends();
        panel_->renderProgress = [this]() -> RenderProgress { return job_ ? job_->progress() : lastRender_; };
        panel_->onStartRender = [this] { startRenderFromUi(); };
        panel_->onCancelRender = [this] {
            if (job_) job_->cancel();
        };
        panel_->onEnqueueRender = [this] {
            if (engine_->projectPath().empty()) {
                panel_->setStatus("save the project before queueing a render");
                return;
            }
            engine_->renderSettings() = uiRender_;
            if (auto r = engine_->saveProject(engine_->projectPath()); !r) {
                panel_->setStatus(r.error().message);
                return;
            }
            uiQueue_.emplace_back(engine_->projectPath(), uiRender_);
            panel_->queuedRenders = uiQueue_.size();
            panel_->setStatus("queued " + engine_->projectPath().filename().string());
        };
        panel_->onRunQueue = [this] {
            if (job_ || uiQueue_.empty()) return;
            auto [project, settings] = uiQueue_.front();
            uiQueue_.pop_front();
            panel_->queuedRenders = uiQueue_.size();
            auto job = makeRenderJob(project, settings);
            if (!job) {
                panel_->setStatus(job.error().message);
                return;
            }
            job_ = std::move(*job);
        };
        panel_->outputs = &outputs_;
        panel_->shareStatus = share::TextureShare::describe();
        panel_->onShare = [this](const std::string& kind, const std::string& name) { applyShare(kind, name); };
        panel_->onOutputsChanged = [this] {
            if (auto r = outputs_.open(*context_, *shaders_); !r) {
                panel_->setStatus(r.error().message);
            }
            storeOutputsToProject();
        };
        panel_->onUseAudioInput = [this](const std::string& name) {
            if (auto r = engine_->useAudioInput(name); !r) {
                panel_->setStatus(r.error().message);
            } else {
                panel_->setStatus("live input: " + engine_->audioInput()->deviceName());
            }
        };
        panel_->onStopAudioInput = [this] { engine_->stopAudioInput(); };
        panel_->onChooseRenderOutput = [this] {
            // Two different questions, because a video is a file and a sequence is a directory full
            // of them. Asking for a file name when the answer is a folder is how the output path of
            // a PNG render ended up being a file that was never written.
            const RenderOutput kind = uiRender_.output;
            auto chosen = [this, kind](std::string path) {
                if (path.empty()) {
                    return;
                }
                uiRender_.outputPath = path;
                // The extension can *confirm* a kind but never silently contradicts the one that is
                // selected: the dialog was opened for that kind, and a person who has just picked
                // Video did not mean to pick a PNG sequence by typing a name without a dot in it.
                uiRender_.output = RenderSettings::outputForPath(path, kind);
                if (uiRender_.output == RenderOutput::Video) {
                    uiRender_.outputPath = RenderSettings::withVideoExtension(uiRender_.outputPath);
                }
            };
            if (isSequence(kind)) {
                window_->chooseFolderDialog(std::move(chosen));
            } else {
                window_->saveFileDialog(platform::Window::SaveKind::Video, std::move(chosen));
            }
        };
    }

    if (options.example) {
        auto examples = loadExamples(exampleSearchDirs(executablePath));
        bool found = false;
        if (examples) {
            for (const auto& ex : *examples) {
                if (ex.name == *options.example) {
                    if (auto r = openAny(ex.file); !r) {
                        log::error("example '{}': {}", ex.name, r.error().message);
                        if (options.headless) return std::unexpected(r.error());
                    }
                    found = true;
                }
            }
        }
        if (!found) {
            log::error("example '{}' not found", *options.example);
            if (options.headless) return fail("example '{}' not found", *options.example);
        }
    }
    // The project restores its own audio/scene/environment; explicit flags below override it.
    if (options.project) {
        if (auto r = engine_->loadProject(*options.project); !r) {
            log::error("project: {}", r.error().message);
            if (options.headless) {
                return std::unexpected(r.error());
            }
            if (panel_) panel_->setStatus(r.error().message);
        } else if (!options.headless) {
            rememberProject(*options.project);
        }
    }
    if (options.generateRecipe) {
        if (auto r = generateWorldFromRecipe(*options.generateRecipe); !r) {
            log::error("generate: {}", r.error().message);
            if (options.headless) {
                return std::unexpected(r.error());
            }
            if (panel_) panel_->setStatus(r.error().message);
        }
    }
    if (options.oscPort) {
        auto map = engine_->control().map();
        map.oscPort = static_cast<std::uint16_t>(std::clamp(*options.oscPort, 0, 65535));
        map.oscEnabled = true;
        engine_->control().setMap(std::move(map));
    }
    if (options.input && !options.headless) {
        if (auto r = engine_->useAudioInput(*options.input); !r) {
            log::error("audio input: {}", r.error().message);
            if (panel_) panel_->setStatus(r.error().message);
        }
    }
    if (options.scene) {
        if (auto r = engine_->loadScene(*options.scene); !r) {
            log::error("scene: {}", r.error().message);
            if (options.headless) {
                return std::unexpected(r.error());
            }
            if (panel_) panel_->setStatus(r.error().message);
        }
    }
    if (options.composition) {
        if (auto r = engine_->loadComposition(*options.composition); !r) {
            log::error("composition: {}", r.error().message);
            if (options.headless) {
                return std::unexpected(r.error());
            }
            if (panel_) panel_->setStatus(r.error().message);
        }
    }
    if (options.environment) {
        if (auto r = engine_->loadEnvironment(*options.environment); !r) {
            log::error("environment: {}", r.error().message);
            if (options.headless) {
                return std::unexpected(r.error());
            }
            if (panel_) panel_->setStatus(r.error().message);
        }
    }

    if (options.audio) {
        loadAudio(*options.audio);
        if (!engine_->hasAudio() && options.headless) {
            return fail("headless run requires a loadable audio file");
        }
    }
    if (options.directCamera) {
        // After the world *and* the track, because the director needs both: it shoots the world's
        // heroes and it cuts to the music. This used to sit above, between `--generate` and
        // `--scene`, on the reasoning that a generated world has no heroes until it is installed --
        // which is true and insufficient. `--composition` loads thirty lines further down and
        // `--audio` sixty, so `avgen --composition w.scene.json --audio t.wav --direct` reached the
        // director with no scene at all and failed with "--direct needs a scene; load a project or
        // generate a world first" every single time. The flag worked only for `--project` and
        // `--generate`, which is not what its own usage line claims.
        if (auto r = directCameraFromTrack(); !r) {
            log::error("direct: {}", r.error().message);
            if (options.headless) {
                return std::unexpected(r.error());
            }
            if (panel_) panel_->setStatus(r.error().message);
        }
    }
    for (const auto& [file, isPost] : options.shaders) {
        auto id = engine_->addShaderLayer(file, isPost ? shaders::LayerStage::Post : shaders::LayerStage::Background);
        if (!id) {
            log::error("shader: {}", id.error().message);
            if (options.headless) {
                return std::unexpected(id.error());
            }
            if (panel_) panel_->setStatus(id.error().message);
        }
    }
    // Hot reload of the engine's own shaders.
    for (const char* name : {"common.wgsl", "pbr.wgsl", "grid.wgsl", "skybox.wgsl", "tonemap.wgsl"}) {
        if (auto located = shaders_->locate(name)) {
            engineShaderWatcher_.watch(*located);
        }
    }
    if (options.autoplay && engine_->hasAudio()) {
        if (auto r = engine_->play(); !r) {
            log::warn("autoplay: {}", r.error().message);
        }
    }
    if (!options.headless) {
        // Output windows from the project, then from --output flags (ADR-022).
        applyOutputsFromProject();
        for (const auto& spec : options.outputs) {
            OutputDesc desc;
            desc.name = "output" + std::to_string(outputs_.outputs().size() + 1);
            try {
                desc.display = std::stoi(spec.substr(0, spec.find(':')));
                if (const auto colon = spec.find(':'); colon != std::string::npos) {
                    const std::string opt = spec.substr(colon + 1);
                    if (opt == "fullscreen") {
                        desc.fullscreen = true;
                    } else if (const auto x = opt.find('x'); x != std::string::npos) {
                        desc.width = static_cast<std::uint32_t>(std::max(1, std::stoi(opt.substr(0, x))));
                        desc.height = static_cast<std::uint32_t>(std::max(1, std::stoi(opt.substr(x + 1))));
                    }
                }
            } catch (const std::exception&) {
                log::error("--output expects <display>[:fullscreen|:WxH], got '{}'", spec);
                continue;
            }
            if (auto r = outputs_.add(desc); !r) {
                log::error("output: {}", r.error().message);
            }
        }
        if (auto r = outputs_.open(*context_, *shaders_); !r) {
            log::warn("outputs: {}", r.error().message);
        }
        storeOutputsToProject();        if (options.syphon) {
            applyShare("syphon", *options.syphon);
        } else if (options.ndi) {
            applyShare("ndi", *options.ndi);
        }
    }
    return {};
}

void Application::loadAudio(const std::filesystem::path& path) {
    auto result = engine_->loadAudio(path);
    if (!result) {
        log::error("open audio: {}", result.error().message);
        if (panel_) {
            panel_->setStatus(result.error().message);
        }
        return;
    }
    if (panel_) {
        panel_->setStatus({});
    }
    if (window_) {
        window_->setTitle("avgen 0.1 - " + path.filename().string());
    }
}

void Application::applyShare(const std::string& kind, const std::string& name) {
    share_.close();
    if (kind == "off") {
        if (panel_) panel_->shareStatus = share::TextureShare::describe();
        return;
    }
    const auto k = kind == "ndi" ? share::ShareKind::Ndi : share::ShareKind::Syphon;
    if (auto r = share_.open(k, *context_, name.empty() ? "avgen" : name); !r) {
        log::error("share: {}", r.error().message);
        if (panel_) {
            panel_->setStatus(r.error().message);
            panel_->shareStatus = r.error().message;
        }
        return;
    }
    log::info("sharing the frame as {} '{}'", kind, share_.name());
}

// ---- opening something, one frame later (the brief's section 5) -------------------------------
//
// Every interactive open funnels through here: the File menu, Open Recent, the examples list, the
// asset browser and a file dropped on the window. It used to do the work on the spot, which meant
// the main thread disappeared into `Engine::loadProject` for about two seconds with the window
// holding whatever was last painted -- an editor that looks exactly like an editor that ignored
// the click.
//
// The load is *not* moved to a worker, and that is a decision rather than an omission: the
// composition is not thread-safe, the environment map's prefilter is GPU work and all GPU work here
// is the main thread's, and the parameter set is torn down and rebuilt underneath everything that
// reads it. ADR-084 rejected threading a far smaller piece of this for the same reasons, and
// section 19 of the brief rules out moving unsafe work to a background thread to make a number look
// better.
//
// What is safe is to *say so first*. The request is remembered, the canvas paints "Opening <name>"
// over the scene that is still there, that frame is presented, and the load runs at the top of the
// next one. The freeze is the same length; it stops being ambiguous. The part that actually made it
// shorter is `world::scatter` being threaded, which is a third of it.
//
// Before the first frame there is no panel and nothing to paint on, so start-up opens immediately.
void Application::loadAny(const std::filesystem::path& path) {
    if (panel_ == nullptr) {
        performOpen(path);
        return;
    }
    pendingOpen_ = path;
    panel_->loading = ui::ControlPanel::Loading{
        .active = true, .what = path.filename().string(), .stage = {}, .index = 0, .count = 1};
    panel_->setStatus(fmt::format("Opening {}...", path.filename().string()));
}

void Application::servicePendingOpen() {
    if (!pendingOpen_.has_value()) {
        return;
    }
    const std::filesystem::path path = *pendingOpen_;
    pendingOpen_.reset();
    performOpen(path);
    if (panel_ != nullptr) {
        // Cleared on every path out of the load, success or failure. A loading state that survives
        // its own failure is the "stuck forever" the brief's section 20 asks to be tested for, and
        // the one place it could happen is here.
        panel_->loading = ui::ControlPanel::Loading{};
    }
}

void Application::performOpen(const std::filesystem::path& path) {
    auto r = openAny(path);
    if (!r) {
        log::error("open '{}': {}", path.string(), r.error().message);
        if (panel_) {
            panel_->setStatus(r.error().message);
        }
        return;
    }
    if (panel_) {
        panel_->setStatus({});
    }
    if (!engine_->projectPath().empty() && std::filesystem::absolute(engine_->projectPath()) == std::filesystem::absolute(path)) {
        rememberProject(path);
    } else if (window_) {
        window_->setTitle("avgen " + std::string(app::Engine::kAppVersion) + " - " + path.filename().string());
    }
}

Result<void> Application::ensureFinalTexture(std::uint32_t width, std::uint32_t height) {
    if (finalTexture_ && finalWidth_ == width && finalHeight_ == height) {
        return {};
    }
    wgpu::TextureDescriptor desc{};
    desc.label = "final";
    desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {width, height, 1};
    desc.format = wgpu::TextureFormat::BGRA8Unorm;
    finalTexture_ = context_->device().CreateTexture(&desc);
    if (!finalTexture_) {
        return fail("cannot create the {}x{} final texture", width, height);
    }
    finalView_ = finalTexture_.CreateView();
    finalWidth_ = width;
    finalHeight_ = height;
    return {};
}

void Application::applyOutputsFromProject() {
    outputs_.closeAll();
    if (auto r = outputs_.fromJson(engine_->outputsJson()); !r) {
        log::warn("outputs: {}", r.error().message);
    }
}

void Application::storeOutputsToProject() {
    engine_->setOutputsJson(outputs_.toJson());
    // The last thing before a save, so a headless run that never reaches the frame loop's
    // `syncDirectorSettings` -- `--direct --director=... --save-project` -- still records the
    // direction it was given rather than the defaults it never used.
    syncDirectorSettings();
}

void Application::startRenderFromUi() {
    if (job_) {
        return;
    }
    engine_->renderSettings() = uiRender_;
    // Renders load a project file: the current one when saved, else a session snapshot.
    std::filesystem::path projectFile = engine_->projectPath();
    if (projectFile.empty()) {
        renderProjectTemp_ = std::filesystem::temp_directory_path() / "avgen_render_session.json";
        projectFile = renderProjectTemp_;
    }
    if (auto r = engine_->saveProject(projectFile); !r) {
        panel_->setStatus(r.error().message);
        return;
    }
    if (renderProjectTemp_ == projectFile) {
        engine_->clearProjectPath(); // the snapshot is not the user's project
    }
    auto job = makeRenderJob(projectFile, uiRender_);
    if (!job) {
        panel_->setStatus(job.error().message);
        return;
    }
    job_ = std::move(*job);
    panel_->setStatus("rendering...");
}

void Application::rememberProject(const std::filesystem::path& path) {
    if (context_ && shaders_) {
        applyOutputsFromProject();
        if (auto r = outputs_.open(*context_, *shaders_); !r) {
            log::warn("outputs: {}", r.error().message);
        }
    }
    recent_.add(path);
    if (auto r = recent_.save(); !r) {
        log::warn("recent files: {}", r.error().message);
    }
    if (panel_) {
        panel_->recentProjects = recent_.entries();
        if (!engine_->projectWarnings().empty()) {
            panel_->setStatus(std::to_string(engine_->projectWarnings().size()) + " project warning(s): " +
                              engine_->projectWarnings().front());
        }
    }
    if (window_) {
        window_->setTitle("avgen " + std::string(app::Engine::kAppVersion) + " - " + path.filename().string());
    }
}

namespace {
Result<void> writeCapture(const gpu::Image8& image, const std::filesystem::path& path) {
    if (path.extension() == ".png") {
        return assets::writePng(path, image.width, image.height, image.rgba);
    }
    return gpu::writePpm(image, path);
}
} // namespace

Result<void> Application::captureFrame(const FrameTime& time, const std::filesystem::path& path) {
    const std::uint32_t w = window_ ? window_->pixelWidth() : 1280;
    const std::uint32_t h = window_ ? window_->pixelHeight() : 720;
    const rendering::ShaderFrameInputs shaderInputs{&engine_->shaderLayers(),
                                                    engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
    auto image = renderer_->renderToImage(engine_->scene(), time, w, h, &shaderInputs);
    if (!image) {
        return std::unexpected(image.error());
    }
    if (auto r = writeCapture(*image, path); !r) {
        return r;
    }
    log::info("captured frame {} ({}x{}) to {}", time.frameIndex, w, h, path.string());
    return {};
}

namespace {
// Mimics a user dragging controls: the same calls the ControlPanel makes, chosen at random.
void stressStep(Engine& engine, Rng& rng, std::uint64_t frame) {
    const float roll = rng.nextFloat();
    auto& params = engine.params();
    if (roll < 0.35f && params.size() > 0) {
        auto* p = params.ordered()[static_cast<std::size_t>(rng.nextFloat() * static_cast<float>(params.size())) % params.size()];
        for (std::size_t c = 0; c < p->componentCount(); ++c) {
            p->setBaseComponent(c, rng.range(p->softMin(c), p->softMax(c)));
        }
    } else if (roll < 0.50f) {
        engine.seekSeconds(static_cast<double>(rng.nextFloat()) * engine.durationSeconds());
    } else if (roll < 0.60f) {
        engine.setVolume(rng.nextFloat());
    } else if (roll < 0.80f && !engine.modulator().routes().empty()) {
        auto& routes = engine.modulator().routes();
        auto& r = routes[static_cast<std::size_t>(rng.nextFloat() * static_cast<float>(routes.size())) % routes.size()];
        r.amount = rng.range(-3.0f, 6.0f);
        r.enabled = rng.nextFloat() > 0.2f;
    } else if (roll < 0.85f) {
        engine.modulator().masterGain = rng.range(0.0f, 3.0f);
    } else if (roll < 0.90f) {
        engine.togglePlay();
    } else if (roll < 0.93f) {
        engine.stop();
        if (auto r = engine.play(); !r) {
            log::debug("stress play: {}", r.error().message);
        }
    } else if (roll < 0.96f) {
        engine.seekSeconds(engine.positionSeconds() + (static_cast<double>(rng.nextFloat()) - 0.5) * 10.0);
    } else if (frame % 97 == 0) {
        engine.seekSeconds(engine.durationSeconds()); // jump to the very end
    }
}
// AVGEN_SLOW_PHASE_MS=<ms>: a phase that takes longer than this names, in the log, whatever the
// scripted interaction wrote on that frame. Turning "changing a property costs 200 ms" into the
// name of the property is otherwise guesswork, and guesswork is how a performance pass ends up
// optimising something that was never the problem.
double slowPhaseMs() {
    static const double threshold = [] {
        const char* env = std::getenv("AVGEN_SLOW_PHASE_MS");
        return env != nullptr ? std::atof(env) : 0.0;
    }();
    return threshold;
}

std::string describeWrites(const UiScript& script) {
    std::string out;
    for (const std::string& w : script.lastWrites()) {
        out += w + " ";
    }
    return out.empty() ? std::string("(nothing)") : out;
}

} // namespace


// ---- viewport interaction (ADR-068) -------------------------------------------------------------

CameraPose Application::viewportPose() const {
    CameraPose pose;
    if (engine_ == nullptr) {
        return pose;
    }
    pose.eye = engine_->scene().camera.position;
    pose.target = engine_->scene().camera.target;
    if (auto* p = engine_->params().find("camera/position")) {
        if (auto* v = dynamic_cast<params::Parameter<glm::vec3>*>(p)) {
            pose.eye = v->value();
        }
    }
    if (auto* p = engine_->params().find("camera/target")) {
        if (auto* v = dynamic_cast<params::Parameter<glm::vec3>*>(p)) {
            pose.target = v->value();
        }
    }
    return pose;
}

void Application::setViewportPose(const CameraPose& pose) {
    if (engine_ == nullptr) {
        return;
    }
    if (auto* p = engine_->params().find("camera/position")) {
        if (auto* v = dynamic_cast<params::Parameter<glm::vec3>*>(p)) {
            v->setBase(pose.eye);
        }
    }
    if (auto* p = engine_->params().find("camera/target")) {
        if (auto* v = dynamic_cast<params::Parameter<glm::vec3>*>(p)) {
            v->setBase(pose.target);
        }
    }
}

void Application::ensureFreeCamera() {
    if (engine_ == nullptr) {
        return;
    }
    // The viewport is about to move the camera by hand, so whoever else was driving it stops now.
    //
    // Without this a drag under a directed camera wrote `camera/position` and the timeline replaced
    // it on the very next frame: the mouse appeared to do nothing, and the way out was a menu item
    // you had to know was there. Reaching for the camera *is* asking for it back.
    //
    // Only the director's own tracks go. Automation somebody authored is their work, and deleting it
    // because a pointer moved would be a far worse surprise than a camera that does not budge; the
    // gesture that meets one of those says so instead (see `handleViewportEvent`).
    if (cameraDirection_.directed) {
        const std::size_t removed = releaseDirectedCamera(*engine_, cameraDirection_);
        log::info("viewport: took the camera back from the director ({} track(s) removed)", removed);
        if (panel_ != nullptr) {
            panel_->setStatus("camera handed back to the viewport -- Enable Auto-director re-cuts it");
        }
    }
    auto* p = engine_->params().find("camera/mode");
    auto* mode = dynamic_cast<params::Parameter<int>*>(p);
    if (mode == nullptr || mode->value() == 1) {
        return;
    }
    // Orbit mode ignores position and target entirely and circles the scene bounds, so a drag in
    // orbit mode moves the two parameters this writes and changes nothing on screen. Switching is
    // the only way the gesture can mean anything, and it is said out loud because it is a change to
    // the project the user did not ask for in so many words.
    mode->setBase(1);
    if (!viewportFreeModeAnnounced_) {
        viewportFreeModeAnnounced_ = true;
        log::info("viewport: camera switched to free mode so the mouse can move it");
    }
}

void Application::handleViewportEvent(const SDL_Event& event) {
    if (engine_ == nullptr) {
        return;
    }
    // SDL reports mouse positions in points, relative to the window's client area. The render
    // target is in pixels and covers the *canvas*, not the window. Two conversions, and both are
    // silent when they are wrong:
    //
    //  - points to pixels. On a retina display those differ by two, and picking without the
    //    conversion lands in the top-left quarter of the frame.
    //  - window to canvas. The canvas is inset by the left column and the menu bar, so a click
    //    converted without subtracting the canvas origin lands short by exactly that inset. The
    //    picture still moves, the click still picks *something*, and the error grows with the
    //    width of the left-hand panel -- which is the kind of wrong that survives a demo.
    const auto scale = [this]() { return std::max(window_->pixelScale(), 1e-3f); };

    // The world editor gets first refusal on the left button (ADR-092). It says so when the pointer
    // is over a gizmo handle, mid-drag, mid-box or in Place mode -- all the cases where a left drag
    // means something to the editor. A drag that started on a handle and orbited the camera instead
    // is the single most infuriating thing a viewport can do, and it is the default outcome unless
    // somebody asks this question in this order. The other buttons are always the camera's, so
    // looking around never stops being possible whatever mode the editor is in.
    const bool editorOwnsLeft = panel_ != nullptr && panel_->editor.wantsMouse();

    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        viewportLastMouse_ = glm::vec2(event.button.x, event.button.y);
        viewportDragTotal_ = glm::vec2(0.0f);
        const SDL_Keymod mods = SDL_GetModState();
        // One rule, shared with the editor, decided from the button and the modifiers rather than
        // from who asked first (`ui::viewportIntent`). The left button belongs to the editor unless
        // the camera modifier is held; the other buttons are always the camera's.
        //
        // `editorOwnsLeft` above is still read elsewhere, but it can no longer be the only thing
        // standing between a press and an orbit -- which is what made box selection unreachable,
        // because the editor does not know a press is a drag until the pointer travels and by then
        // the camera had taken the gesture.
        const ui::ViewportIntent intent = ui::viewportIntent(
            event.button.button == SDL_BUTTON_LEFT, event.button.button == SDL_BUTTON_MIDDLE,
            event.button.button == SDL_BUTTON_RIGHT, (mods & SDL_KMOD_ALT) != 0,
            (mods & SDL_KMOD_SHIFT) != 0);
        // A camera gesture that is about to be overruled by automation nobody here owns. Said once
        // per gesture rather than once per frame of it, and said rather than acted on: the tracks
        // belong to whoever wrote them.
        const auto announceAutomation = [this] {
            if (!cameraDirection_.directed && engine_->timeline().isAutomated("camera/position") &&
                panel_ != nullptr) {
                panel_->setStatus("the timeline is driving the camera -- Camera > Hand Camera Back "
                                  "to the Viewport to move it by hand");
            }
        };
        switch (intent) {
        case ui::ViewportIntent::CameraPan:
            viewportGesture_ = ViewportGesture::Pan;
            announceAutomation();
            break;
        case ui::ViewportIntent::CameraLook:
            viewportGesture_ = ViewportGesture::Look;
            announceAutomation();
            break;
        case ui::ViewportIntent::CameraOrbit:
            viewportGesture_ = ViewportGesture::Orbit;
            announceAutomation();
            break;
        case ui::ViewportIntent::EditorPointer:
        case ui::ViewportIntent::None:
            break; // the editor's, or nothing's
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (viewportGesture_ == ViewportGesture::None) {
            break;
        }
        const glm::vec2 now(event.motion.x, event.motion.y);
        const glm::vec2 delta = now - viewportLastMouse_;
        viewportLastMouse_ = now;
        viewportDragTotal_ += glm::abs(delta);
        ensureFreeCamera();
        setViewportPose(applyDrag(viewportPose(), viewportGesture_, delta, ViewportControlSettings{},
                                  engine_->scene().camera.up));
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const bool wasLeft = event.button.button == SDL_BUTTON_LEFT;
        const bool moved = viewportDragTotal_.x + viewportDragTotal_.y > 4.0f;
        const SDL_Keymod mods = SDL_GetModState();
        // A left button that went down and up without travelling is a click, not a tiny orbit.
        // Shift no longer disqualifies it: shift-click is how a selection is added to, and it only
        // means "pan" while the button is being *dragged*.
        if (wasLeft && !moved && !editorOwnsLeft) {
            const ui::CanvasPixel pixel = ui::canvasPixelFor(canvas_, event.button.x, event.button.y,
                                                             scale(), renderWidth_, renderHeight_);
            viewportPickPixel_ = glm::uvec2(pixel.x, pixel.y);
            viewportPickPending_ = true;
            viewportPickAdditive_ = (mods & SDL_KMOD_SHIFT) != 0;
            viewportPickInsideGroup_ = (mods & SDL_KMOD_ALT) != 0;
        }
        viewportGesture_ = ViewportGesture::None;
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        ensureFreeCamera();
        setViewportPose(applyDolly(viewportPose(), event.wheel.y, ViewportControlSettings{}));
        break;
    }
    default:
        break;
    }
}


// Viewport probes. Real SDL events, pushed through SDL's own queue, at points expressed as
// fractions of the canvas:
//
//   AVGEN_VIEWPORT_PROBE=<u>,<v>                one left click
//   AVGEN_VIEWPORT_DRAG=<u0>,<v0>,<u1>,<v1>     one left drag; the end may be outside the canvas
//
// Nothing about the path is bypassed: the events are routed by Window::pumpEvents, seen by ImGui,
// gated by the canvas's hover state, converted by canvasPixelFor and answered by the real picker.
// They exist because an editor's interaction cannot be checked by reading it, and a machine that
// is not allowed to post synthetic input to the window server cannot check it by hand either.
//
// Two invariants they are good for:
//   - A click at the canvas centre travels along the camera's forward axis whatever shape the
//     canvas is, so the same probe under two panel widths must report the same world position. It
//     does not if the canvas origin is not subtracted from the click.
//   - A drag that ends outside the canvas must keep moving the camera after it leaves, or a panel
//     has stolen a gesture halfway through (ADR-068).
// One raw SDL event on its way to ImGui, the viewport and the shortcut table. A member rather
// than a lambda in the loop because the loop now pumps the queue twice: once before the
// swapchain wait for window-level events, and once after it so the frame is built on the
// freshest input there is. See runLive().
namespace {

// Where an event happened, when it happened anywhere. Wheel events carry no position of their own,
// so they fall back to the pointer's current place rather than to the origin -- which is inside the
// canvas on most layouts and would have routed every scroll to the world.
[[nodiscard]] bool eventPointer(const SDL_Event& event, float& x, float& y) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        x = event.button.x;
        y = event.button.y;
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        x = event.motion.x;
        y = event.motion.y;
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        SDL_GetMouseState(&x, &y);
        return true;
    default:
        return false; // keys and everything else: not a pointer event, so not the canvas's to claim
    }
}

} // namespace

void Application::handleInputEvent(const SDL_Event& event) {
            if (uiSelfTestEvents_) {
                uiEventTypes_[event.type] += 1;
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                ++uiMotionEvents_;
                newestInputNs_ = std::max(newestInputNs_, event.motion.timestamp);
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                ++uiButtonEvents_;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                ++uiButtonEvents_;
            }
            // Output windows share the SDL queue; only the main window's input reaches ImGui.
            if (SDL_Window* from = SDL_GetWindowFromEvent(&event); from != nullptr && from != window_->handle()) {
                ++uiFilteredEvents_;
                return;
            }
            imgui_->processEvent(event);
            // The viewport gets the mouse when the pointer is over the canvas. Since the canvas is
            // an ImGui window of its own (ADR-076), ImGui's WantCaptureMouse is true whenever the
            // pointer is on the world, so it can no longer be the test -- the canvas's own hover
            // state is, and it is false when a panel, a popup or a menu is over it.
            //
            // A drag that began on the canvas keeps the mouse until the button is released:
            // letting a panel steal a gesture halfway through because the cursor passed over it is
            // how an orbit ends up jumping to a stop mid-swing.
            // `canvas_.hovered` is last frame's answer and can be stale by a whole frame -- see
            // `ui::viewportOwnsPointer`, which explains why that let a click on the sequencer
            // select something in the world, and why the pointer's actual position settles it.
            float pointerX = 0.0f;
            float pointerY = 0.0f;
            const bool positional = eventPointer(event, pointerX, pointerY);
            const bool inside = !positional || canvas_.contains(pointerX, pointerY);
            if (ui::viewportOwnsPointer(canvas_.hovered, inside,
                                        viewportGesture_ != ViewportGesture::None)) {
                handleViewportEvent(event);
            }
            // Not `wantsKeyboard`. With `NavEnableKeyboard` set, Dear ImGui reports that it wants
            // the keyboard whenever a window has nav focus, and in a docked UI where the canvas is
            // itself a window that is always -- so this guard silently disabled every editor
            // shortcut in the application. The Edit menu is what made it visible: the menu item
            // worked and Cmd+Z did not.
            //
            // The question is whether the user is *typing*, which is what must not be interrupted.
            if (event.type == SDL_EVENT_KEY_DOWN && !imgui_->wantsTextInput() &&
                !imgui_->itemActive() && handleEditorShortcut(event)) {
                return;
            }
            // The transport's keys are allowed to repeat -- holding an arrow walks the piece frame
            // by frame, which is the gesture frame stepping exists for -- so they are tested before
            // the non-repeat block below rather than inside it.
            if (event.type == SDL_EVENT_KEY_DOWN && !imgui_->wantsTextInput() && !imgui_->itemActive() &&
                handleTransportShortcut(event)) {
                return;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !imgui_->wantsTextInput() &&
                !imgui_->itemActive()) {
                // The File menu's plain keys, and the one modified one. The modifier has to be
                // tested: without it `Cmd+S` fell through to the plain `S` below and opened the Open
                // Scene dialog -- a menu advertising a key the application did not bind, which the
                // Help validator reported on every run.
                const SDL_Keymod fileMods = SDL_GetModState();
                const bool command = (fileMods & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0;
                // The modifier is named on the same line as the key on purpose: that is how the Help
                // scanner recognises a binding as `Cmd+S` rather than as a bare `S`, and a menu item
                // that advertises `Cmd+S` is only satisfied by a binding of that name.
                if (event.key.key == SDLK_S && (fileMods & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0) {
                    // Save Project. Only when there is a path to save to, exactly as the menu item
                    // is enabled: Cmd+S on an unsaved project must not silently do nothing, so it
                    // falls through to Save As.
                    if (panel_ != nullptr && !engine_->projectPath().empty() && panel_->onSaveProjectHere) {
                        panel_->onSaveProjectHere();
                    } else if (panel_ != nullptr && panel_->onSaveProject) {
                        panel_->onSaveProject();
                    }
                } else if (command) {
                    // Every other modified key belongs to somebody else; the plain bindings below
                    // must not answer for it.
                } else if (event.key.key == SDLK_O && panel_ && panel_->onOpenAudio) {
                    panel_->onOpenAudio();
                } else if (event.key.key == SDLK_S && panel_ && panel_->onOpenScene) {
                    panel_->onOpenScene();
                } else if (event.key.key == SDLK_E && panel_ && panel_->onOpenEnvironment) {
                    panel_->onOpenEnvironment();
                }
            }
}

// The transport's keyboard (ADR-102). The set every editing tool uses: space plays, the arrows step
// a frame, shift and the arrows step a beat, home and end go to the ends of the piece.
//
// Arrows reach here only when the world editor declined them, which it does when nothing is
// selected -- with a selection they nudge it, and that is the older and more specific meaning.
//
// Deliberately not J/K/L. The three-key shuttle is a video-editing convention and `L` is worth more
// here as the loop toggle, which this application has and a shuttle it does not.
bool Application::handleTransportShortcut(const SDL_Event& event) {
    if (engine_ == nullptr) {
        return false;
    }
    const SDL_Keymod mods = SDL_GetModState();
    const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
    const bool command = (mods & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0;
    if (command) {
        return false; // Cmd+arrow and Cmd+L belong to whoever wants them; the transport takes neither
    }
    app::Transport& transport = engine_->transport();
    switch (event.key.key) {
    case SDLK_SPACE:
        if (!event.key.repeat) {
            engine_->togglePlay();
        }
        return true;
    case SDLK_RETURN:
        // Return, not Home: this is the key a person's hand is already on, and Home on a laptop
        // keyboard is a chord. Safe because the transport's keys are only tested when no text field
        // and no widget has the keyboard, so Return still commits what you were typing.
        engine_->seekSeconds(transport.playStartSeconds());
        return true;
    case SDLK_END:
        engine_->seekSeconds(transport.playEndSeconds());
        return true;
    case SDLK_LEFT:
        if (shift) {
            engine_->stepBeats(-1);
        } else {
            engine_->stepFrames(-1);
        }
        return true;
    case SDLK_RIGHT:
        if (shift) {
            engine_->stepBeats(1);
        } else {
            engine_->stepFrames(1);
        }
        return true;
    case SDLK_UP:
        engine_->stepMarkers(-1);
        return true;
    case SDLK_DOWN:
        engine_->stepMarkers(1);
        return true;
    case SDLK_L:
        if (!event.key.repeat) {
            const bool wanted = !transport.loop().enabled;
            transport.setLoopEnabled(wanted);
            if (wanted && !transport.loop().usable() && engine_->durationSeconds() > 0.0) {
                transport.setLoop(app::TransportLoop{true, 0.0, engine_->durationSeconds()});
            }
        }
        return true;
    default:
        return false;
    }
}

// The editor's keyboard shortcuts (world-authoring-spec §43). The set is the one every 3D tool
// uses -- Q/W/E/R for the tool, G to group, Cmd+D to duplicate, Cmd+Z to undo -- because an artist
// arrives already knowing it, and a tool that renames the shortcuts everybody has in their hands
// is a tool that has to be learned before it can be used.
//
// Repeat is allowed for the nudge keys and for undo, which are the two anybody holds down.
bool Application::handleEditorShortcut(const SDL_Event& event) {
    if (panel_ == nullptr || engine_ == nullptr) {
        return false;
    }
    ui::WorldEditor& editor = panel_->editor;
    const SDL_Keymod mods = SDL_GetModState();
    const bool command = (mods & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0;
    const bool shift = (mods & SDL_KMOD_SHIFT) != 0;

    // The editing actions go through the application's dispatcher, not to this editor. The menu
    // asks the same `execute`, so a shortcut and a menu item cannot come to mean different things
    // -- which is the point of ADR-101 and the reason this switch stopped naming an editor.
    //
    // A recognised shortcut is consumed whether or not it could act: Cmd+Z with nothing to undo
    // must not fall through and mean something else.
    const auto dispatch = [this](EditAction action) {
        if (edits_.canExecute(action)) {
            static_cast<void>(edits_.execute(action, *engine_));
        }
        return true;
    };

    if (command) {
        switch (event.key.key) {
        case SDLK_Z:
            return dispatch(shift ? EditAction::Redo : EditAction::Undo);
        case SDLK_Y:
            return dispatch(EditAction::Redo);
        case SDLK_X:
            return dispatch(EditAction::Cut);
        case SDLK_C:
            return dispatch(EditAction::Copy);
        case SDLK_V:
            return dispatch(EditAction::Paste);
        case SDLK_D:
            return dispatch(EditAction::Duplicate);
        case SDLK_A:
            return dispatch(shift ? EditAction::SelectNone : EditAction::SelectAll);
        case SDLK_G:
            // Grouping stays the world editor's: it is not an action other editors have, and
            // inventing a global "group" for one editor is the pile of special cases this replaces.
            if (shift) {
                editor.ungroupSelection(*engine_);
            } else {
                editor.groupSelection(*engine_);
            }
            return true;
        default:
            return false;
        }
    }
    if (event.key.repeat) {
        // Only the nudges repeat below this line.
        switch (event.key.key) {
        case SDLK_UP:
        case SDLK_DOWN:
        case SDLK_LEFT:
        case SDLK_RIGHT:
            break;
        default:
            return false;
        }
    }

    // Arrow keys nudge the selection along the world axes, by the snap grid when one is set and by
    // a tenth of a metre when it is not. Holding shift makes it ten times as far, which is the
    // gesture for "about there" against "exactly there".
    const float step = (editor.snap.move > 0.0f ? editor.snap.move : 0.1f) * (shift ? 10.0f : 1.0f);
    switch (event.key.key) {
    case SDLK_Q:
        editor.mode = ui::EditorMode::Select;
        return true;
    case SDLK_B:
        editor.mode = ui::EditorMode::Place;
        return true;
    case SDLK_W:
        editor.gizmoMode = ui::GizmoMode::Move;
        editor.mode = ui::EditorMode::Select;
        return true;
    case SDLK_E:
        // E is also "open environment" in the File menu's bindings. The editor takes it while
        // something is selected, because that is when it means "rotate"; with nothing selected it
        // falls through and still opens an HDR.
        if (editor.selection.empty()) {
            return false;
        }
        editor.gizmoMode = ui::GizmoMode::Rotate;
        return true;
    case SDLK_R:
        editor.gizmoMode = ui::GizmoMode::Scale;
        editor.mode = ui::EditorMode::Select;
        return true;
    case SDLK_X:
        editor.localSpace = !editor.localSpace;
        return true;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:
        // Only ours when there is something to delete. Swallowing the key with an empty selection
        // takes it away from whatever else might want it and gives nothing back -- which is why
        // this one asks availability first, where Cmd+Z is consumed either way: nothing else in
        // this application wants Cmd+Z, and plenty might want Delete.
        if (!edits_.canExecute(EditAction::Delete)) {
            return false;
        }
        return dispatch(EditAction::Delete);
    case SDLK_UP:
        editor.nudgeSelection(*engine_, glm::vec3(0.0f, 0.0f, -step));
        return !editor.selection.empty();
    case SDLK_DOWN:
        editor.nudgeSelection(*engine_, glm::vec3(0.0f, 0.0f, step));
        return !editor.selection.empty();
    case SDLK_LEFT:
        editor.nudgeSelection(*engine_, glm::vec3(-step, 0.0f, 0.0f));
        return !editor.selection.empty();
    case SDLK_RIGHT:
        editor.nudgeSelection(*engine_, glm::vec3(step, 0.0f, 0.0f));
        return !editor.selection.empty();
    case SDLK_F: {
        if (editor.selection.empty()) {
            return false;
        }
        panel_->editPanel.frameSelectionRequested = true;
        return true;
    }
    default:
        return false;
    }
}

void Application::runViewportProbe(int frameIndex) {
    static const char* const clickSpec = std::getenv("AVGEN_VIEWPORT_PROBE");
    static const char* const dragSpec = std::getenv("AVGEN_VIEWPORT_DRAG");
    if ((clickSpec == nullptr && dragSpec == nullptr) || !canvas_.valid() || window_ == nullptr) {
        return;
    }
    const auto pointAt = [this](float u, float v) {
        return glm::vec2(canvas_.x + canvas_.width * u, canvas_.y + canvas_.height * v);
    };
    const auto push = [this](std::uint32_t type, glm::vec2 at) {
        SDL_Event e{};
        e.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            e.motion.windowID = window_->id();
            e.motion.x = at.x;
            e.motion.y = at.y;
        } else {
            e.button.windowID = window_->id();
            e.button.button = SDL_BUTTON_LEFT;
            e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.clicks = 1;
            e.button.x = at.x;
            e.button.y = at.y;
        }
        SDL_PushEvent(&e);
    };

    if (clickSpec != nullptr) {
        float u = 0.5f;
        float v = 0.5f;
        if (std::sscanf(clickSpec, "%f,%f", &u, &v) == 2) {
            const glm::vec2 at = pointAt(u, v);
            // The move goes first and early: the canvas's hover state, which is what lets the click
            // reach the scene at all, is settled by ImGui on the frame after the pointer arrives.
            if (frameIndex == 120) {
                log::info("probe: canvas ({:.0f},{:.0f}) {:.0f}x{:.0f} points, render {}x{} px; clicking ({:.1f},{:.1f})",
                          canvas_.x, canvas_.y, canvas_.width, canvas_.height, renderWidth_, renderHeight_, at.x, at.y);
                push(SDL_EVENT_MOUSE_MOTION, at);
            } else if (frameIndex == 150) {
                push(SDL_EVENT_MOUSE_BUTTON_DOWN, at);
            } else if (frameIndex == 152) {
                push(SDL_EVENT_MOUSE_BUTTON_UP, at);
            }
        }
        return;
    }

    float u0 = 0.5f;
    float v0 = 0.5f;
    float u1 = 0.0f;
    float v1 = 0.5f;
    if (std::sscanf(dragSpec, "%f,%f,%f,%f", &u0, &v0, &u1, &v1) != 4) {
        return;
    }
    constexpr int kStart = 120;
    constexpr int kSteps = 16;
    const auto say = [this](const char* what, glm::vec2 at) {
        const CameraPose pose = viewportPose();
        log::info("probe-drag: {} at ({:.1f},{:.1f}) {} canvas; eye ({:.3f}, {:.3f}, {:.3f})", what, at.x, at.y,
                  canvas_.contains(at.x, at.y) ? "inside" : "OUTSIDE", pose.eye.x, pose.eye.y, pose.eye.z);
    };
    if (frameIndex == kStart) {
        push(SDL_EVENT_MOUSE_MOTION, pointAt(u0, v0));
        say("start", pointAt(u0, v0));
    } else if (frameIndex == kStart + 30) {
        push(SDL_EVENT_MOUSE_BUTTON_DOWN, pointAt(u0, v0));
    } else if (frameIndex > kStart + 30 && frameIndex <= kStart + 30 + kSteps * 2 && (frameIndex & 1) == 0) {
        const float t = static_cast<float>(frameIndex - kStart - 30) / static_cast<float>(kSteps * 2);
        const glm::vec2 at = pointAt(u0 + (u1 - u0) * t, v0 + (v1 - v0) * t);
        push(SDL_EVENT_MOUSE_MOTION, at);
        say("moved", at);
    } else if (frameIndex == kStart + 30 + kSteps * 2 + 4) {
        const glm::vec2 at = pointAt(u1, v1);
        push(SDL_EVENT_MOUSE_BUTTON_UP, at);
        say("released", at);
    }
}

std::size_t Application::placeAt(glm::vec3 position, glm::vec3 normal) {
    if (placementAssetId_.empty() || engine_ == nullptr || panel_ == nullptr) {
        return 0;
    }
    const assets::AssetLibrary* library = panel_->worldBuilder.library();
    if (library == nullptr) {
        log::warn("place: no asset library is loaded");
        return 0;
    }
    const assets::AssetDescriptor* asset = library->find(placementAssetId_);
    if (asset == nullptr) {
        log::warn("place: '{}' is not in the library", placementAssetId_);
        return 0;
    }
    const std::string file = library->resolve(*asset).generic_string();
    if (file.empty()) {
        log::warn("place: '{}' has no file", placementAssetId_);
        return 0;
    }

    const auto plan = planPlacements(placement_, position, normal, placementSeed_++);
    // The library's own sizing, applied once. The placement's jitter multiplies it, so a plan can be
    // reasoned about in "how much bigger than usual" without knowing anything about the pack the
    // asset came from.
    const float base = normalisingScale(*asset);

    std::vector<std::string> taken;
    if (const auto* composition = engine_->composition()) {
        for (const auto& node : composition->nodes()) {
            if (node) {
                taken.push_back(node->name);
            }
        }
    }

    std::size_t made = 0;
    for (const Placement& p : plan) {
        scene::CompositionNode node;
        node.name = uniquePlacementName(placementAssetId_, taken);
        taken.push_back(node.name);
        node.kind = scene::NodeKind::Gltf;
        node.asset = file;
        node.transform.position = p.position;
        node.transform.scale = glm::vec3(base * p.scale);
        glm::quat rotation = glm::angleAxis(p.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        if (placement_.alignToNormal) {
            // Stand along the surface rather than along the world's up. The shortest rotation from
            // up to the normal, then the yaw about that -- applied in this order, or the yaw is
            // about the wrong axis and objects on a slope all face the same way regardless of it.
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            const glm::vec3 n = glm::normalize(p.normal);
            if (glm::dot(up, n) < 0.9999f) {
                rotation = glm::rotation(up, n) * rotation;
            }
        }
        node.transform.rotation = rotation;
        if (auto added = engine_->addNode(std::move(node)); !added) {
            log::warn("place: {}", added.error().message);
        } else {
            ++made;
        }
    }
    if (made > 0) {
        log::info("place: {} x '{}' ({}) at ({:.2f}, {:.2f}, {:.2f})", made, placementAssetId_,
                  placementModeName(placement_.mode), position.x, position.y, position.z);
    }
    return made;
}

void Application::serviceViewportPick() {
    if (!viewportPickPending_ || renderer_ == nullptr || engine_ == nullptr) {
        return;
    }
    viewportPickPending_ = false;

    PickView view;
    // The canvas, not the window: the identifier and depth targets are the size the world was
    // rendered at, and the ray a pixel stands for is built from the same aspect the camera used.
    const std::uint32_t pw = renderWidth_;
    const std::uint32_t ph = renderHeight_;
    if (pw == 0 || ph == 0) {
        return;
    }
    view.size = glm::uvec2(pw, ph);
    const scene::Camera& camera = engine_->scene().camera;
    const float aspect = static_cast<float>(pw) / static_cast<float>(std::max(ph, 1u));
    view.invViewProj = glm::inverse(camera.projection(aspect) * camera.view());
    view.cameraPosition = camera.position;
    view.cameraForward = glm::normalize(camera.target - camera.position);

    auto result = pickAt(*context_, renderer_->identifierTexture(), renderer_->linearDepthTexture(),
                         view, viewportPickPixel_);
    if (!result) {
        log::warn("pick: {}", result.error().message);
        return;
    }
    if (!result->hit) {
        // The sky. Deselecting is what a click on nothing means everywhere else, so it means it
        // here too rather than leaving the previous selection stuck -- unless this was a shift
        // click, which is an addition and must not throw away what is already chosen.
        viewportSelectedNode_.clear();
        if (panel_ != nullptr) {
            panel_->editor.applyPick(*engine_, {}, viewportPickAdditive_, viewportPickInsideGroup_);
            if (!viewportPickAdditive_) {
                panel_->world.selection = ui::WorldSelection{};
            }
        }
        return;
    }
    viewportPickPosition_ = result->position;
    const auto* composition = engine_->composition();
    // Which numbering this id belongs to, then the resolver for it. Three renderers write into one
    // object id and each counts from zero, so the tag is the only thing that says what the number
    // counts (scene_types.hpp, `PickSpace`). Resolving every id as an entity index -- which is what
    // this did -- meant a click on anything scattered selected whichever node owned that entity, or
    // nothing: in a scatter world, almost every click.
    const scene::CompositionNode* node = nullptr;
    if (composition != nullptr) {
        const std::uint32_t index = scene::pickIndexOf(result->objectId);
        switch (scene::pickSpaceOf(result->objectId)) {
        case scene::PickSpace::Entity:
            node = composition->nodeForEntity(index);
            break;
        case scene::PickSpace::Procedural:
            node = composition->nodeForProcedural(index);
            break;
        case scene::PickSpace::Sdf:
            break; // no resolver yet; a click on one deselects rather than selecting somebody else
        }
    }
    if (node == nullptr) {
        // Geometry no node owns -- terrain chunks, an SDF, a procedural the composition did not
        // emit. The position is still useful.
        viewportSelectedNode_.clear();
        log::info("pick: surface at ({:.2f}, {:.2f}, {:.2f})", result->position.x,
                  result->position.y, result->position.z);
        return;
    }
    viewportSelectedNode_ = node->name;
    if (panel_ != nullptr) {
        // The editor decides what a click on this node *means* -- whether it selects the node or
        // the group it is in, and whether it replaces the selection or adds to it (ADR-092). The
        // World panel's single selection follows it so the inspector still shows what is in hand.
        panel_->editor.applyPick(*engine_, node->name, viewportPickAdditive_, viewportPickInsideGroup_);
        panel_->world.selection.kind = ui::WorldSelection::Kind::Node;
        panel_->world.selection.name = panel_->editor.selection.empty()
                                           ? node->name
                                           : panel_->editor.selection.primary();
    }
    log::info("pick: '{}' at ({:.2f}, {:.2f}, {:.2f})", node->name, result->position.x,
              result->position.y, result->position.z);
}


namespace {

// `--director mode=continuous,maxShot=6.8,maxSpeed=0.4`. Unknown keys are refused rather than
// ignored: a typo in a measurement's arguments that silently measures the default is worse than no
// flag at all, and this flag exists to make measurements reproducible.
Result<void> applyDirectorArgs(AutoDirectorSettings& s, std::string_view spec) {
    std::size_t at = 0;
    while (at <= spec.size()) {
        const std::size_t comma = spec.find(',', at);
        std::string_view item = spec.substr(at, comma == std::string_view::npos ? comma : comma - at);
        at = comma == std::string_view::npos ? spec.size() + 1 : comma + 1;
        if (item.empty()) {
            continue;
        }
        const std::size_t eq = item.find('=');
        if (eq == std::string_view::npos) {
            return fail("--director: '{}' is not key=value", item);
        }
        const std::string key(item.substr(0, eq));
        const std::string value(item.substr(eq + 1));
        const auto number = [&](double& out) -> Result<void> {
            try {
                std::size_t used = 0;
                out = std::stod(value, &used);
                if (used != value.size()) {
                    return fail("--director: '{}' is not a number for '{}'", value, key);
                }
            } catch (const std::exception&) {
                return fail("--director: '{}' is not a number for '{}'", value, key);
            }
            return {};
        };
        double v = 0.0;
        // ADR-217: two of the hold's three settings are names rather than numbers, so they are
        // handled before the number parse the rest share.
        if (key == "holdScenario") {
            s.holdScenario = value;
            continue;
        }
        if (key == "holdRole") {
            s.holdRole = value;
            continue;
        }
        if (key == "mode") {
            if (value == "continuous") {
                s.mode = DirectorMode::ContinuousShot;
            } else if (value == "edited") {
                s.mode = DirectorMode::EditedSequence;
            } else {
                return fail("--director: mode must be 'continuous' or 'edited', got '{}'", value);
            }
            continue;
        }
        if (auto ok = number(v); !ok) {
            return std::unexpected(ok.error());
        }
        if (key == "minShot") {
            s.minShotSeconds = v;
        } else if (key == "minBuildShot") {
            s.minBuildShotSeconds = v;
        } else if (key == "maxShot") {
            s.maxShotSeconds = v;
        } else if (key == "wide") {
            s.wideFocalLength = static_cast<float>(v);
        } else if (key == "hero") {
            s.heroFocalLength = static_cast<float>(v);
        } else if (key == "maxSpeed") {
            s.maxCameraSpeed = static_cast<float>(v);
        } else if (key == "maxSwing") {
            s.maxViewRate = static_cast<float>(v);
        } else if (key == "dwell") {
            s.dwellShots = static_cast<int>(v);
        } else if (key == "holdRelease") {
            s.holdReleaseSeconds = v;
        } else if (key == "seed") {
            s.seed = static_cast<std::uint32_t>(std::max(0.0, v));
        } else {
            return fail("--director: unknown setting '{}'", key);
        }
    }
    return s.validate();
}

} // namespace

Result<void> Application::directCameraFromTrack() {
    if (engine_ == nullptr || engine_->composition() == nullptr) {
        return fail("--direct needs a scene; load a project or generate a world first");
    }
    if (!options_.directorSettings.empty()) {
        if (auto ok = applyDirectorArgs(cameraDirection_.settings, options_.directorSettings); !ok) {
            return std::unexpected(ok.error());
        }
    }
    // Heroes come from whichever source the world has one. An authored scene declares them
    // (ADR-074); a generated world's composer places them (ADR-072). Preferring the scene's own is
    // deliberate: if somebody wrote them down, those are the ones they meant.
    std::vector<world::HeroPoint> heroes = engine_->composition()->heroes();
    if (heroes.empty() && panel_ != nullptr && panel_->worldBuilder.lastWorld) {
        heroes = panel_->worldBuilder.lastWorld->composed.plan.heroes;
    }
    if (heroes.empty()) {
        return fail("--direct found no heroes to shoot: open the World window and star an object "
                    "in its Objects list to make it a hero, declare them in the scene's \"heroes\" "
                    "block, or generate a world");
    }
    // The panel's settings, not the defaults. Omitting this argument is how "select Continuous shot
    // and nothing changes" happened: `refreshDirection` passed `state.settings` on an automatic
    // re-cut, and the *first* cut -- the one the Enable button makes, and the one a settings change
    // re-triggers -- silently took `AutoDirectorSettings{}`. Every control in the panel was bound to
    // a struct nothing on this path read.
    auto installed = directEngine(*engine_, heroes, cameraDirection_.settings);
    if (!installed) {
        return std::unexpected(installed.error());
    }
    log::info("direct: {} camera track(s) from {} hero(es)", *installed, heroes.size());
    if (panel_ != nullptr) {
        panel_->directorSummary = lastDirectionSummary();
    }
    // From now until the camera is handed back, the shot follows the heroes: starring an object
    // re-cuts it on the next frame rather than waiting to be asked (`refreshDirection`).
    noteDirected(*engine_, cameraDirection_);
    return {};
}

int Application::run() {
    if (!options_.aiPrompt.empty() || options_.aiScript) {
        if (const int aiCode = runAiTask(); aiCode != 0) {
            return aiCode;
        }
    }
    const int code = options_.headless ? runHeadless() : runLive();
    if (options_.saveProject) {
        storeOutputsToProject();
        if (auto r = engine_->saveProject(*options_.saveProject); !r) {
            log::error("save project: {}", r.error().message);
            return code == 0 ? 6 : code;
        }
        log::info("project saved to {}", options_.saveProject->string());
    }
    if (options_.bundle) {
        if (auto r = engine_->exportBundle(*options_.bundle); !r) {
            log::error("bundle: {}", r.error().message);
            return code == 0 ? 6 : code;
        }
    }
    return code;
}

int Application::runLive() {
    RealtimeClock clock;
    ui::FrameStats stats;
    stats.adapter = context_->capabilities().adapterName;
    stats.backend = context_->capabilities().backendName;
    double fpsAccum = 0.0;
    int fpsFrames = 0;
    auto fpsStart = std::chrono::steady_clock::now();
    int framesRendered = 0;
    FrameTime lastTime{};
    Rng stressRng(options_.stressSeed);

    // The main thread's frame, phase by phase. Resolved once: `phase()` is a linear scan over the
    // names and is not for the hot path, and a function-local static would be a guarded load per
    // frame for no reason when the loop can simply hold them.
    core::PhaseProfiler& prof = cpuProfile_;
    const int kPhScript = prof.phase("ui.script");
    // The AI control plane's per-frame cost, named so it appears in --profile-cpu like everything
    // else. An idle plane must measure as nothing, and a phase is how that is checked rather than
    // asserted.
    const int kPhAi = prof.phase("ai.pump");
    const int kPhEvents = prof.phase("events.poll");
    const int kPhEngine = prof.phase("engine.update");
    const int kPhUi = prof.phase("ui.build");
    const int kPhAcquire = prof.phase("gpu.acquire WAIT");
    const int kPhDebug = prof.phase("debug.geometry");
    const int kPhRecord = prof.phase("render.record");
    const int kPhImgui = prof.phase("imgui.record");
    const int kPhSubmit = prof.phase("gpu.submit");
    const int kPhPresent = prof.phase("gpu.present WAIT");
    const int kPhPick = prof.phase("viewport.pick");
    const int kPhOutputs = prof.phase("outputs+share");
    const int kPhJob = prof.phase("render.job");
    const int kPhEvProc = prof.phase("gpu.processEvents");
    const int kPhResize = prof.phase("canvas.resize");
    const int kPhLatency = prof.phase("input->present ms");
    const int kPhAllocK = prof.phase("# kallocs/frame");
    const int kPhAllocUi = prof.phase("# allocs ui.build");
    const int kPhAllocEngine = prof.phase("# allocs engine.upd");
    const int kPhAllocRecord = prof.phase("# allocs render.rec");
    const int kPhAllocImgui = prof.phase("# allocs imgui.rec");
    if (auto arms = parseUiScript(options_.uiScript)) {
        uiScript_ = UiScript(*arms);
    }
    if (uiScript_.active()) {
        log::info("ui-script: '{}' driving the editor", options_.uiScript);
    }

    for (;;) {
        const auto frameStart = std::chrono::steady_clock::now();
        prof.beginFrame();
        const std::uint64_t allocsAtFrameStart = core::allocCounters().allocations;
        // Last frame's canvas. Events are read before this frame is laid out, so this is the most
        // recent answer there is; on a still window it is the current one.
        if (panel_ != nullptr) {
            canvas_ = panel_->canvas();
        }
        // Before anything else in the frame: a request made last frame is honoured now, so the
        // "Opening ..." frame it set up has already been presented. See `loadAny`.
        servicePendingOpen();
        const auto eventsStart = std::chrono::steady_clock::now();
        newestInputNs_ = 0;
        auto events = window_->pollEvents([this](const SDL_Event& e) { handleInputEvent(e); });
        prof.add(kPhEvents, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                        eventsStart).count());
        if (events.quit) {
            break;
        }
        for (const auto& dropped : events.droppedFiles) {
            loadAny(dropped);
        }
        if (window_->minimised()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (events.resized || pendingResize_) {
            pendingResize_ = false;
            context_->configureSurface(window_->pixelWidth(), window_->pixelHeight());
            if (auto r = renderer_->resize(window_->pixelWidth(), window_->pixelHeight()); !r) {
                log::error("resize: {}", r.error().message);
            }
        }

        if (options_.stressSeed != 0) {
            for (int i = 0; i < 3; ++i) {
                stressStep(*engine_, stressRng, static_cast<std::uint64_t>(framesRendered));
            }
        }
        if (!engineShaderWatcher_.poll().empty()) {
            if (auto r = renderer_->reloadEngineShaders(); !r) {
                panel_->setStatus(r.error().message);
            } else {
                panel_->setStatus("engine shaders reloaded");
            }
        }
        // The world is rendered at the size of the canvas, not of the window. Everything downstream
        // follows from this one pair of numbers: the renderer's targets, the camera's aspect (the
        // scene renderer takes it from its own HDR target) and the pixel a click resolves to.
        // Before the first frame is laid out there is no canvas, and the window is the best guess.
        const float pixelScale = std::max(window_->pixelScale(), 1e-3f);
        std::uint32_t cw = window_->pixelWidth();
        std::uint32_t ch = window_->pixelHeight();
        // The canvas's pixels, times the editor's render scale. The scale is the editor's one
        // lever on what the world costs: the canvas is the display's backing scale times its own
        // size, so on a Retina screen it is several times the pixel count the renderer's benchmarks
        // quote, and the person at the keyboard had no way to say "softer, but keep up". The
        // default is 1.0, which is every canvas pixel and exactly what this did before.
        const float renderScale =
            panel_ != nullptr ? std::clamp(panel_->canvasRenderScale, 0.25f, 1.0f) : 1.0f;
        if (canvas_.valid()) {
            cw = std::max(1u, static_cast<std::uint32_t>(std::lround(canvas_.width * pixelScale * renderScale)));
            ch = std::max(1u, static_cast<std::uint32_t>(std::lround(canvas_.height * pixelScale * renderScale)));
        }
        // A new size only takes effect once it has held still for a few frames. Following every
        // frame of a splitter drag would be more correct and much worse: each size is a new render
        // target and a new texture view, and ImGui's WebGPU backend caches a bind group per view
        // for the life of the context. The picture stretches slightly for those few frames and is
        // exact the moment the splitter is let go.
        if (cw != pendingWidth_ || ch != pendingHeight_) {
            pendingWidth_ = cw;
            pendingHeight_ = ch;
            pendingFrames_ = 0;
        } else {
            ++pendingFrames_;
        }
        constexpr int kResizeSettleFrames = 6;
        if ((cw != renderWidth_ || ch != renderHeight_) &&
            (renderWidth_ == 0 || pendingFrames_ >= kResizeSettleFrames)) {
            if (auto r = renderer_->resize(cw, ch); !r) {
                log::error("canvas resize: {}", r.error().message);
            } else {
                renderWidth_ = cw;
                renderHeight_ = ch;
            }
        }
        // The final texture has to exist before the panel draws, because the canvas window shows it
        // and ImGui records the texture id while it lays the frame out -- the drawing into it
        // happens later in the same encoder, so the image is this frame's, not the last one's.
        const auto resizeStart = std::chrono::steady_clock::now();
        const bool retiringView = finalTexture_ != nullptr &&
                                  (finalWidth_ != renderWidth_ || finalHeight_ != renderHeight_);
        if (auto r = ensureFinalTexture(renderWidth_, renderHeight_); !r) {
            log::error("final texture: {}", r.error().message);
            return 2;
        }
        if (retiringView) {
            // The view the canvas was showing is gone. Nothing else releases the bind group ImGui
            // built for it, and that bind group is the last reference to a whole render target.
            imgui_->forgetCachedTextures();
        }
        if (panel_ != nullptr) {
            panel_->canvasTexture = reinterpret_cast<std::uint64_t>(finalView_.Get());
        }
        prof.add(kPhResize, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                      resizeStart).count());

        // ---- the swapchain wait, and then the frame ------------------------------------------
        //
        // The wait comes *before* the input is sampled and the frame is built, which is the reverse
        // of the obvious order and the point of it.
        //
        // Under Fifo this wait is however long the CPU must stand still before a swapchain image is
        // free: the vsync pace when the frame is cheap, the GPU's backlog when it is not -- 15.6 ms
        // of a 17.2 ms frame on the world scene. Building the UI before it meant the picture that
        // reached the screen was built from input sampled a whole wait earlier, so every millisecond
        // the GPU fell behind was also a millisecond of staleness in the pointer. The frame rate did
        // not show it; input-to-present latency did, at 17.2 ms median where the work is 1.6.
        //
        // So: wait first, then read whatever the device produced during the wait, then build the
        // frame from it. The queue is pumped twice per frame -- the first pass takes the
        // window-level events (resize, close, dropped files) that have to be acted on before a
        // surface image is asked for at all, and this pass takes the input. Nothing is dropped:
        // both passes go through the same handler and the same ImGui backend, and the second pass's
        // window events are merged into the first's.
        const auto workBeforeAcquire = std::chrono::steady_clock::now();
        auto view = context_->acquireSurfaceView();
        const auto workAfterAcquire = std::chrono::steady_clock::now();
        // A WAIT, not work, and not GPU time either: this is how long the CPU stood still because
        // no swapchain image was free. Under Fifo it is the vsync pace when the frame is cheap and
        // the GPU's backlog when it is not. Naming it as a wait is the whole point -- a number that
        // grows when the GPU is the bottleneck must not be read as the CPU getting slower.
        prof.add(kPhAcquire,
                 std::chrono::duration<double, std::milli>(workAfterAcquire - workBeforeAcquire).count());
        if (!view) {
            // Nothing has been submitted to ImGui yet this frame -- the UI is built below, after
            // the wait -- so there is no frame to end here.
            log::warn("frame skipped: {}", view.error().message);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        // Late input. Anything the pointer or keyboard produced while the CPU was waiting above is
        // in the queue now, and this frame uses it rather than showing it one frame later.
        {
            const auto lateStart = std::chrono::steady_clock::now();
            if (uiScript_.active()) {
                // The script stands in for the device, so it produces its events where a device's
                // would be: just before the poll that consumes them. Pushing them at the top of the
                // frame instead would date every synthetic event by a whole wait and quietly make
                // the measurement insensitive to the very thing being measured.
                core::PhaseProfiler::Scope scope(prof, kPhScript);
                uiScript_.step(*engine_, panel_.get(), *window_, static_cast<std::uint64_t>(framesRendered));
            }
            const platform::FrameEvents late =
                window_->pollEvents([this](const SDL_Event& e) { handleInputEvent(e); });
            events.quit = events.quit || late.quit;
            // This frame's surface is already configured and its image already acquired, so a
            // resize that arrives now is next frame's business.
            pendingResize_ = pendingResize_ || late.resized;
            for (const std::string& dropped : late.droppedFiles) {
                loadAny(dropped);
            }
            prof.add(kPhEvents,
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lateStart).count());
            if (events.quit) {
                break;
            }
        }

        // The panel edited these in place during the last frame's UI; carry them to the copy the
        // project file is written from, and take a loaded project's copy back (ADR-225).
        syncDirectorSettings();

        // Before the clock, so a shot re-cut this frame is the shot this frame renders.
        if (auto redirected = refreshDirection(*engine_, cameraDirection_); !redirected) {
            log::warn("direct: {}", redirected.error().message);
            if (panel_) {
                panel_->setStatus("could not re-cut the shot: " + redirected.error().message);
            }
        } else if (panel_ != nullptr) {
            if (*redirected == Redirect::Recut) {
                renderer_->resetTemporalHistory();
                panel_->setStatus(fmt::format("camera re-cut for {} hero(es)",
                                              engine_->composition()->heroes().size()));
            } else if (*redirected == Redirect::HandedBack) {
                renderer_->resetTemporalHistory();
                panel_->setStatus("no heroes left: the camera is back with the viewport");
            }
        }

        const FrameTime time = engine_->tick(clock);
        lastTime = time;
        const std::uint64_t discontinuity = engine_->transport().discontinuityRevision();
        if (discontinuity != lastTransportDiscontinuity_) {
            renderer_->resetTemporalHistory();
            lastTransportDiscontinuity_ = discontinuity;
        }
        engine_->setViewport(renderWidth_, renderHeight_);
        {
            const std::uint64_t allocsBefore = core::allocCounters().allocations;
            const auto updateStart = std::chrono::steady_clock::now();
            engine_->update(time);
            prof.count(kPhAllocEngine, static_cast<double>(core::allocCounters().allocations - allocsBefore));
            const double updateMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();
            prof.add(kPhEngine, updateMs);
            if (slowPhaseMs() > 0.0 && updateMs > slowPhaseMs()) {
                log::warn("slow engine.update {:.1f} ms at frame {}: wrote {}", updateMs, framesRendered,
                          describeWrites(uiScript_));
            }
        }

        stats.width = renderWidth_;
        stats.height = renderHeight_;
        stats.drawCalls = renderer_->stats().drawCalls;
        stats.triangles = renderer_->stats().triangles;
        stats.procedural = renderer_->stats().procedural;
        stats.sdf = renderer_->stats().sdf;
        stats.particles = renderer_->stats().particles;
        stats.gpuFrameMs = renderer_->timeline().frameMs();
        lastCpuFrameMs_ = stats.cpuFrameMs;
        lastFrameIntervalMs_ = stats.frameIntervalMs;
        lastFps_ = stats.fps;
        lastGpuFrameMs_ = stats.gpuFrameMs;

        // The AI control plane's only claim on the frame (ADR-094, spec §38). Queued tool bodies
        // run here, on this thread, under a time budget -- the agent loop itself and every network
        // call are on a job worker and cannot reach the engine except through this queue. With no
        // task running this is one mutex and an empty-deque check.
        if (ai_) {
            core::PhaseProfiler::Scope scope(prof, kPhAi);
            ai_->pump();
        }

        // Background render: a few frames per UI frame, then the next queued job.
        if (job_) {
            core::PhaseProfiler::Scope scope(prof, kPhJob);
            if (job_->step(4, 0.010)) {
                const auto p = job_->progress();
                lastRender_ = p;
                panel_->setStatus(p.error.empty() ? fmt::format("render done: {} frames, hash {:016x}", p.framesRendered,
                                                                p.sequenceHash)
                                                  : "render failed: " + p.error);
                job_.reset();
                if (!uiQueue_.empty() && p.error.empty() && panel_->onRunQueue) {
                    panel_->onRunQueue();
                }
            }
        }
        // Input diagnostics (AVGEN_UI_SELFTEST=1): logs what ImGui and SDL each see of the
        // pointer, plus the raw event counts, so "the UI does not react to clicks" can be traced
        // to the event routing rather than the widgets.
        static const bool uiSelfTest = std::getenv("AVGEN_UI_SELFTEST") != nullptr;
        uiSelfTestEvents_ = uiSelfTest;
        {
            core::PhaseProfiler::AllocScope scope(prof, kPhUi, kPhAllocUi);
            imgui_->newFrame();
            panel_->draw(*engine_, stats);
        }
        if (uiSelfTest && (time.frameIndex % 30) == 0) {
            const ImGuiIO& io = ImGui::GetIO();
            int wx = 0;
            int wy = 0;
            SDL_GetWindowPosition(window_->handle(), &wx, &wy);
            float gx = 0.0f;
            float gy = 0.0f;
            SDL_GetGlobalMouseState(&gx, &gy);
            float lx = 0.0f;
            float ly = 0.0f;
            SDL_GetMouseState(&lx, &ly);
            const bool keyboardFocus = SDL_GetKeyboardFocus() == window_->handle();
            const bool mouseFocus = SDL_GetMouseFocus() == window_->handle();
            log::info("ui-sdl: windowPos ({},{}) global ({:.1f},{:.1f}) local ({:.1f},{:.1f}) "
                      "keyboardFocus={} mouseFocus={} winFlags=0x{:x} sdlEvents motion={} buttons={} filtered={}",
                      wx, wy, gx, gy, lx, ly, keyboardFocus, mouseFocus,
                      static_cast<std::uint64_t>(SDL_GetWindowFlags(window_->handle())), uiMotionEvents_,
                      uiButtonEvents_, uiFilteredEvents_);
            std::string types;
            for (const auto& [type, count] : uiEventTypes_) {
                types += fmt::format("0x{:x}:{} ", type, count);
            }
            log::info("ui-types: {}", types.empty() ? std::string("(none)") : types);
            log::info("ui: display {:.0f}x{:.0f} scale {:.2f} mouse ({:.1f},{:.1f}) down={} captureMouse={} "
                      "hovered='{}' anyItemHovered={} dt={:.4f}",
                      io.DisplaySize.x, io.DisplaySize.y, io.DisplayFramebufferScale.x, io.MousePos.x, io.MousePos.y,
                      io.MouseDown[0], io.WantCaptureMouse,
                      ImGui::GetCurrentContext()->HoveredWindow != nullptr
                          ? ImGui::GetCurrentContext()->HoveredWindow->Name
                          : "(none)",
                      ImGui::IsAnyItemHovered(), io.DeltaTime);
        }

        const std::uint32_t pw = window_->pixelWidth();
        const std::uint32_t ph = window_->pixelHeight();
        // The frame renders into the offscreen final texture; every projection output still
        // presents it through the output mapper (ADR-022), and the main window shows it inside the
        // editor's canvas (ADR-076).
        gpu::TargetView finalTarget{finalView_, wgpu::TextureFormat::BGRA8Unorm, renderWidth_,
                                    renderHeight_};
        gpu::TargetView target{*view, context_->surfaceFormat(), pw, ph};
        wgpu::CommandEncoder encoder = context_->device().CreateCommandEncoder();
        // Debug drawing (ADR-031): build this frame's inspection geometry from the World window's
        // options; an empty set costs nothing.
        if (panel_) {
            core::PhaseProfiler::Scope scope(prof, kPhDebug);
            panel_->composition.stats = &compositor_->stats();
            rendering::DebugViewOptions options = panel_->world.debug;
            // The frustum overlay is drawn at the aspect the frame is *actually* being rendered at,
            // not the option's default: a box drawn at 16:9 over a 2:1 viewport is a wrong shape
            // that looks like a culling bug.
            if (renderHeight_ > 0) {
                options.frustumAspect = static_cast<float>(renderWidth_) / static_cast<float>(renderHeight_);
            }
            renderer_->setDiagnosticEntity(options.selectedEntity);
            renderer_->setDebugDepthTest(options.depthTest);
            transformHistory_.setSubject(options.selectedEntity);
            rendering::buildDebugGeometry(renderer_->debugDraw(), engine_->scene(), options, time.renderTime,
                                          &transformHistory_);
        }
        const rendering::ShaderFrameInputs shaderInputs{&engine_->shaderLayers(),
                                                        engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
        {
            // The CPU cost of *recording* the frame's commands. Not the GPU's cost of running them:
            // that is gpu::FrameTimeline's, is reported separately, and belongs to the renderer
            // effort rather than to this one.
            const std::uint64_t recordAllocsBefore = core::allocCounters().allocations;
            const auto recordStart = std::chrono::steady_clock::now();
            if (auto r = renderer_->render(encoder, engine_->scene(), time, finalTarget, &shaderInputs); !r) {
                log::error("render: {}", r.error().message);
                return 2;
            }
            const double recordMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - recordStart).count();
            prof.add(kPhRecord, recordMs);
            prof.count(kPhAllocRecord,
                       static_cast<double>(core::allocCounters().allocations - recordAllocsBefore));
            if (slowPhaseMs() > 0.0 && recordMs > slowPhaseMs()) {
                log::warn("slow render.record {:.1f} ms at frame {}: wrote {}", recordMs, framesRendered,
                          describeWrites(uiScript_));
            }
        }
        // The frame has published its diagnosis; keep it. Recording after the render and drawing
        // before it means the trail is one frame behind the picture, which is the honest ordering:
        // a trail drawn from a frame that has not been rendered would be a prediction.
        transformHistory_.record(renderer_->diagnosticFrame());
        // Clearing the window is the whole of the main window's present now: the editor draws the
        // frame into its canvas, and the world no longer covers the surface for the panels to be
        // painted over. The clear still has to happen, because ImGui's pass loads rather than
        // clears and the swapchain image it gets is whatever was last in it.
        {
            wgpu::RenderPassColorAttachment ground{};
            ground.view = *view;
            ground.loadOp = wgpu::LoadOp::Clear;
            ground.storeOp = wgpu::StoreOp::Store;
            ground.clearValue = {0.04, 0.04, 0.05, 1.0};
            wgpu::RenderPassDescriptor pass{};
            pass.label = "editor-ground";
            pass.colorAttachmentCount = 1;
            pass.colorAttachments = &ground;
            encoder.BeginRenderPass(&pass).End();
        }
        {
            core::PhaseProfiler::AllocScope scope(prof, kPhImgui, kPhAllocImgui);
            imgui_->render(encoder, target);
        }
        {
            core::PhaseProfiler::Scope scope(prof, kPhSubmit);
            wgpu::CommandBuffer commands = encoder.Finish();
            context_->queue().Submit(1, &commands);
        }
        renderer_->collectFrameTimings();
        compositor_->collectTimings();
        if (panel_ != nullptr) {
            // Placement is the world editor's now (ADR-092): it plans and commits inside the UI
            // pass against the CPU ground probe, so a click no longer costs a GPU round trip and a
            // *hover* can show what the click would do. These two are kept in step with it so a
            // scripted or headless caller still has one place to arm a brush from.
            placementAssetId_ = panel_->editor.brushAssetId;
            placement_ = panel_->editor.brush;
            // What the Edit panel asked for: look at what is selected.
            if (panel_->editPanel.frameSelectionRequested) {
                panel_->editPanel.frameSelectionRequested = false;
                if (auto* composition = engine_->composition()) {
                    const scene::WorldBounds bounds =
                        ui::selectionBounds(*composition, panel_->editor.selection.nodes());
                    if (bounds.valid) {
                        ensureFreeCamera();
                        setViewportPose(frameSphere(viewportPose(), bounds.centre(),
                                                    std::max(bounds.radius(), 0.5f),
                                                    engine_->scene().camera.effectiveFovY()));
                        log::info("viewport: framed {} object(s)", panel_->editor.selection.size());
                    }
                }
            }
            // "Frame it" on a hero. The panel asks and the viewport answers, because the camera
            // belongs to the viewport: a panel that moved the camera itself would be a second thing
            // writing camera/position, and the two would fight during a drag.
            if (panel_->worldBuilder.focusRequest) {
                const world::HeroPoint& hero = *panel_->worldBuilder.focusRequest;
                ensureFreeCamera();
                // Framed on the hero's own bounding sphere at the field of view actually in use, so
                // a two-metre artefact and a thirty-metre tree each fill the frame rather than each
                // getting the same arbitrary stand-off.
                const float radius = std::max(hero.radius, hero.height * 0.5f);
                const glm::vec3 centre(hero.position.x, hero.position.y + hero.height * 0.4f,
                                       hero.position.z);
                setViewportPose(frameSphere(viewportPose(), centre, radius,
                                            engine_->scene().camera.effectiveFovY()));
                log::info("viewport: framed hero '{}'", hero.name);
                panel_->worldBuilder.focusRequest.reset();
            }
        }
        runViewportProbe(framesRendered);
        // After the frame is submitted, so the identifier and depth targets hold what the user
        // actually clicked on rather than the frame before it.
        {
            core::PhaseProfiler::Scope scope(prof, kPhPick);
            serviceViewportPick();
        }
        const auto workEnd = std::chrono::steady_clock::now();
        const auto presentStart = workEnd;
        context_->present();
        if (newestInputNs_ != 0) {
            // How old the input is by the time the frame carrying it is handed to the compositor.
            // Not the whole of what a person perceives -- scanout and the compositor's own queue are
            // past this point -- but it is the part the application controls, and the part that
            // moves when the loop is reordered.
            const std::uint64_t nowNs = SDL_GetTicksNS();
            if (nowNs > newestInputNs_) {
                prof.add(kPhLatency, static_cast<double>(nowNs - newestInputNs_) / 1.0e6);
            }
        }
        prof.add(kPhPresent, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                       presentStart).count());
        const auto outputsStart = std::chrono::steady_clock::now();
        if (outputs_.openCount() > 0) {
            if (auto r = outputs_.presentAll(*context_, finalTexture_, renderWidth_, renderHeight_); !r) {
                log::warn("outputs: {}", r.error().message);
            }
        }
        if (share_.isOpen()) {
            if (auto r = share_.publish(finalTexture_, renderWidth_, renderHeight_); !r) {
                log::warn("share: {}", r.error().message);
            }
            if (fpsFrames % 30 == 0) {
                const auto st = share_.stats();
                panel_->shareStatus = fmt::format("{} '{}': {} frames{}{}", share::TextureShare::kindName(share_.kind()),
                                                  share_.name(), st.framesPublished,
                                                  st.clients >= 0 ? fmt::format(", {} client(s)", st.clients) : std::string(),
                                                  st.lastError.empty() ? std::string() : ", error: " + st.lastError);
            }
        }
        outputs_.pumpEvents(/*pumpQueue*/ false); // the primary window already pumped this frame
        prof.add(kPhOutputs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                       outputsStart).count());
        {
            core::PhaseProfiler::Scope scope(prof, kPhEvProc);
            context_->processEvents();
        }

        const auto frameEnd = std::chrono::steady_clock::now();
        // CPU work excludes the swapchain wait inside acquire and the present call.
        stats.cpuFrameMs = std::chrono::duration<double, std::milli>((workBeforeAcquire - frameStart) +
                                                                     (workEnd - workAfterAcquire)).count();
        stats.frameIntervalMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        prof.count(kPhAllocK,
                   static_cast<double>(core::allocCounters().allocations - allocsAtFrameStart) / 1000.0);
        prof.endFrame(stats.frameIntervalMs);
        ++fpsFrames;
        fpsAccum = std::chrono::duration<double>(frameEnd - fpsStart).count();
        if (fpsAccum >= 0.5) {
            stats.fps = fpsFrames / fpsAccum;
            fpsFrames = 0;
            fpsStart = frameEnd;
        }
        ++framesRendered;
        if (framesRendered == 60) {
            // The one number that decides what the world costs and that nobody quotes: the canvas is
            // a fraction of the window but at the display's backing scale, so an editor on a Retina
            // screen renders the world at several times the pixel count of a "1440x900" benchmark.
            // Logged once, so every run states what it actually rendered.
            log::info("canvas: world rendered at {}x{} px ({:.2f} Mpx) inside a {}x{} px window (scale {:.2f})",
                      renderWidth_, renderHeight_,
                      static_cast<double>(renderWidth_) * renderHeight_ / 1.0e6, window_->pixelWidth(),
                      window_->pixelHeight(), window_->pixelScale());
            const EngineStats& es = engine_->stats();
            log::info("engine.update allocations: control={} signals={} modulation={} controller={} other={}",
                      es.allocsControl, es.allocsSignals, es.allocsModulation, es.allocsController, es.allocsOther);
        }
        if (framesRendered % 120 == 0) {
            const auto& f = engine_->latestFrame();
            log::debug("frame {} t={:.2f}s fps={:.1f} cpu={:.2f}ms gpu={:.2f}ms bass={:.2f} mid={:.2f} treble={:.2f} scale={:.2f}",
                       framesRendered, time.renderTime, stats.fps, stats.cpuFrameMs, stats.gpuFrameMs,
                       f.bands[0], f.bands[2], f.bands[4],
                       engine_->params().ordered().empty() ? 0.0f : engine_->params().ordered().front()->finalComponent(0));
        }
        if (options_.frames >= 0 && framesRendered >= options_.frames) {
            break;
        }
        if (context_->deviceLost()) {
            log::error("GPU device lost; exiting");
            return 3;
        }
    }

    if (options_.capture) {
        if (auto r = captureFrame(lastTime, *options_.capture); !r) {
            log::error("capture: {}", r.error().message);
            return 4;
        }
    }
    // The throttle means the last few seconds of toggling may not have reached the disk yet.
    // ImGui saves its own ini from DestroyContext, so the two halves of the layout land together.
    if (panel_) {
        panel_->saveLayout();
    }
    if (options_.profileCpu) {
        // stderr rather than the log, so the table is not interleaved with timestamps and levels
        // and can be pasted into a document as it stands.
        std::fputs(cpuProfile_
                       .report(fmt::format("main-thread CPU, live editor, ui-script '{}'",
                                           options_.uiScript.empty() ? "idle" : options_.uiScript))
                       .c_str(),
                   stderr);
    }
    if (options_.profileCsv) {
        std::ofstream out(*options_.profileCsv);
        out << cpuProfile_.csv();
        log::info("frame phases written to {}", options_.profileCsv->string());
    }
    // What the scripted editor run actually did, in order. This is the only record that survives a
    // run the machine cannot screenshot (ADR-092), so it is printed whether or not anything else is.
    for (const std::string& line : uiScript_.editLog()) {
        log::info("{}", line);
    }
    log::info("rendered {} frames; GPU errors: {}", framesRendered, context_->errorCount());
    return context_->errorCount() == 0 ? 0 : 5;
}

RenderSettings Application::renderSettingsFromOptions() const {
    RenderSettings s = engine_->renderSettings();
    if (options_.render) {
        s.outputPath = *options_.render;
        s.output = RenderSettings::outputForPath(*options_.render);
    }
    if (options_.renderOutput) {
        s.output = *options_.renderOutput;
    }
    s.normalisePattern();
    // ADR-182: the diagnostic arms reach the offline renderer too. Without this `--disable water`
    // on a `--render` produced a byte-identical sequence -- an attribution arm that cannot fail.
    s.disablePasses = options_.disablePasses;
    s.qualityArms = options_.qualityArms;
    // `--tier` used to reach the interactive renderer and stop there, so `--render out --tier
    // realtime` -- the fast proof everyone wants before committing an hour to a sequence -- still
    // rendered at the offline tier. Exactly ADR-147's defect one flag over.
    if (!options_.qualityTier.empty()) {
        s.tier = options_.qualityTier;
    }
    if (!options_.renderLimits.empty()) {
        s.limits = options_.renderLimits; // ADR-186; validated with the rest of the settings
    }
    if (options_.renderWidth) s.width = *options_.renderWidth;
    if (options_.renderHeight) s.height = *options_.renderHeight;
    if (options_.offlineFps > 0.0 && options_.fpsGiven) s.fps = options_.offlineFps;
    if (options_.rangeStart) s.startSeconds = *options_.rangeStart;
    if (options_.rangeEnd) s.endSeconds = *options_.rangeEnd;
    if (options_.codec) s.codec = *options_.codec;
    if (options_.quality) s.quality = *options_.quality;
    if (options_.supersample > 1.0f) s.supersample = options_.supersample;
    return s;
}

Result<void> Application::openAny(const std::filesystem::path& path) {
    // A history that describes a world that is gone is worse than no history: pressing undo would
    // try to restore nodes into a scene that never had them, and the labels would describe edits to
    // somebody else's project (ADR-092). The nodes the commands were holding go with it.
    if (panel_ != nullptr) {
        panel_->editor.reset();
    }
    if (world::isRecipeFile(path)) {
        return generateWorldFromRecipe(path);
    }
    return engine_->loadFile(path);
}

Result<void> Application::generateWorldFromRecipe(const std::filesystem::path& path) {
    auto world = composeFromRecipeFile(path);
    if (!world) {
        return std::unexpected(world.error());
    }
    // A recipe with no composition to land in gets one, so `--generate` alone is a complete
    // instruction rather than something that only works after a scene is already open.
    if (engine_->composition() == nullptr) {
        engine_->newComposition();
    }
    if (auto installed = installWorld(*engine_, *world); !installed) {
        return installed;
    }
    log::info("generate: '{}' from {} asset(s) -> {} layer(s)", world->recipe.world,
              world->assetsConsidered, world->composed.layers.size());
    return {};
}

Result<std::unique_ptr<RenderJob>> Application::makeRenderJob(const std::filesystem::path& projectFile,
                                                              RenderSettings settings) {
    auto offline = std::make_unique<Engine>(EngineMode::Offline);
    if (auto r = offline->loadProject(projectFile); !r) {
        return std::unexpected(r.error());
    }
    for (const auto& w : offline->projectWarnings()) {
        log::warn("render project: {}", w);
    }
    auto job = std::make_unique<RenderJob>(*context_, *shaders_, std::move(offline), std::move(settings),
                                           std::filesystem::absolute(projectFile).parent_path());
    if (auto r = job->start(); !r) {
        return std::unexpected(r.error());
    }
    return job;
}

int Application::runQueue(const std::filesystem::path& queueFile) {
    std::ifstream in(queueFile);
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object() || doc.value("format", std::string()) != "avgen-render-queue" || !doc.contains("jobs") ||
        !doc["jobs"].is_array()) {
        log::error("'{}' is not a render queue (format 'avgen-render-queue' with a 'jobs' array)", queueFile.string());
        return 2;
    }
    const auto dir = std::filesystem::absolute(queueFile).parent_path();
    int failures = 0;
    int index = 0;
    for (const auto& jobJson : doc["jobs"]) {
        ++index;
        if (!jobJson.is_object() || !jobJson.contains("project")) {
            log::error("queue job {}: needs a 'project'", index);
            ++failures;
            continue;
        }
        std::filesystem::path project = jobJson["project"].get<std::string>();
        if (project.is_relative()) {
            project = dir / project;
        }
        // Settings: the project's, overridden by the job's "render" block, then by CLI flags.
        RenderSettings settings;
        {
            std::ifstream pin(project);
            nlohmann::json pdoc = nlohmann::json::parse(pin, nullptr, false);
            if (pdoc.is_object() && pdoc.contains("render")) {
                if (auto s = RenderSettings::fromJson(pdoc["render"])) settings = *s;
            }
        }
        if (jobJson.contains("render")) {
            auto merged = settings.toJson();
            merged.update(jobJson["render"]);
            auto s = RenderSettings::fromJson(merged);
            if (!s) {
                log::error("queue job {}: {}", index, s.error().message);
                ++failures;
                continue;
            }
            settings = *s;
        }
        if (options_.codec) settings.codec = *options_.codec;
        if (options_.quality) settings.quality = *options_.quality;
        if (options_.supersample > 1.0f) settings.supersample = options_.supersample;
        if (options_.renderOutput) settings.output = *options_.renderOutput;
        settings.normalisePattern();
        if (settings.outputPath.empty()) {
            settings.outputPath = project.stem().string() + "_frames";
        }
        log::info("queue job {}/{}: {} -> {}", index, doc["jobs"].size(), project.filename().string(),
                  settings.outputPath.string());
        auto job = makeRenderJob(project, settings);
        if (!job) {
            log::error("queue job {}: {}", index, job.error().message);
            ++failures;
            continue;
        }
        if (auto r = (*job)->run(); !r) {
            log::error("queue job {}: {}", index, r.error().message);
            ++failures;
        }
    }
    log::info("queue complete: {} job(s), {} failure(s)", index, failures);
    return failures == 0 ? 0 : 7;
}

int Application::runHeadless() {
    if (options_.queue) {
        return runQueue(*options_.queue);
    }
    if (options_.render) {
        // The offline engine builds its own renderer, so a debug view selected on the command line
        // never reaches it and the sequence comes out as the ordinary shaded frame. That is
        // defensible for a deliverable and indefensible in silence: a flag that is accepted,
        // validated, and then ignored is how somebody spends an afternoon studying a debug view
        // that was never drawn. (This warning lives here rather than beside the flag's own parsing
        // because that code runs only on the interactive path -- putting it there made it dead code
        // for precisely the case it is about.)
        if (!options_.debugTarget.empty()) {
            log::warn("--debug-target '{}' does not apply to --render: an offline sequence is drawn "
                      "by its own renderer and comes out shaded. Use --capture for a debug view.",
                      options_.debugTarget);
        }
        // The offline engine loads the project itself; without a project file, snapshot the
        // current session into a temporary one.
        std::filesystem::path projectFile = engine_->projectPath();
        if (projectFile.empty() || options_.audio || options_.scene || options_.composition || options_.environment ||
            !options_.shaders.empty()) {
            projectFile = std::filesystem::temp_directory_path() / "avgen_render_session.json";
            if (auto r = engine_->saveProject(projectFile); !r) {
                log::error("render: {}", r.error().message);
                return 2;
            }
        }
        auto job = makeRenderJob(projectFile, renderSettingsFromOptions());
        if (!job) {
            log::error("render: {}", job.error().message);
            return 2;
        }
        if (auto r = (*job)->run(); !r) {
            log::error("render: {}", r.error().message);
            return context_->errorCount() == 0 ? 7 : 5;
        }
        return 0;
    }
    const int frames = options_.frames > 0 ? options_.frames : 120;
    FixedStepClock clock(options_.offlineFps);
    // `--size` in points, as the window takes it. Headless ignored it and rendered 1280x720
    // whatever was asked for, which silently invalidated every headless measurement that varied
    // resolution: three sizes, one frame size, and a confident conclusion that the frame was not
    // fragment-bound. A benchmark that cannot set its own resolution is not a benchmark.
    const std::uint32_t w = std::max(options_.width, 16u);
    const std::uint32_t h = std::max(options_.height, 16u);
    if (auto r = renderer_->resize(w, h); !r) {
        log::error("resize: {}", r.error().message);
        return 2;
    }
    renderer_->setClusterStatsEnabled(options_.clusterStats);

    // ---- the run's schedule (ADR-113) ----------------------------------------------------------
    //
    // One block is one arm, measured once. An ordinary run has a single block; an A/B has
    // 2 * `--ab-blocks` of them, alternating, so that a machine which drifts during the session
    // charges the drift to both arms instead of to the change. Both arms are in *this process* and
    // this session, because that is the only comparison the audit found to be valid: two recorded
    // Glowmere figures differ by 28% with nothing to explain it, so a number from another run --
    // however carefully taken -- is not a baseline.
    struct BenchBlock {
        std::string arm;
        rendering::SceneRenderer::PassToggles toggles;
        // ADR-117: an arm may change a quality *setting* rather than remove a pass. Carried per
        // block and re-applied at the top of every block, because a setting the renderer keeps is
        // state and the SYM-TERRAIN-1 investigation produced four wrong attributions from a probe
        // that measured state it had established only once.
        rendering::QualitySettings quality;
        bool baseline = false;
    };
    const rendering::SceneRenderer::PassToggles baseToggles = renderer_->passToggles();
    const rendering::QualitySettings baseQuality = renderer_->qualitySettings();
    std::vector<BenchBlock> schedule;
    if (!options_.abArm.empty()) {
        rendering::SceneRenderer::PassToggles armToggles = baseToggles;
        rendering::QualitySettings armQuality = baseQuality;
        // `--ab none` is the null A/B: both arms are the baseline, so the difference it reports is
        // the harness measuring itself. It is the only honest way to state this mode's noise floor
        // -- the 2%/4% constants were calibrated from five separate *runs*, and a within-process
        // interleaved block is a different measurement with a different floor. A null A/B that
        // reports "A RESULT" is a broken harness, whatever it says about any real arm.
        const bool isQualityArm = rendering::SceneRenderer::setQualityArm(armQuality, options_.abArm);
        if (options_.abArm != "none" && !isQualityArm &&
            !rendering::SceneRenderer::setPassArm(armToggles, options_.abArm, false)) {
            log::error("--ab: unknown phase '{}' (one of: none,{},{})", options_.abArm,
                       rendering::SceneRenderer::passArmNames(),
                       rendering::SceneRenderer::qualityArmNames());
            return 2;
        }
        const std::string armLabel = isQualityArm ? options_.abArm : "no-" + options_.abArm;
        // Counterbalanced: the arm order alternates between blocks (ADR-181).
        //
        // Interleaving alone is not enough, and this harness had the defect it exists to prevent.
        // Running baseline-then-arm in every block means the arm *always* pays for whatever the
        // machine did during that block -- thermal drift, a background process waking, the GPU
        // clocking down -- so drift enters the delta as a **bias** rather than as noise, and
        // averaging more blocks converges on the wrong answer instead of the right one. It is not
        // hypothetical: a fixed-order interleaved measurement of six mushrooms reported that
        // *hiding* them made the frame 2.3 ms slower.
        //
        // Alternating means each arm runs first as often as it runs second, so first-position and
        // second-position effects cancel in the mean instead of accumulating in one arm.
        for (int b = 0; b < options_.abBlocks; ++b) {
            const BenchBlock base{"baseline", baseToggles, baseQuality, true};
            const BenchBlock armed{armLabel, armToggles, armQuality, false};
            if ((b % 2) == 0) {
                schedule.push_back(base);
                schedule.push_back(armed);
            } else {
                schedule.push_back(armed);
                schedule.push_back(base);
            }
        }
        if (isQualityArm) {
            log::info("A/B: {} pair(s) of baseline vs quality arm '{}' -- {} -- interleaved, {} frames "
                      "each; a difference below {:.0f}% GPU or {:.0f}% wall is not a result",
                      options_.abBlocks, options_.abArm,
                      rendering::SceneRenderer::qualityArmDescription(options_.abArm), frames,
                      rendering::kGpuNoiseFloorPercent, rendering::kWallNoiseFloorPercent);
        } else {
            log::info("A/B: {} pair(s) of baseline vs '{}' disabled, interleaved, {} frames each; "
                      "a difference below {:.0f}% GPU or {:.0f}% wall is not a result",
                      options_.abBlocks, options_.abArm, frames, rendering::kGpuNoiseFloorPercent,
                      rendering::kWallNoiseFloorPercent);
        }
    } else {
        schedule.push_back({options_.disablePasses.empty() ? "baseline" : "disabled:" + options_.disablePasses,
                            baseToggles, baseQuality, true});
    }

    // The conditions every record in this run shares. One session id per process is what makes the
    // "same session or no comparison" rule checkable by a reader of the file rather than a
    // convention somebody has to remember.
    rendering::BenchmarkConditions shared;
    shared.scene = options_.composition  ? options_.composition->string()
                   : options_.project    ? options_.project->string()
                   : options_.scene      ? options_.scene->string()
                   : options_.example    ? *options_.example
                                         : "";
    shared.sceneKind = options_.composition ? "composition" : options_.project ? "project" : "scene";
    shared.width = w;
    shared.height = h;
    shared.qualityTier = options_.qualityTier.empty() ? "default" : options_.qualityTier;
    shared.gitRevision = AVGEN_GIT_REVISION;
    shared.gitDirty = AVGEN_GIT_DIRTY != 0;
    shared.buildType = AVGEN_BUILD_TYPE;
    shared.backend = context_->capabilities().backendName;
    // The GPU the numbers were taken on. Two adapters are two machines as far as a frame time is
    // concerned, whatever else the conditions say.
    shared.platform = context_->capabilities().adapterName;
    shared.offlineFps = options_.offlineFps;
    shared.clusterStats = options_.clusterStats;
    {
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
        ::localtime_r(&now, &local);
        char stamp[32] = {};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &local);
        shared.startedAt = stamp;
        // Process-unique without needing a UUID library: the start second plus the pid. Two runs
        // cannot collide; the blocks of one run cannot differ, which is what the rule needs.
        shared.sessionId = fmt::format("{}-{}", stamp, static_cast<long long>(::getpid()));
    }

    std::vector<rendering::BenchmarkRecord> records;
    std::vector<rendering::AbBlock> baselineBlocks;
    std::vector<rendering::AbBlock> armBlocks;
    FrameTime time{};
    std::uint64_t lastHash = 0;
    for (std::size_t blockIndex = 0; blockIndex < schedule.size(); ++blockIndex) {
        const BenchBlock& block = schedule[blockIndex];
        renderer_->setPassToggles(block.toggles);
        // Re-established per block, not once: an arm that measures state has to put the state back
        // before every measurement or it is measuring whatever the previous block left behind.
        renderer_->setQualitySettings(block.quality);
        // Every block renders the same frame range from the same start, or the arms are not being
        // compared on the same work: a scene whose second 2 differs from its second 0 would put
        // the difference between two blocks into the difference between two arms.
        clock.restartAt(0.0);
        renderer_->resetTemporalHistory();
        if (schedule.size() > 1) {
            log::info("--- block {}/{}: arm '{}' ---", blockIndex + 1, schedule.size(), block.arm);
        }
        // Per-frame wall clock, reported as a median at the end. The benchmark used to be the whole
        // process under /usr/bin/time divided by the frame count, which charges the scene build to
        // the frames: eleven scatter layers take two seconds longer to load than none, and over a
        // hundred frames that is twenty milliseconds a frame of glTF decode masquerading as draw
        // cost. A median also ignores the handful of frames a concurrent build steals, which an
        // average cannot.
        std::vector<double> frameMs;
        frameMs.reserve(static_cast<std::size_t>(frames));
        // Per-pass GPU time, per frame, keyed by the timeline's label. Reported as a median at the
        // end next to the frame median, because one frame's sample of a 0.066 ms-resolution counter
        // says very little and a hundred of them say what the pass costs.
        std::vector<std::pair<std::string, std::vector<double>>> passMs;
        std::vector<double> gpuFrameMs;
        gpuFrameMs.reserve(static_cast<std::size_t>(frames));
        // The workload each frame was given, kept per frame so the record's counters can be medians
        // over the same window the timings came from rather than one frame's snapshot of a number
        // that moves (an indirect draw's instance count lands a frame or three late).
        std::vector<rendering::RenderStats> frameStats;
        frameStats.reserve(static_cast<std::size_t>(frames));
        for (int i = 0; i < frames; ++i) {
            const auto frameStart = std::chrono::steady_clock::now();
            time = engine_->tick(clock);
            const std::uint64_t discontinuity = engine_->transport().discontinuityRevision();
            if (discontinuity != lastTransportDiscontinuity_) {
                renderer_->resetTemporalHistory();
                lastTransportDiscontinuity_ = discontinuity;
            }
            // The scene rebuild is CPU work that scales with the size of the world rather than with
            // what is on screen, and nothing measured it: a world scene with the camera turned to
            // face empty sky spends 12-14 ms on the GPU and 21 ms of wall clock, and the difference
            // was invisible. See docs/performance.md.
            const auto updateStart = std::chrono::steady_clock::now();
            engine_->setViewport(w, h);
            engine_->update(time);
            lastEngineUpdateMs_ =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();
            // The AI control plane's queue, drained here for the same reason the live loop drains it
            // (ADR-094): this is the thread that owns engine state, so it is the thread tool bodies
            // run on. A headless run that never pumped would leave a task submitted during the loop
            // waiting for a service that never came -- which is the exact shape of the bug this
            // repository keeps shipping, so it is drained even though nothing in the offline path
            // submits one today.
            if (ai_) {
                ai_->pump();
            }
            // Debug drawing (ADR-031): build this frame's inspection geometry from the World window's
            // options; an empty set costs nothing.
            if (panel_) {
                const rendering::DebugViewOptions& options = panel_->world.debug;
                renderer_->setDebugDepthTest(options.depthTest);
                rendering::buildDebugGeometry(renderer_->debugDraw(), engine_->scene(), options, time.renderTime);
            }
            const rendering::ShaderFrameInputs shaderInputs{&engine_->shaderLayers(),
                                                            engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
            // Pixels are only pulled back on the frames something reads them: the captured frame, or
            // every frame when debug logging wants a determinism hash. The rest render and submit and
            // stop there, as the live path does.
            const bool wantPixels = options_.logLevel <= log::Level::Debug ||
                                    (options_.capture && i == frames - 1);
            std::optional<gpu::Image8> image;
            if (wantPixels) {
                auto rendered = renderer_->renderToImage(engine_->scene(), time, w, h, &shaderInputs);
                if (!rendered) {
                    log::error("render: {}", rendered.error().message);
                    return 2;
                }
                image = std::move(*rendered);
            } else if (auto r = renderer_->renderFrame(engine_->scene(), time, w, h, &shaderInputs); !r) {
                log::error("render: {}", r.error().message);
                return 2;
            }
            // The determinism hash is a full scan of the frame and scales with its area: at 2880x1800
            // it was a fifth of the wall time of every headless run, which is a fifth of every
            // performance measurement taken with one. It exists to diff two runs frame by frame, so it
            // is computed when someone is actually looking -- debug logging, or the captured frame.
            if (image) {
                lastHash = gpu::hashImage(*image);
                log::debug("offline frame {:4d} hash={:016x}", i, lastHash);
            }
            frameMs.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
            {
                // Sum this frame's passes by label first: several passes share one (two cascades, a
                // bloom pyramid), and it is the phase's total that a workload A/B moves.
                std::vector<std::pair<std::string, double>> frameLabels;
                for (const auto& entry : renderer_->timeline().passes()) {
                    auto it = std::find_if(frameLabels.begin(), frameLabels.end(),
                                           [&](const auto& e) { return e.first == entry.label; });
                    if (it == frameLabels.end()) {
                        frameLabels.emplace_back(entry.label, entry.ms);
                    } else {
                        it->second += entry.ms;
                    }
                }
                for (const auto& [label, ms] : frameLabels) {
                    auto it = std::find_if(passMs.begin(), passMs.end(),
                                           [&](const auto& e) { return e.first == label; });
                    if (it == passMs.end()) {
                        passMs.emplace_back(label, std::vector<double>{ms});
                    } else {
                        it->second.push_back(ms);
                    }
                }
                if (renderer_->stats().gpuFrameMs >= 0.0) {
                    gpuFrameMs.push_back(renderer_->stats().gpuFrameMs);
                }
                // The whole counter set, per frame. Copied rather than summarised here so the
                // record's medians are taken over exactly the window its timings are.
                frameStats.push_back(renderer_->stats());
            }
            if (i % 30 == 0 || i == frames - 1) {
                const auto& f = engine_->latestFrame();
                // The headline parameters differ per scene kind; a missing one reads as 0.
                auto valueOf = [&](const char* a, const char* b) {
                    if (const auto* p = engine_->params().find(a)) return p->finalComponent(0);
                    if (const auto* p = engine_->params().find(b)) return p->finalComponent(0);
                    return 0.0f;
                };
                log::info("offline frame {:4d} t={:7.3f}s bass={:.2f} mid={:.2f} treble={:.2f} rms={:.2f} onset={} scale={:.3f} "
                          "emissive={:.2f} gpu={:.2f}ms hash={:016x}",
                          i, time.renderTime, f.bands[0], f.bands[2], f.bands[4], f.rms, f.onset ? 1 : 0,
                          valueOf("orb/scale", "root/scale"), valueOf("orb/emissive", "material/emissiveBoost"),
                          renderer_->stats().gpuFrameMs, lastHash);
                // Where the frame went. Every pass in the frame marks one GPU timestamp on a single
                // timeline at its end (gpu/frame_timeline.hpp), so what is printed here is the
                // interval from the previous pass's end to this one's: the passes partition the
                // frame and sum to it. The old per-pass begin/end pairs did not -- a pass behind a
                // heavy one absorbed the drain of everything still in flight, and the volumetric
                // pass reported 39.4 ms of a 46 ms frame for 5.4 ms of work.
                const auto& st = renderer_->stats();
                {
                    // Passes in submission order, summed per label, printed largest first.
                    std::vector<std::pair<std::string, double>> byLabel;
                    for (const auto& entry : renderer_->timeline().passes()) {
                        auto it = std::find_if(byLabel.begin(), byLabel.end(),
                                               [&](const auto& p) { return p.first == entry.label; });
                        if (it == byLabel.end()) {
                            byLabel.emplace_back(entry.label, entry.ms);
                        } else {
                            it->second += entry.ms;
                        }
                    }
                    std::stable_sort(byLabel.begin(), byLabel.end(),
                                     [](const auto& a, const auto& b) { return a.second > b.second; });
                    std::string breakdown;
                    double sum = 0.0;
                    for (const auto& [label, ms] : byLabel) {
                        breakdown += fmt::format(" {}={:.2f}", label, ms);
                        sum += ms;
                    }
                    log::info("             gpu {:.2f} ms over {} passes (sum {:.2f}):{}", st.gpuFrameMs,
                              st.gpuPasses, sum, breakdown);
                    // The same passes in submission order, for when the question is which pass a
                    // bubble landed on rather than which phase is expensive.
                    std::string ordered;
                    for (const auto& entry : renderer_->timeline().passes()) {
                        ordered += fmt::format(" {}={:.3f}", entry.label, entry.ms);
                    }
                    log::debug("             in order:{}", ordered);
                    if (renderer_->timeline().unwritten() > 0) {
                        log::debug("             {} pass timestamp(s) the driver did not write (an empty "
                                   "render pass); their cost folds into the pass after them",
                                   renderer_->timeline().unwritten());
                    }
                }
                // `instances=Av/Bc/T` carries its own total, because A + B is *not* the world: it covers
                // only the objects whose cull pass ran, and it counts the renderer's instance records
                // rather than the composer's per-layer placement log, which excludes terrain chunks and
                // hero nodes. Without the denominator the two invite being compared, and a benchmark
                // pass duly reported "101,822 culled against 99,515 placed" as a defect when both
                // numbers were right and measuring different things.
                log::info("             draws={} (indirect {}, empty {}, skipped {}) shadowDraws={} "
                          "cascades={}/{} spots={} dispatches={} tris={} instances={}v/{}c/{} lod={}/{}/{}/{} "
                          "particles={}sys/{}cap/{}emit cpu(proc)={:.2f}ms cpu(scene)={:.2f}ms",
                          st.drawCalls, st.indirectDraws, st.emptyDraws, st.skippedDraws, st.shadowDraws,
                          st.shadows.cascades, st.shadows.views, st.shadows.spots, st.computeDispatches,
                          st.triangles, st.visibleInstances, st.culledInstances,
                          st.visibleInstances + st.culledInstances, st.lodCounts[0], st.lodCounts[1],
                          st.lodCounts[2], st.lodCounts[3], st.particles.systems, st.particles.capacity,
                          st.particles.emittedThisFrame, st.procedural.cpuUpdateMs, lastEngineUpdateMs_);
                // The workload each measured phase was actually given. Without these an A/B that edits
                // a scene cannot prove its two arms differ, and "no effect" reads exactly like a run
                // whose edit never applied.
                log::info("             workload: volumeSteps={} volumeTarget={}x{} cascades={} shadowRes={} aoTarget={}x{} "
                          "aoSlices={}x{} shadowMask={}x{}/{}L postPasses={} bloomLevels={} "
                          "sdf={}ray/{}mesh simGrids={} "
                          "transient={} wind={}obj plants={}/{}awake ({} examined, {} slot writes)",
                          st.volume.steps, st.volume.marchWidth, st.volume.marchHeight, st.shadows.cascades, st.shadows.resolution, st.ao.width,
                          st.ao.height, st.ao.slices, st.ao.steps, st.shadowMask.width, st.shadowMask.height,
                          st.shadowMask.lights, st.post.passes, st.post.bloomLevels,
                          st.sdf.raymarchObjects, st.sdf.meshObjects, st.simulation.grids,
                          st.transientTextures, st.procedural.windObjects, st.procedural.simActive,
                          st.procedural.simAwake, st.procedural.simExamined, st.procedural.simSlotWrites);
            }
            if (options_.capture && i == frames - 1 && image) {
                if (auto r = writeCapture(*image, *options_.capture); !r) {
                    log::error("capture: {}", r.error().message);
                    return 4;
                }
                log::info("captured frame {} to {}", i, options_.capture->string());
            }
        }
        // The first frames build pipelines, meshes and shadow maps, and the empty-LOD suppression
        // has not settled; they are not what a steady frame costs. Drop them when there are enough
        // left. An A/B block gets the same treatment: switching an arm invalidates pipelines too.
        const std::size_t warmup = frameMs.size() > 24 ? 12 : 0;
        const auto steadyOf = [&](const std::vector<double>& v) {
            return v.size() > warmup
                       ? std::vector<double>(v.begin() + static_cast<std::ptrdiff_t>(warmup), v.end())
                       : std::vector<double>{};
        };
        rendering::BenchmarkRecord record;
        record.conditions = shared;
        record.conditions.arm = block.arm;
        record.conditions.framesRendered = static_cast<int>(frameMs.size());
        record.conditions.warmupFrames = static_cast<int>(warmup);
        record.conditions.measuredFrames = static_cast<int>(steadyOf(frameMs).size());
        {
            const scene::Camera& camera = engine_->scene().camera;
            record.conditions.camera = camera.name;
            for (int c = 0; c < 3; ++c) {
                record.conditions.cameraPosition[c] = camera.position[c];
                record.conditions.cameraTarget[c] = camera.target[c];
            }
            record.conditions.fovYDegrees = camera.effectiveFovY() * 180.0 / std::numbers::pi;
        }
        record.wallMs = rendering::describe(steadyOf(frameMs));
        record.gpuMs = rendering::describe(steadyOf(gpuFrameMs));
        {
            // The CPU frame, distributed like the other two. Taken from the same per-frame stats
            // the counters come from, so its window is exactly theirs.
            std::vector<double> cpuMs;
            cpuMs.reserve(frameStats.size());
            for (const auto& fs : frameStats) {
                cpuMs.push_back(fs.cpu.totalMs);
            }
            record.cpuMs = rendering::describe(steadyOf(cpuMs));
        }
        // The distribution, in full. The harness used to report median/p10/p90/min, which cannot
        // see a stutter at all: a hundred 10 ms frames with one 100 ms frame among them has a p99
        // of 10 ms. The 1% low is the mean of the slowest 1% of frames and is a *different number*
        // from p99 -- see rendering/render_stats.hpp, where the two definitions are written down.
        if (record.wallMs.valid()) {
            const rendering::Distribution& d = record.wallMs;
            log::info("frame wall clock over {} steady frames: median {:.2f} ms  p10 {:.2f}  p90 {:.2f}  min {:.2f}",
                      d.count, d.p50, d.p10, d.p90, d.min);
            log::info("             wall p95 {:.2f}  p99 {:.2f}  max {:.2f}  1%low {:.2f}  0.1%low {:.2f}  "
                      "var {:.3f} ms^2 (sd {:.2f})",
                      d.p95, d.p99, d.max, d.low1Percent, d.low01Percent, d.variance, d.stddev);
        }
        if (record.cpuMs.valid()) {
            const rendering::Distribution& d = record.cpuMs;
            log::info("             cpu  p50 {:.2f}  p90 {:.2f}  p99 {:.2f}  max {:.2f}  1%low {:.2f}",
                      d.p50, d.p90, d.p99, d.max, d.low1Percent);
        }
        if (record.gpuMs.valid()) {
            const rendering::Distribution& d = record.gpuMs;
            log::info("             gpu  p50 {:.2f}  p90 {:.2f}  p95 {:.2f}  p99 {:.2f}  max {:.2f}  "
                      "1%low {:.2f}  0.1%low {:.2f}  var {:.3f} ms^2",
                      d.p50, d.p90, d.p95, d.p99, d.max, d.low1Percent, d.low01Percent, d.variance);
        }
        // The GPU frame's passes, as medians over the same steady window. The passes partition the
        // frame (each is the interval between two consecutive pass ends on one timeline), so the
        // medians very nearly sum to the frame median and a phase's number responds to its own
        // workload. Sorted by cost: the top line is what to attack.
        {
            for (const auto& [label, samples] : passMs) {
                const rendering::Distribution d = rendering::describe(steadyOf(samples));
                if (d.valid()) {
                    record.passMedianMs.push_back(gpu::TimelineInterval{label, d.p50});
                }
            }
            std::stable_sort(record.passMedianMs.begin(), record.passMedianMs.end(),
                             [](const auto& a, const auto& b) { return a.ms > b.ms; });
            std::string breakdown;
            double sum = 0.0;
            for (const auto& pass : record.passMedianMs) {
                breakdown += fmt::format(" {}={:.2f}", pass.label, pass.ms);
                sum += pass.ms;
            }
            log::info("gpu frame median {:.2f} ms; pass medians (sum {:.2f}):{}",
                      record.gpuMs.valid() ? record.gpuMs.p50 : -1.0, sum, breakdown);
        }
        // The workload the timings were taken over, and the CPU stage split, both as medians over
        // the same window. `varied` says whether any counter moved: when it did not, these are
        // exact for every measured frame rather than a summary of several values.
        {
            const std::size_t first = std::min(warmup, frameStats.size());
            const auto medianOfStat = [&](auto pick) {
                std::vector<double> v;
                v.reserve(frameStats.size() - first);
                for (std::size_t f = first; f < frameStats.size(); ++f) {
                    v.push_back(static_cast<double>(pick(frameStats[f])));
                }
                if (v.empty()) {
                    return 0.0;
                }
                if (std::adjacent_find(v.begin(), v.end(), std::not_equal_to<>()) != v.end()) {
                    record.counters.varied = true;
                }
                std::sort(v.begin(), v.end());
                return v[v.size() / 2];
            };
            using RS = rendering::RenderStats;
            record.counters.draws = medianOfStat([](const RS& s) { return s.drawCalls; });
            record.counters.shadowDraws = medianOfStat([](const RS& s) { return s.shadowDraws; });
            record.counters.triangles = medianOfStat([](const RS& s) { return s.triangles; });
            record.counters.logicalTriangles =
                medianOfStat([](const RS& s) { return s.geometry.logicalTriangles; });
            record.counters.visibleInstances = medianOfStat([](const RS& s) { return s.visibleInstances; });
            record.counters.culledInstances = medianOfStat([](const RS& s) { return s.culledInstances; });
            for (int l = 0; l < 4; ++l) {
                record.counters.lod[l] = medianOfStat([l](const RS& s) { return s.lodCounts[l]; });
            }
            record.counters.shadowCasters = medianOfStat([](const RS& s) { return s.shadowCasters; });
            record.counters.lights = medianOfStat([](const RS& s) { return s.shadedLights; });
            record.counters.directionalLights = medianOfStat([](const RS& s) { return s.directionalLights; });
            record.counters.uniformPathLights = medianOfStat([](const RS& s) { return s.lights; });
            record.counters.clusteredLights = medianOfStat([](const RS& s) { return s.clusteredLights; });
            record.counters.particleSystems = medianOfStat([](const RS& s) { return s.particles.systems; });
            record.counters.particleCapacity = medianOfStat([](const RS& s) { return s.particles.capacity; });
            record.counters.particlesEmitted =
                medianOfStat([](const RS& s) { return s.particles.emittedThisFrame; });
            record.counters.transientTextures = medianOfStat([](const RS& s) { return s.transientTextures; });
            record.counters.entities = medianOfStat([](const RS& s) { return s.entities; });
            record.counters.computeDispatches = medianOfStat([](const RS& s) { return s.computeDispatches; });
            record.counters.gpuPasses = medianOfStat([](const RS& s) { return s.gpuPasses; });
            record.cpuMedian.uploadsMs = medianOfStat([](const RS& s) { return s.cpu.uploadsMs; });
            record.cpuMedian.lightsMs = medianOfStat([](const RS& s) { return s.cpu.lightsMs; });
            record.cpuMedian.objectsMs = medianOfStat([](const RS& s) { return s.cpu.objectsMs; });
            record.cpuMedian.fieldsMs = medianOfStat([](const RS& s) { return s.cpu.fieldsMs; });
            record.cpuMedian.simulationMs = medianOfStat([](const RS& s) { return s.cpu.simulationMs; });
            record.cpuMedian.particlesMs = medianOfStat([](const RS& s) { return s.cpu.particlesMs; });
            record.cpuMedian.proceduralMs = medianOfStat([](const RS& s) { return s.cpu.proceduralMs; });
            record.cpuMedian.sdfMs = medianOfStat([](const RS& s) { return s.cpu.sdfMs; });
            record.cpuMedian.shadowEncodeMs = medianOfStat([](const RS& s) { return s.cpu.shadowEncodeMs; });
            record.cpuMedian.backgroundEncodeMs =
                medianOfStat([](const RS& s) { return s.cpu.backgroundEncodeMs; });
            record.cpuMedian.depthEncodeMs = medianOfStat([](const RS& s) { return s.cpu.depthEncodeMs; });
            record.cpuMedian.sceneEncodeMs = medianOfStat([](const RS& s) { return s.cpu.sceneEncodeMs; });
            record.cpuMedian.volumeEncodeMs = medianOfStat([](const RS& s) { return s.cpu.volumeEncodeMs; });
            record.cpuMedian.postEncodeMs = medianOfStat([](const RS& s) { return s.cpu.postEncodeMs; });
            record.cpuMedian.tonemapEncodeMs = medianOfStat([](const RS& s) { return s.cpu.tonemapEncodeMs; });
            record.cpuMedian.finishMs = medianOfStat([](const RS& s) { return s.cpu.finishMs; });
            record.cpuMedian.submitMs = medianOfStat([](const RS& s) { return s.cpu.submitMs; });
            // The offline path blocks here waiting for the GPU, so this is usually most of the CPU
            // frame and is what stops anyone reading a 21 ms CPU frame as CPU-bound work.
            record.cpuMedian.queueWaitMs = medianOfStat([](const RS& s) { return s.cpu.queueWaitMs; });
            record.cpuMedian.totalMs = medianOfStat([](const RS& s) { return s.cpu.totalMs; });
            // Occupancy is a property of one camera position, so the last measured frame's grid is
            // reported rather than an average over a moving camera, which would describe no camera.
            if (!frameStats.empty() && frameStats.back().haveClusters) {
                record.clusters = frameStats.back().clusters;
                record.haveClusters = true;
                const rendering::ClusterOccupancy& o = record.clusters;
                log::info("cluster occupancy ({} froxels, {} local lights, cap {}): "
                          "min {} p50 {} p90 {} p99 {} max {} mean {:.2f}; empty {} ({:.1f}%), "
                          "overflowed {} ({:.2f}%), lights dropped by the cap {}",
                          o.clusters, o.lights, o.cap, o.min, o.p50, o.p90, o.p99, o.max, o.mean,
                          o.empty, o.clusters > 0 ? 100.0 * o.empty / o.clusters : 0.0, o.overflowed,
                          o.clusters > 0 ? 100.0 * o.overflowed / o.clusters : 0.0, o.dropped);
            }
        }
        rendering::AbBlock abBlock;
        abBlock.wallMs = record.wallMs;
        abBlock.gpuMs = record.gpuMs;
        (block.baseline ? baselineBlocks : armBlocks).push_back(abBlock);
        records.push_back(std::move(record));
    }
    // Restore whatever the run was configured with, so nothing after this point sees an A/B arm.
    renderer_->setPassToggles(baseToggles);

    // ---- the paired result (ADR-113) -----------------------------------------------------------
    rendering::AbSummary ab;
    if (!options_.abArm.empty()) {
        // The arm's own name, not "no-<name>": a quality arm is not a removal, and a summary line
        // that calls `shadowrange` "no-shadowrange" reads as the opposite of what was measured.
        ab = rendering::compareArms(schedule.size() > 1 ? schedule[1].arm : options_.abArm, baselineBlocks,
                                    armBlocks);
        const auto report = [&](const char* clock_, const rendering::PairedDelta& d,
                                const rendering::DriftCheck& drift) {
            // The drift check runs before the verdict, because a voided run has no verdict to
            // report (ADR-181). Counterbalancing removes drift's bias; only this detects its size.
            const bool voided = drift.voids(d.deltaMs);
            log::info("A/B {} : baseline {:.2f} ms, arm {:.2f} ms, delta {:+.2f} ms ({:+.2f}%); "
                      "noise floor {:.2f}% -> {}",
                      clock_, d.baselineMs, d.armMs, d.deltaMs, d.deltaPercent, d.noiseFloorPercent,
                      voided ? "VOID: the machine drifted further than the effect"
                      : d.isResult() ? (d.deltaMs > 0.0 ? "A RESULT: the arm is faster"
                                                        : "A RESULT: the arm is slower")
                                     : "NOT A RESULT: inside the noise");
            if (!drift.measurable()) {
                log::info("A/B {} drift: not checked -- {} baseline block(s); --ab-blocks 2 or more "
                          "is what makes the check possible",
                          clock_, drift.samples);
            } else {
                log::info("A/B {} drift: baseline {:.2f} ms in the first half of the run, {:.2f} ms "
                          "in the second ({:+.2f} ms, {:+.2f}%){}",
                          clock_, drift.firstHalfMs, drift.secondHalfMs, drift.driftMs,
                          drift.driftPercent,
                          voided ? " -- larger than the effect, so this run measured two machines "
                                   "rather than two arms"
                                 : " -- the machine held still");
            }
            // Which component set the floor, on the line where the verdict is read (ADR-148). A
            // difference rejected by the calibrated constant and one rejected because this
            // session's *arm* wobbled are different findings with different next steps, and the
            // floor alone cannot tell them apart.
            log::info("A/B {} : floor components -- calibrated {:.2f}%, baseline blocks {:.2f}%, "
                      "arm blocks {:.2f}%, per-pair deltas {:.2f}%",
                      clock_, d.calibratedFloorPercent, d.baselineSpreadPercent, d.armSpreadPercent,
                      d.deltaSpreadPercent);
        };
        if (ab.blocks == 0) {
            log::warn("A/B: no completed pair, so no comparison");
        } else {
            log::info("A/B over {} pair(s) of '{}': baseline blocks varied by {:.2f}% GPU / {:.2f}% wall",
                      ab.blocks, ab.arm, ab.gpuSpreadPercent, ab.wallSpreadPercent);
            report("gpu ", ab.gpu, ab.gpuDrift);
            report("wall", ab.wall, ab.wallDrift);
            std::string perBlock;
            for (std::size_t b = 0; b < ab.gpuBlockDeltaMs.size(); ++b) {
                perBlock += fmt::format(" pair{}={:+.2f}", b + 1, ab.gpuBlockDeltaMs[b]);
            }
            // Printed because a headline delta that two pairs disagree about is not one result, it
            // is two measurements of a machine that moved.
            log::info("A/B per-pair gpu delta ms:{}", perBlock);
        }
    }
    if (options_.benchJson) {
        const std::string json = rendering::benchmarkJson(records, ab.blocks > 0 ? &ab : nullptr);
        std::ofstream out(*options_.benchJson, std::ios::binary);
        if (!out) {
            log::error("--bench-json: cannot write {}", options_.benchJson->string());
            return 4;
        }
        out << json;
        log::info("benchmark record written to {} ({} block(s))", options_.benchJson->string(),
                  records.size());
    }
    log::info("headless run complete: {} block(s) of {} frames at {} fps; GPU errors: {}",
              schedule.size(), frames, options_.offlineFps, context_->errorCount());
    return context_->errorCount() == 0 ? 0 : 5;
}

} // namespace avgen::app
