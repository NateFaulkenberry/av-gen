#include "app/application.hpp"

#include "app/asset_browser.hpp"
#include "app/world_director.hpp"
#include "app/examples.hpp"
#include "assets/video_writer.hpp"

#include <nlohmann/json.hpp>

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

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstring>
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
           "  --save-project <f>  write the project on exit\n"
           "  --play              start playback immediately\n"
           "  --frames <n>        exit after n frames\n"
           "  --stress <seed>     apply random slider-like actions every frame (seek, params, routes, volume)\n"
           "  --capture <file>    write the last frame as a PPM image\n"
           "  --debug-target <t>  display an auxiliary render target: normal|roughness|velocity|\n"
           "                      emission|ids|occlusion|depth\n"
           "  --tier <t>          quality tier: preview|realtime|high|offline\n"
           "  --disable <list>    switch phases off for cost attribution: shadows,ao,volume,post\n"
           "  --headless          no window: offline mode, fixed-step clock, precomputed analysis\n"
           "  --fps <n>           offline frame rate (default 60)\n"
           "  --size <w>x<h>      window size in points (default 1440x900)\n"
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
        } else if (arg == "--disable") {
            auto v = need(i, "--disable");
            if (!v) return std::unexpected(v.error());
            options.disablePasses = *v;
            ++i;
        } else if (arg == "--stress") {
            auto v = need(i, "--stress");
            if (!v) return std::unexpected(v.error());
            options.stressSeed = static_cast<std::uint64_t>(std::atoll(v->c_str()));
            if (options.stressSeed == 0) return fail("--stress seed must be > 0");
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
    panel_.reset();
    imgui_.reset();
    engine_.reset();
    renderer_.reset();
    shaders_.reset();
    context_.reset();
    window_.reset();
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

    if (!options.headless) {
        platform::WindowDesc wdesc;
        wdesc.title = "avgen 0.2";
        wdesc.width = options.width;
        wdesc.height = options.height;
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
    if (!options_.qualityTier.empty()) {
        rendering::QualityTier tier = rendering::QualityTier::Realtime;
        if (!rendering::qualityTierFromName(options_.qualityTier, tier)) {
            return fail("unknown quality tier '{}' (preview|realtime|high|offline)", options_.qualityTier);
        }
        renderer_->setQuality(tier);
    }
    if (!options_.disablePasses.empty()) {
        rendering::SceneRenderer::PassToggles toggles;
        std::string off;
        std::string token;
        std::istringstream stream(options_.disablePasses);
        while (std::getline(stream, token, ',')) {
            if (token == "shadows") {
                toggles.shadows = false;
            } else if (token == "ao") {
                toggles.ao = false;
            } else if (token == "volume") {
                toggles.volume = false;
            } else if (token == "post") {
                toggles.post = false;
            } else if (!token.empty()) {
                return fail("--disable: unknown phase '{}' (shadows,ao,volume,post)", token);
            }
            if (!token.empty()) {
                off += off.empty() ? token : ", " + token;
            }
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
            rendering::AuxDebugView::Occlusion, rendering::AuxDebugView::Depth};
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
        auto imgui = ui::ImGuiLayer::create(*window_, *context_);
        if (!imgui) {
            return std::unexpected(imgui.error());
        }
        imgui_ = std::move(*imgui);
        panel_ = std::make_unique<ui::ControlPanel>();
        auto dialog = [this](platform::Window::DialogKind kind) {
            return [this, kind] {
                window_->openFileDialog(kind, [this](std::string path) {
                    if (!path.empty()) {
                        loadAny(path);
                    }
                });
            };
        };
        panel_->onOpenAudio = dialog(platform::Window::DialogKind::Audio);
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
            window_->saveFileDialog([this](std::string path) {
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
            window_->saveFileDialog([saveTo](std::string path) {
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
            window_->saveFileDialog([this](std::string path) {
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
        // ---- asset browser (ADR-031): the example directories plus the current project's folder
        const auto assetDirs = [executablePath]() { return exampleSearchDirs(executablePath); };
        panel_->onRescanAssets = [this, assetDirs] { panel_->assets = scanAssets(assetDirs()); };
        panel_->assets = scanAssets(assetDirs());
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
            window_->saveFileDialog([this](std::string path) {
                if (path.empty()) return;
                uiRender_.outputPath = path;
                uiRender_.output = RenderSettings::outputForPath(path);
            });
        };
    }

    if (options.example) {
        auto examples = loadExamples(exampleSearchDirs(executablePath));
        bool found = false;
        if (examples) {
            for (const auto& ex : *examples) {
                if (ex.name == *options.example) {
                    if (auto r = engine_->loadFile(ex.file); !r) {
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

void Application::loadAny(const std::filesystem::path& path) {
    auto r = engine_->loadFile(path);
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

void Application::storeOutputsToProject() { engine_->setOutputsJson(outputs_.toJson()); }

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
} // namespace

int Application::run() {
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

    for (;;) {
        const auto frameStart = std::chrono::steady_clock::now();
        auto events = window_->pollEvents([this](const SDL_Event& event) {
            if (uiSelfTestEvents_) {
                uiEventTypes_[event.type] += 1;
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                ++uiMotionEvents_;
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
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !imgui_->wantsKeyboard()) {
                if (event.key.key == SDLK_SPACE) {
                    engine_->togglePlay();
                } else if (event.key.key == SDLK_O && panel_ && panel_->onOpenAudio) {
                    panel_->onOpenAudio();
                } else if (event.key.key == SDLK_S && panel_ && panel_->onOpenScene) {
                    panel_->onOpenScene();
                } else if (event.key.key == SDLK_E && panel_ && panel_->onOpenEnvironment) {
                    panel_->onOpenEnvironment();
                } else if (event.key.key == SDLK_LEFT) {
                    engine_->seekSeconds(engine_->positionSeconds() - 5.0);
                } else if (event.key.key == SDLK_RIGHT) {
                    engine_->seekSeconds(engine_->positionSeconds() + 5.0);
                }
            }
        });
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
        if (events.resized) {
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
        const FrameTime time = engine_->tick(clock);
        lastTime = time;
        engine_->setViewport(window_->pixelWidth(), window_->pixelHeight());
        engine_->update(time);

        stats.width = window_->pixelWidth();
        stats.height = window_->pixelHeight();
        stats.drawCalls = renderer_->stats().drawCalls;
        stats.triangles = renderer_->stats().triangles;
        stats.procedural = renderer_->stats().procedural;
        stats.sdf = renderer_->stats().sdf;
        stats.particles = renderer_->stats().particles;
        stats.gpuFrameMs = renderer_->timeline().frameMs();

        // Background render: a few frames per UI frame, then the next queued job.
        if (job_) {
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
        imgui_->newFrame();
        panel_->draw(*engine_, stats);
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

        const auto workBeforeAcquire = std::chrono::steady_clock::now();
        auto view = context_->acquireSurfaceView();
        const auto workAfterAcquire = std::chrono::steady_clock::now();
        if (!view) {
            log::warn("frame skipped: {}", view.error().message);
            ImGui::EndFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        const std::uint32_t pw = window_->pixelWidth();
        const std::uint32_t ph = window_->pixelHeight();
        if (auto r = ensureFinalTexture(pw, ph); !r) {
            log::error("final texture: {}", r.error().message);
            return 2;
        }
        // The frame renders into the offscreen final texture; the main window and every output
        // present it through the output mapper (ADR-022).
        gpu::TargetView finalTarget{finalView_, wgpu::TextureFormat::BGRA8Unorm, pw, ph};
        gpu::TargetView target{*view, context_->surfaceFormat(), pw, ph};
        wgpu::CommandEncoder encoder = context_->device().CreateCommandEncoder();
        // Debug drawing (ADR-031): build this frame's inspection geometry from the World window's
        // options; an empty set costs nothing.
        if (panel_) {
            const rendering::DebugViewOptions& options = panel_->world.debug;
            renderer_->setDebugDepthTest(options.depthTest);
            rendering::buildDebugGeometry(renderer_->debugDraw(), engine_->scene(), options, time.renderTime);
        }
        const rendering::ShaderFrameInputs shaderInputs{&engine_->shaderLayers(),
                                                        engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
        if (auto r = renderer_->render(encoder, engine_->scene(), time, finalTarget, &shaderInputs); !r) {
            log::error("render: {}", r.error().message);
            return 2;
        }
        if (auto r = mapper_->draw(encoder, finalView_, *view, pw, ph, rendering::OutputMapping::identity(),
                                   context_->surfaceFormat()); !r) {
            log::error("present: {}", r.error().message);
            return 2;
        }
        imgui_->render(encoder, target);
        wgpu::CommandBuffer commands = encoder.Finish();
        context_->queue().Submit(1, &commands);
        renderer_->collectFrameTimings();
        const auto workEnd = std::chrono::steady_clock::now();
        context_->present();
        if (outputs_.openCount() > 0) {
            if (auto r = outputs_.presentAll(*context_, finalTexture_, pw, ph); !r) {
                log::warn("outputs: {}", r.error().message);
            }
        }
        if (share_.isOpen()) {
            if (auto r = share_.publish(finalTexture_, pw, ph); !r) {
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
        context_->processEvents();

        const auto frameEnd = std::chrono::steady_clock::now();
        // CPU work excludes the swapchain wait inside acquire and the present call.
        stats.cpuFrameMs = std::chrono::duration<double, std::milli>((workBeforeAcquire - frameStart) +
                                                                     (workEnd - workAfterAcquire)).count();
        stats.frameIntervalMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        ++fpsFrames;
        fpsAccum = std::chrono::duration<double>(frameEnd - fpsStart).count();
        if (fpsAccum >= 0.5) {
            stats.fps = fpsFrames / fpsAccum;
            fpsFrames = 0;
            fpsStart = frameEnd;
        }
        ++framesRendered;
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
    if (options_.renderWidth) s.width = *options_.renderWidth;
    if (options_.renderHeight) s.height = *options_.renderHeight;
    if (options_.offlineFps > 0.0 && options_.fpsGiven) s.fps = options_.offlineFps;
    if (options_.rangeStart) s.startSeconds = *options_.rangeStart;
    if (options_.rangeEnd) s.endSeconds = *options_.rangeEnd;
    if (options_.codec) s.codec = *options_.codec;
    if (options_.quality) s.quality = *options_.quality;
    return s;
}

Result<void> Application::generateWorldFromRecipe(const std::filesystem::path& path) {
    auto recipe = world::WorldRecipe::loadFile(path);
    if (!recipe) {
        return std::unexpected(recipe.error());
    }
    // A recipe may name its own library; otherwise the repository's manifest is the default, since
    // that is the one curated list of things allowed to be placed procedurally.
    auto libraryPath = recipe->assetLibrary;
    if (libraryPath.empty()) {
        libraryPath = path.parent_path() / ".." / ".." / "assets" / "manifest.json";
        libraryPath = libraryPath.lexically_normal();
    }
    auto library = assets::AssetLibrary::loadFile(libraryPath);
    if (!library) {
        return std::unexpected(library.error());
    }
    auto composed = world::composeWorld(*recipe, *library);
    if (!composed) {
        return std::unexpected(composed.error());
    }
    // A recipe with no composition to land in gets one, so `--generate` alone is a complete
    // instruction rather than something that only works after a scene is already open.
    if (engine_->composition() == nullptr) {
        engine_->newComposition();
    }
    GeneratedWorld world;
    world.recipe = *recipe;
    world.composed = std::move(*composed);
    world.assetsConsidered = library->size();
    if (auto installed = installWorld(*engine_, world); !installed) {
        return installed;
    }
    log::info("generate: '{}' from {} asset(s) -> {} layer(s)", world.recipe.world,
              world.assetsConsidered, world.composed.layers.size());
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
    FrameTime time{};
    std::uint64_t lastHash = 0;
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
    for (int i = 0; i < frames; ++i) {
        const auto frameStart = std::chrono::steady_clock::now();
        time = engine_->tick(clock);
        // The scene rebuild is CPU work that scales with the size of the world rather than with
        // what is on screen, and nothing measured it: a world scene with the camera turned to
        // face empty sky spends 12-14 ms on the GPU and 21 ms of wall clock, and the difference
        // was invisible. See docs/performance.md.
        const auto updateStart = std::chrono::steady_clock::now();
        engine_->setViewport(w, h);
        engine_->update(time);
        lastEngineUpdateMs_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();
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
            log::info("             draws={} (indirect {}, empty {}, skipped {}) shadowDraws={} "
                      "cascades={}/{} spots={} dispatches={} tris={} instances={}v/{}c lod={}/{}/{}/{} "
                      "particles={}sys/{}cap/{}emit cpu(proc)={:.2f}ms cpu(scene)={:.2f}ms",
                      st.drawCalls, st.indirectDraws, st.emptyDraws, st.skippedDraws, st.shadowDraws,
                      st.shadows.cascades, st.shadows.views, st.shadows.spots, st.computeDispatches,
                      st.triangles, st.visibleInstances, st.culledInstances, st.lodCounts[0], st.lodCounts[1],
                      st.lodCounts[2], st.lodCounts[3], st.particles.systems, st.particles.capacity,
                      st.particles.emittedThisFrame, st.procedural.cpuUpdateMs, lastEngineUpdateMs_);
            // The workload each measured phase was actually given. Without these an A/B that edits
            // a scene cannot prove its two arms differ, and "no effect" reads exactly like a run
            // whose edit never applied.
            log::info("             workload: volumeSteps={} cascades={} shadowRes={} aoTarget={}x{} "
                      "aoSlices={}x{} postPasses={} bloomLevels={} sdf={}ray/{}mesh simGrids={} "
                      "transient={} wind={}obj plants={}/{}awake ({} examined, {} slot writes)",
                      st.volume.steps, st.shadows.cascades, st.shadows.resolution, st.ao.width,
                      st.ao.height, st.ao.slices, st.ao.steps, st.post.passes, st.post.bloomLevels,
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
    // The first frames build pipelines, meshes and shadow maps, and the empty-LOD suppression has
    // not settled; they are not what a steady frame costs. Drop them when there are enough left.
    const std::size_t warmup = frameMs.size() > 24 ? 12 : 0;
    if (frameMs.size() > warmup) {
        std::vector<double> steady(frameMs.begin() + static_cast<std::ptrdiff_t>(warmup), frameMs.end());
        std::sort(steady.begin(), steady.end());
        const auto at = [&](double q) {
            return steady[std::min(steady.size() - 1, static_cast<std::size_t>(q * (steady.size() - 1)))];
        };
        log::info("frame wall clock over {} steady frames: median {:.2f} ms  p10 {:.2f}  p90 {:.2f}  min {:.2f}",
                  steady.size(), at(0.5), at(0.1), at(0.9), steady.front());
    }
    // The GPU frame and its passes, as medians over the same steady window. The passes partition
    // the frame (each is the interval between two consecutive pass ends on one timeline), so the
    // medians very nearly sum to the frame median and a phase's number responds to its own
    // workload. Sorted by cost: the top line is what to attack.
    {
        const auto median = [](std::vector<double> v) {
            if (v.empty()) {
                return -1.0;
            }
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        const std::size_t drop = gpuFrameMs.size() > 24 ? 12 : 0;
        const auto trim = [&](const std::vector<double>& v) {
            return v.size() > drop ? std::vector<double>(v.begin() + static_cast<std::ptrdiff_t>(drop), v.end())
                                   : v;
        };
        std::vector<std::pair<std::string, double>> medians;
        for (const auto& [label, samples] : passMs) {
            medians.emplace_back(label, median(trim(samples)));
        }
        std::stable_sort(medians.begin(), medians.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });
        std::string breakdown;
        double sum = 0.0;
        for (const auto& [label, ms] : medians) {
            breakdown += fmt::format(" {}={:.2f}", label, ms);
            sum += ms;
        }
        log::info("gpu frame median {:.2f} ms; pass medians (sum {:.2f}):{}", median(trim(gpuFrameMs)), sum,
                  breakdown);
    }
    log::info("headless run complete: {} frames at {} fps; GPU errors: {}", frames, options_.offlineFps,
              context_->errorCount());
    return context_->errorCount() == 0 ? 0 : 5;
}

} // namespace avgen::app
