#include "app/application.hpp"

#include "assets/image.hpp"
#include "rendering/shader_layer.hpp"
#include "core/rng.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "platform/window.hpp"
#include "rendering/scene_renderer.hpp"
#include "ui/control_panel.hpp"
#include "ui/imgui_layer.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <chrono>
#include <cstring>
#include <thread>

namespace avgen::app {

std::string usageText() {
    return "usage: avgen [options]\n"
           "  --audio <file>      load an audio file at start-up\n"
           "  --scene <file>      load a glTF/GLB scene (default: built-in orb)\n"
           "  --env <file>        load an equirectangular .hdr environment map\n"
           "  --shader <file>     add a user shader layer behind the scene (repeatable)\n"
           "  --post <file>       add a user shader layer as a post effect (repeatable)\n"
           "  --project <file>    load a project (parameters, routes, sources, presets, shaders) at start-up\n"
           "  --save-project <f>  write the project on exit\n"
           "  --play              start playback immediately\n"
           "  --frames <n>        exit after n frames\n"
           "  --stress <seed>     apply random slider-like actions every frame (seek, params, routes, volume)\n"
           "  --capture <file>    write the last frame as a PPM image\n"
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
    if (auto r = renderer_->init(); !r) {
        return std::unexpected(r.error());
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
        panel_->onSaveProject = [this] {
            window_->saveFileDialog([this](std::string path) {
                if (path.empty()) {
                    return;
                }
                if (auto r = engine_->saveProject(path); !r) {
                    log::error("save project: {}", r.error().message);
                    panel_->setStatus(r.error().message);
                } else {
                    panel_->setStatus("saved " + std::filesystem::path(path).filename().string());
                }
            });
        };
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
    if (options.project) {
        if (auto r = engine_->loadProject(*options.project); !r) {
            log::error("project: {}", r.error().message);
            if (options.headless) {
                return std::unexpected(r.error());
            }
            if (panel_) panel_->setStatus(r.error().message);
        }
    }
    if (options.autoplay && engine_->hasAudio()) {
        if (auto r = engine_->play(); !r) {
            log::warn("autoplay: {}", r.error().message);
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
    if (window_) {
        window_->setTitle("avgen 0.2 - " + path.filename().string());
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
        if (auto r = engine_->saveProject(*options_.saveProject); !r) {
            log::error("save project: {}", r.error().message);
            return code == 0 ? 6 : code;
        }
        log::info("project saved to {}", options_.saveProject->string());
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
        engine_->update(time);

        stats.width = window_->pixelWidth();
        stats.height = window_->pixelHeight();
        stats.drawCalls = renderer_->stats().drawCalls;
        stats.triangles = renderer_->stats().triangles;
        stats.gpuFrameMs = renderer_->timer().lastFrameMs();

        imgui_->newFrame();
        panel_->draw(*engine_, stats);

        const auto workBeforeAcquire = std::chrono::steady_clock::now();
        auto view = context_->acquireSurfaceView();
        const auto workAfterAcquire = std::chrono::steady_clock::now();
        if (!view) {
            log::warn("frame skipped: {}", view.error().message);
            ImGui::EndFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        gpu::TargetView target{*view, context_->surfaceFormat(), window_->pixelWidth(), window_->pixelHeight()};
        wgpu::CommandEncoder encoder = context_->device().CreateCommandEncoder();
        const rendering::ShaderFrameInputs shaderInputs{&engine_->shaderLayers(),
                                                        engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
        if (auto r = renderer_->render(encoder, engine_->scene(), time, target, &shaderInputs); !r) {
            log::error("render: {}", r.error().message);
            return 2;
        }
        imgui_->render(encoder, target);
        wgpu::CommandBuffer commands = encoder.Finish();
        context_->queue().Submit(1, &commands);
        renderer_->timer().collect();
        const auto workEnd = std::chrono::steady_clock::now();
        context_->present();
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

int Application::runHeadless() {
    const int frames = options_.frames > 0 ? options_.frames : 120;
    FixedStepClock clock(options_.offlineFps);
    const std::uint32_t w = 1280;
    const std::uint32_t h = 720;
    if (auto r = renderer_->resize(w, h); !r) {
        log::error("resize: {}", r.error().message);
        return 2;
    }
    FrameTime time{};
    std::uint64_t lastHash = 0;
    for (int i = 0; i < frames; ++i) {
        time = engine_->tick(clock);
        engine_->update(time);
        const rendering::ShaderFrameInputs shaderInputs{&engine_->shaderLayers(),
                                                        engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
        auto image = renderer_->renderToImage(engine_->scene(), time, w, h, &shaderInputs);
        if (!image) {
            log::error("render: {}", image.error().message);
            return 2;
        }
        lastHash = gpu::hashImage(*image);
        if (i % 30 == 0 || i == frames - 1) {
            const auto& f = engine_->latestFrame();
            log::info("offline frame {:4d} t={:7.3f}s bass={:.2f} mid={:.2f} treble={:.2f} rms={:.2f} onset={} scale={:.3f} "
                      "emissive={:.2f} gpu={:.2f}ms hash={:016x}",
                      i, time.renderTime, f.bands[0], f.bands[2], f.bands[4], f.rms, f.onset ? 1 : 0,
                      engine_->params().find("orb/scale") ? engine_->params().find("orb/scale")->finalComponent(0)
                                                          : engine_->params().find("root/scale")->finalComponent(0),
                      engine_->params().find("orb/emissive") ? engine_->params().find("orb/emissive")->finalComponent(0)
                                                             : engine_->params().find("material/emissiveBoost")->finalComponent(0),
                      renderer_->stats().gpuFrameMs, lastHash);
        }
        if (options_.capture && i == frames - 1) {
            if (auto r = writeCapture(*image, *options_.capture); !r) {
                log::error("capture: {}", r.error().message);
                return 4;
            }
            log::info("captured frame {} to {}", i, options_.capture->string());
        }
    }
    log::info("headless run complete: {} frames at {} fps; GPU errors: {}", frames, options_.offlineFps,
              context_->errorCount());
    return context_->errorCount() == 0 ? 0 : 5;
}

} // namespace avgen::app
