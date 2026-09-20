#include "app/application.hpp"
#include "pathtrace/denoise.hpp"
#include "app/trace_sequence.hpp"
#include "pathtrace/trace_job.hpp"
#include "labs/overlays.hpp"

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
#include "scene/procedural.hpp"
#include "core/time.hpp"
#include <cstdlib>

#include "core/interaction_latency.hpp"
#include "core/phase2_probe.hpp" // TEMPORARY: ui-responsiveness phase 2
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/surface.hpp"
#include "gpu/shader_library.hpp"
#include "platform/window.hpp"
#include "rendering/environment.hpp"
#include "rendering/particle_renderer.hpp" // ADR-360: kMaxWarmUpFrames, the --particle-warmup cap
#include "rendering/scene_renderer.hpp"
#include "rendering/debug_visualizer.hpp"
#include "app/world_builder.hpp"
#include "ui/control_panel.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include "ui/imgui_layer.hpp"
#include "ui/ui_logic.hpp"
#include "ui/unsaved_changes.hpp"

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

// ADR-320: the side of the one texture the Render panel's frame preview is uploaded into. 512
// rather than a number per render, because `RenderJob::kPreviewMaxDimension` is 480 and every
// output shape's downscale therefore fits in one corner of this whatever the aspect ratio -- so
// the texture is created once and never replaced, and ImGui caches exactly one bind group for it.
constexpr std::uint32_t kRenderPreviewAtlas = 512;

namespace {
// "a:b:c" -> {"a","b","c"}. Empty tokens are dropped so a trailing separator is not an arm.
std::vector<std::string> splitList(std::string_view spec, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= spec.size()) {
        const std::size_t at = spec.find(sep, start);
        const std::string_view token =
            spec.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start);
        if (!token.empty()) {
            out.emplace_back(token);
        }
        if (at == std::string_view::npos) {
            break;
        }
        start = at + 1;
    }
    return out;
}

// An arm's name may carry "+"-separated flags after the script, each meaning "run this block with
// one of ADR-233's fixes put back". That is what makes a before/after a *difference* rather than a
// comparison of two runs on a machine three agents share: the two arms are blocks of the same
// process, seconds apart, under the same contention and the same thermal state.
//
//   idle+legacy     the procedural double generation, restored
//   sliders+skynow  the sky IBL rebuilt inside the frame again, undeferred
//   strip+seeknow   the playhead's re-simulation back inside the gesture, undeferred
struct UiAbArm {
    std::string script;
    bool legacyProcGen = false;
    bool eagerSky = false;
    bool eagerSeek = false;
};
UiAbArm parseUiAbArm(std::string_view spec) {
    UiAbArm out;
    std::size_t start = 0;
    bool first = true;
    while (start <= spec.size()) {
        const std::size_t at = spec.find('+', start);
        const std::string_view token =
            spec.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start);
        if (first) {
            out.script = std::string(token);
            first = false;
        } else if (token == "legacy") {
            out.legacyProcGen = true;
        } else if (token == "skynow") {
            out.eagerSky = true;
        } else if (token == "seeknow") {
            out.eagerSeek = true;
        } else {
            // An unknown flag must not read as "no flag": it would silently measure the arm twice
            // and report the pair as a null result (ADR-182's vacuous arm, exactly).
            out.script = "?" + std::string(token);
        }
        if (at == std::string_view::npos) {
            break;
        }
        start = at + 1;
    }
    return out;
}

} // namespace

std::string usageText() {
    return "usage: avgen [options]\n"
           "  --audio <file>      load an audio file at start-up\n"
           "  --scene <file>      load a glTF/GLB scene (default: built-in orb)\n"
           "  --env <file>        load an equirectangular .hdr environment map\n"
           "  --composition <f>   load a scene composition file (avgen-scene JSON)\n"
           "  --export-bundle <d> copy every referenced asset into <d>/assets and write <d>/project.json\n"
           "  --render <out>      offline render (headless) to a PNG sequence directory or a video file\n"
           "  --pathtrace <out.exr>  CPU path trace (headless, no GPU) of one frame to scene-linear EXR;\n"
           "                      takes its resolution from --size\n"
           "  --pt-samples <n>    samples per pixel for --pathtrace (default 32)\n"
           "  --pt-depth <n>      path depth for --pathtrace (default 4)\n"
           "  --pt-seconds <s>    timeline second to trace (default 0)\n"
           "  --pt-seed <n>       sampler seed; the image is a pure function of it\n"
           "  --pt-threads <n>    worker threads (0 = hardware concurrency)\n"
           "  --pt-aovs           write one multi-layer EXR with albedo, normal, emission, depth, id\n"
           "  --pt-denoise        denoise with OIDN (needs -DAVGEN_PATHTRACE_DENOISE=ON)\n"
           "  --pt-probe          report directional albedo above 1 (ADR-352); does not alter the image\n"
           "                      (.mov/.mp4/...); size/fps/range/codec from the project's render settings\n"
           "  --format <kind>     render output kind: png (default for a directory), exr (scene-linear half\n"
           "                      EXR sequence, before tone mapping), or video\n"
           "  --range <a>:<b>     render time range in seconds (either side may be empty)\n"
           "  --codec <id>        video codec: prores4444, prores422, h264, hevc, or an ffmpeg encoder name\n"
           "  --quality <0-100>   video quality\n"
           "  --queue <file>      run a render queue (JSON list of projects and render settings), headless\n"
           "  --input [name]      analyze a live capture device (substring of its name; default device)\n"
           "  --osc-port <n>      OSC listen port (overrides the project's control map)\n"
           "  --list-audio-devices, --list-midi   enumerate inputs and exit\n"
           "  --labs                              the Engineering Lab Suite: what each lab owns,\n"
           "                                      what it does not, and where its decision is made\n"
           "  --lab-case <lab>:<n>                open a lab case: its fixture, second, size, tier,\n"
           "                                      arms and overlays (docs/engineering-labs.md)\n"
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
           "  --director <k=v,..> --direct with the Auto-director panel's settings:\n"
           "                      mode=continuous|edited|song, autonomy=locked|guided|expressive,\n"
           "                      minShot, minBuildShot, maxShot (s), maxSpeed (m/s),\n"
           "                      maxSwing (deg/s), dwell (shots), seed\n"
           "  --song-plan <file>  load a song plan (sections and shot intents) for mode=song\n"
           "  --save-project <f>  write the project on exit\n"
           "  --save-scene <f>    write the scene on exit (the camera collection and its shot\n"
           "                      track live here, not in the project)\n"
           "  --play              start playback immediately\n"
           "  --frames <n>        exit after n frames\n"
           "  --stress <seed>     apply random slider-like actions every frame (seek, params, routes, volume)\n"
           "  --ai-prompt <text>  run one AI task at start-up against the configured provider\n"
           "  --ai-script <file>  run the AI control plane against a scripted provider (JSON with a\n"
           "                      'turns' array). Deterministic: no key, no network, real tools\n"
           "  --ui-script <arms>  drive the editor with a repeatable interaction: comma-separated from\n"
           "                      hover,sliders,panels,select,scrub,camera,tabs,edit,strip,gizmo,box,\n"
           "                      star,click,drag,\n"
           "                      star,click\n"
           "                      -- or idle, or all\n"
           "  --ui-ab <a>:<b>..   interleave those --ui-script arms in blocks inside ONE process and\n"
           "                      report each arm's frame distribution side by side. The only honest\n"
           "                      way to compare two interactions on a machine other agents share\n"
           "  --ui-ab-frames <n>  frames per block (default 120; keep it a multiple of 60, the\n"
           "                      pointer arms' gesture cycle)\n"
           "  --ui-ab-blocks <n>  passes over the arm list (default 4)\n"
           "  --ui-ab-settle <n>  frames discarded after each switch (default 12)\n"
           "                      an arm may be suffixed '+legacy' (procedural double-generation\n"
           "                      restored), '+skynow' (sky IBL rebuilt inside the frame) and/or\n"
           "                      '+seeknow' (the playhead's re-simulation back inside the gesture),\n"
           "                      so each fix has a before arm in the same process\n"
           "  --canvas-scale <f>  render the world at this fraction of the canvas's pixels (0.25-1)\n"
           "  --supersample <f>   offline render only: render the scene at this multiple of the output\n"
           "  --particle-warmup <n>  step the particle pools n frames before the first frame of a\n"
           "                      render range or after a seek, so the range does not open on an\n"
           "                      empty field (ADR-360). 0 (the default) is the bloom-in. Use at\n"
           "                      least the scene's longest particle lifetime, in frames; capped\n"
           "                      at 240\n"
           "  --viewport-matches-render   lift the distance detail limits in the viewport too, so\n"
           "                      live playback shows what a render will (costs frame time)\n"
           "  --preview-mode <m>  open the canvas in workspace | outputFrame | outputPreview. The\n"
           "                      last two render at the project's output aspect ratio (ADR-246)\n"
           "  --aov <list>        offline render only: write auxiliary passes beside the frames as\n"
           "                      scene-linear EXRs. normal (xyz + roughness in alpha), emission,\n"
           "                      depth (metres), velocity, id, shadow (directional visibility of\n"
           "                      lights 0/1/2, recomputed at full resolution -- ADR-255; refused\n"
           "                      on a scene with no directional light). Comma separated.\n"
           "  --post-stages <dir> offline render only: write every intermediate the post chain\n"
           "                      rendered -- exposure, bloom/prefilter, bloom/downN, bloom/upN,\n"
           "                      halation/*, wide, composite, fxaa, sharpen -- as scene-linear\n"
           "                      EXRs at the resolution the chain chose, plus stages.json with\n"
           "                      each one's extent, peak and mean. A diagnostic: the readback is\n"
           "                      synchronous, so use it with --range t:t (ADR-277)\n"
           "                      size and resolve down (1 = off, max 2). Buys back the sub-pixel\n"
           "                      detail a small output cannot sample.\n"
           "  --profile-cpu       print the main thread's per-phase frame distribution on exit\n"
           "  --profile-csv <f>   write one row per frame (every phase) to <f> on exit\n"
           "  --latency           interaction-latency report on exit (one record per interaction)\n"
           "  --latency-csv <f>   write one row per interaction to <f> on exit\n"
           "  --latency-inject <kind>:<ms>  slow a named interaction deliberately, to show the\n"
           "                      harness detects it (ADR-182). Its records are excluded from\n"
           "                      every distribution.\n"
           "  --capture <file>    write the last frame as a PPM image\n"
           "  --render-preview    show the frames a render is writing in the Render panel, whatever\n"
           "                      the settings file remembers (ADR-320)\n"
           "  --render-in-app <p> start the project's render in the window, as the Render button\n"
           "                      does, instead of headlessly. The only way to reach the Render\n"
           "                      panel's mid-render state from a script or a capture\n"
           "  --capture-ui <f>    write the editor, ImGui and all, as a PNG\n"
           "  --capture-ui-frame <n>  which frame to grab (default 90)\n"
           "  --capture-ui-panel <a,b>  open and raise these panels first, so a closed or\n"
           "                      tab-buried panel can be photographed; last named ends up on top\n"
           "  --capture-ui-stay   keep running after the capture instead of quitting\n"
           "  --debug-draw <list> debug overlays, comma separated: beams (every particle\n"
           "                      emitter's disc, its column's axis and where the column ends),\n"
           "                      entityOrigins, entityBounds, entityIds, skeletons, worldAxes,\n"
           "                      frustum, transformTrail, points, bounds, normals, splines,\n"
           "                      lod (the rung each instance drew at), shadowCascades,\n"
           "                      shadowCascadeSlices, shadowCasters, lights (each light's\n"
           "                      emitter, direction and the sphere of influence past which the\n"
           "                      froxel pass stops evaluating it), lightClusters (the froxel\n"
           "                      grid, coloured by how many lights reach each occupied froxel).\n"
           "                      Works in --render, where there is no panel to switch them on\n"
           "  --debug-target <t>  display an auxiliary render target: normal|roughness|velocity|\n"
           "                      emission|ids|occlusion|depth|linear depth|depth edges|\n"
           "                      object depth|overdraw|fragment density|temporal history\n"
           "  --tier <t>          quality tier: preview|realtime|high|offline\n"
           "  --render-limits <m> distance detail in a render: tier|live|unlimited\n"
           "  --disable <list>    switch phases off for cost attribution, or subsystems off for\n"
           "                      forensic isolation:\n"
           "                      shadows,ao,volume,post,shadowmask,\n"
           "                      water,transparency,particles,animation,cameramotion,fxaa\n"
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
        } else if (arg == "--labs") {
            options.listLabs = true;
        } else if (arg == "--lab-case") {
            auto v = need(i, "--lab-case");
            if (!v) return std::unexpected(v.error());
            auto resolved = labs::resolveCaseSpec(*v, labs::repositoryRoot());
            if (!resolved) return std::unexpected(resolved.error());
            // ADR-275. A case may be written down before the unit that makes it answerable exists.
            // Refusing here, by name, is the point: opening the fixture anyway would show somebody
            // a scene in which the thing they came to look at is not happening, and leave them to
            // work out why.
            if (!resolved->runnable()) {
                return fail("{} case {} ({}) cannot be run yet: it is waiting on {}", resolved->lab,
                            resolved->number, resolved->title, resolved->blockedBy);
            }
            options.labCase = *resolved;
            ++i;
        } else if (arg == "--list-midi") {
            options.listMidi = true;
        } else if (arg == "--render") {
            auto v = need(i, "--render");
            if (!v) return std::unexpected(v.error());
            options.render = *v;
            options.headless = true;
            ++i;
        } else if (arg == "--pathtrace") {
            auto v = need(i, "--pathtrace");
            if (!v) return std::unexpected(v.error());
            options.pathtrace = *v;
            options.headless = true;
            ++i;
        } else if (arg == "--pt-samples") {
            auto v = need(i, "--pt-samples");
            if (!v) return std::unexpected(v.error());
            options.ptSamples = static_cast<std::uint32_t>(std::strtoul(v->c_str(), nullptr, 10));
            ++i;
        } else if (arg == "--pt-depth") {
            auto v = need(i, "--pt-depth");
            if (!v) return std::unexpected(v.error());
            options.ptDepth = static_cast<std::uint32_t>(std::strtoul(v->c_str(), nullptr, 10));
            ++i;
        } else if (arg == "--pt-seconds") {
            auto v = need(i, "--pt-seconds");
            if (!v) return std::unexpected(v.error());
            options.ptSeconds = std::strtod(v->c_str(), nullptr);
            ++i;
        } else if (arg == "--pt-seed") {
            auto v = need(i, "--pt-seed");
            if (!v) return std::unexpected(v.error());
            options.ptSeed = std::strtoull(v->c_str(), nullptr, 10);
            ++i;
        } else if (arg == "--pt-threads") {
            auto v = need(i, "--pt-threads");
            if (!v) return std::unexpected(v.error());
            options.ptThreads = static_cast<unsigned>(std::strtoul(v->c_str(), nullptr, 10));
            ++i;
        } else if (arg == "--pt-denoise") {
            options.ptDenoise = true;
        } else if (arg == "--pt-aovs") {
            options.ptAovs = true;
        } else if (arg == "--pt-probe") {
            options.ptProbe = true;
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
        } else if (arg == "--song-plan") {
            auto v = need(i, "--song-plan");
            if (!v) return std::unexpected(v.error());
            options.songPlan = *v;
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
        } else if (arg == "--save-scene") {
            auto v = need(i, "--save-scene");
            if (!v) return std::unexpected(v.error());
            options.saveScene = *v;
            ++i;
        } else if (arg == "--capture") {
            auto v = need(i, "--capture");
            if (!v) return std::unexpected(v.error());
            options.capture = *v;
            ++i;
        } else if (arg == "--render-preview") {
            options.renderPreview = true;
        } else if (arg == "--render-in-app") {
            auto v = need(i, "--render-in-app");
            if (!v) return std::unexpected(v.error());
            options.renderInApp = *v;
            ++i;
        } else if (arg == "--capture-ui") {
            auto v = need(i, "--capture-ui");
            if (!v) return std::unexpected(v.error());
            options.captureUi = *v;
            ++i;
        } else if (arg == "--capture-ui-frame") {
            auto v = need(i, "--capture-ui-frame");
            if (!v) return std::unexpected(v.error());
            options.captureUiFrame = std::max(1, std::atoi(v->c_str()));
            ++i;
        } else if (arg == "--capture-ui-panel") {
            auto v = need(i, "--capture-ui-panel");
            if (!v) return std::unexpected(v.error());
            // Comma-separated, so one flag can raise a whole tab group's worth in one run and the
            // last one named is the one on top.
            for (std::size_t start = 0; start <= v->size();) {
                const std::size_t comma = v->find(',', start);
                std::string one = v->substr(start, comma == std::string::npos ? std::string::npos
                                                                              : comma - start);
                if (!one.empty()) {
                    options.captureUiPanels.push_back(std::move(one));
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            ++i;
        } else if (arg == "--capture-ui-stay") {
            options.captureUiQuit = false;
        } else if (arg == "--debug-draw") {
            auto v = need(i, "--debug-draw");
            if (!v) return std::unexpected(v.error());
            options.debugDraw = *v;
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
        } else if (arg == "--ui-ab") {
            auto v = need(i, "--ui-ab");
            if (!v) return std::unexpected(v.error());
            // Every arm is validated here rather than at the first switch: a typo in arm four of a
            // six-arm run must not be discovered after four hundred frames of measuring.
            for (const std::string& armSpec : splitList(*v, ':')) {
                if (!parseUiScript(parseUiAbArm(armSpec).script)) {
                    return fail("--ui-ab arm '{}' is not an arm; expected some of {}, optionally "
                                "suffixed '+legacy', '+skynow' and/or '+seeknow'",
                                armSpec, uiScriptNames());
                }
            }
            options.uiAb = *v;
            ++i;
        } else if (arg == "--ui-ab-frames" || arg == "--ui-ab-blocks" || arg == "--ui-ab-settle") {
            auto v = need(i, arg.c_str());
            if (!v) return std::unexpected(v.error());
            int n = 0;
            try {
                n = std::stoi(*v);
            } catch (const std::exception&) {
                return fail("{} expects an integer", arg);
            }
            if (n < 0) {
                return fail("{} expects a non-negative integer", arg);
            }
            if (arg == "--ui-ab-frames") options.uiAbFrames = std::max(1, n);
            else if (arg == "--ui-ab-blocks") options.uiAbBlocks = std::max(1, n);
            else options.uiAbSettle = n;
            ++i;
        } else if (arg == "--particle-warmup") {
            auto v = need(i, "--particle-warmup");
            if (!v) return std::unexpected(v.error());
            const long n = std::strtol(v->c_str(), nullptr, 10);
            if (n < 0 || n > static_cast<long>(rendering::ParticleRenderer::kMaxWarmUpFrames)) {
                return fail("--particle-warmup must be 0 to {}, got '{}'",
                            rendering::ParticleRenderer::kMaxWarmUpFrames, *v);
            }
            options.particleWarmUpFrames = static_cast<std::uint32_t>(n);
            ++i;
        } else if (arg == "--supersample") {
            auto v = need(i, "--supersample");
            if (!v) return std::unexpected(v.error());
            options.supersample = std::strtof(v->c_str(), nullptr);
            if (options.supersample < 1.0f || options.supersample > 2.0f) {
                return fail("--supersample must be 1 (off) to 2, got '{}'", *v);
            }
            ++i;
        } else if (arg == "--viewport-matches-render") {
            options.liftViewportLimits = true;
        } else if (arg == "--preview-mode") {
            // ADR-246. Exists so the output preview can be driven by the scripted-interaction
            // driver and by a benchmark arm: the mode is otherwise only reachable from a toolbar,
            // and a UI mode that can only be entered by hand is a UI mode nobody measures.
            auto v = need(i, "--preview-mode");
            if (!v) return std::unexpected(v.error());
            ui::PreviewViewMode mode{};
            if (!ui::previewViewModeFromName(*v, mode)) {
                return fail("--preview-mode must be workspace, outputFrame or outputPreview, got '{}'",
                            *v);
            }
            options.previewMode = mode;
            ++i;
        } else if (arg == "--aov") {
            auto v = need(i, "--aov");
            if (!v) return std::unexpected(v.error());
            options.aovs = *v;
            ++i;
        } else if (arg == "--post-stages") {
            // ADR-277. Not a setting the project keeps: a diagnostic, like --debug-draw, that
            // names where this run should put the chain's intermediates.
            auto v = need(i, "--post-stages");
            if (!v) return std::unexpected(v.error());
            options.postStages = std::filesystem::path(*v);
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
        } else if (arg == "--latency") {
            options.latencyReport = true;
        } else if (arg == "--latency-csv") {
            auto v = need(i, "--latency-csv");
            if (!v) return std::unexpected(v.error());
            options.latencyCsv = std::filesystem::path(*v);
            options.latencyReport = true;
            ++i;
        } else if (arg == "--latency-inject") {
            auto v = need(i, "--latency-inject");
            if (!v) return std::unexpected(v.error());
            const std::string spec = *v;
            const std::size_t colon = spec.rfind(':');
            if (colon == std::string::npos) {
                return std::unexpected(
                    Error{fmt::format("--latency-inject wants <kind>:<ms>, got '{}'", spec)});
            }
            const std::string kind = spec.substr(0, colon);
            // Refused rather than defaulted. A mistyped kind that silently slowed the first
            // enumerator would produce a calibration run that proves the instrument can see a
            // slowdown in an interaction nobody asked about -- which is the exact failure the
            // control exists to rule out.
            if (!core::interactionFromName(kind)) {
                return std::unexpected(
                    Error{fmt::format("--latency-inject: no such interaction '{}'", kind)});
            }
            double ms = 0.0;
            try {
                ms = std::stod(spec.substr(colon + 1));
            } catch (const std::exception&) {
                return std::unexpected(
                    Error{fmt::format("--latency-inject: '{}' is not a number of milliseconds",
                                      spec.substr(colon + 1))});
            }
            options.latencyInject.emplace_back(kind, ms);
            options.latencyReport = true;
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
    // A lab case is a set of flags with a number for a name (spec 32). Applying it HERE, rather
    // than anywhere downstream, is what makes that true: everything below this line sees a plain
    // set of options and cannot behave differently because a case was named. A flag written
    // explicitly beside `--lab-case` wins, because the case is the starting point of an
    // investigation and the thing a person then varies is the flag they typed.
    if (options.labCase) {
        const labs::LabCase& c = *options.labCase;
        const std::filesystem::path fixture = labs::repositoryRoot() / c.fixture;
        // `.scene.json` is a composition; anything else `--project` loads. The extension is what
        // `Engine::loadFile` routes on, so this is the same rule and not a second one.
        const std::string name = fixture.filename().string();
        const bool isScene = name.size() > 11 && name.substr(name.size() - 11) == ".scene.json";
        if (isScene) {
            if (!options.composition) options.composition = fixture;
        } else if (!options.project) {
            options.project = fixture;
        }
        if (!options.renderWidth) options.renderWidth = c.width;
        if (!options.renderHeight) options.renderHeight = c.height;
        if (!options.fpsGiven) {
            options.offlineFps = c.fps;
            options.fpsGiven = true;
        }
        if (options.qualityTier.empty()) options.qualityTier = c.tier;
        if (options.disablePasses.empty()) {
            for (const std::string& arm : c.disable) {
                options.disablePasses += (options.disablePasses.empty() ? "" : ",") + arm;
            }
        }
        if (options.qualityArms.empty()) {
            for (const std::string& arm : c.qualityArms) {
                options.qualityArms += (options.qualityArms.empty() ? "" : ",") + arm;
            }
        }
        if (!options.aovs && !c.aovs.empty()) {
            std::string list;
            for (const std::string& aov : c.aovs) {
                list += (list.empty() ? "" : ",") + aov;
            }
            options.aovs = list;
        }
        if (c.supersample != 1.0 && options.supersample <= 1.0) {
            options.supersample = static_cast<float>(c.supersample);
        }
    }
    // ADR-277, and ADR-225's rule: a flag the program then ignores is worse than one it refuses.
    // The post-stage capture lives in RenderJob, so without a render there is nothing to arm and
    // the directory would be created and left empty.
    if (!options.postStages.empty() && !options.render && !options.queue) {
        return fail("--post-stages needs --render (or --queue): the capture is part of an offline "
                    "render job and there is nothing to arm without one");
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
    // The two offline jobs, and they were missing from this list (ADR-364). A `RenderJob` owns a
    // `gpu::ReadbackRing` whose destructor calls `Context::waitFor` on an outstanding map; a
    // `unique_ptr` member destroys in reverse declaration order, and `job_` is declared at :346
    // against `context_` at :394, so the context went FIRST and the ring then waited on a Dawn
    // instance that no longer existed. Measured: `--render-in-app` plus `--frames n` with n small
    // enough that the render is still in flight segfaults in
    // `ReadbackRing::~ReadbackRing -> Context::waitFor -> InstanceBase::APIWaitAny`, with the
    // report naming a pointer-authentication failure. It reproduces on a pristine `main` build at
    // 1cdfb84a, so it is not this branch's -- but this is the list whose comment claims to own
    // destruction order, and the jobs were simply never added to it.
    //
    // `ptJob_` is here for the weaker but real version of the same reason: its coordinator thread
    // is joined by its own destructor, and a thread still running while the engine underneath it
    // is torn down is a race nobody would find twice.
    job_.reset();
    ptJob_.reset();
    // ADR-383, and ADR-364's lesson applied the moment it was earned: a job that is not on this
    // list is a job whose thread outlives the engine it is reading. Cancel first, then join, then
    // destroy -- `run()` is on that thread and holds a reference to the sequence's own engine.
    if (ptSequence_) {
        ptSequence_->cancel();
    }
    if (ptSeqThread_.joinable()) {
        ptSeqThread_.join();
    }
    ptSequence_.reset();
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
        // ADR-225/ADR-246: the preview's view state is the editor's, and it has to come back.
        // Taken from the panel here rather than written through a pointer by every toolbar widget,
        // for the same reason the render scale is: ImGui writes these flags directly and there is
        // no hook to set a dirty bit in.
        settings_.preview = panel_->preview;
        // ADR-320/ADR-225. Same shape and same reason as the two above: the Render panel writes
        // this bool through directly, so it is read back here rather than hooked at the widget.
        //
        // Not written back when `--render-preview` forced it on. A flag owns the session it was
        // given and nothing beyond it: a capture or a benchmark arm that leaves the machine's
        // remembered state different from how it found it is a measurement that changes its own
        // conditions, and the next person's editor opens with an instrument they did not ask for.
        if (!options_.renderPreview) {
            settings_.renderFramePreview = panel_->renderPreview.enabled;
        }
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
    // ADR-360: off unless asked for, and asked for exactly once, here. A seek in the editor keeps
    // the hard pool reset it has always had -- EntityWorld::seek already re-simulates up to 90 s
    // per scrub click and is the measured cause of the app's scrub lag, and this would be a second
    // re-simulation stacked on it.
    renderer_->setParticleWarmUpFrames(options_.particleWarmUpFrames);
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
            rendering::AuxDebugView::FragmentDensity,
            rendering::AuxDebugView::TemporalHistory};
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

    if (auto applied = applyDebugDraw(); !applied) {
        return applied;
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
        panel_->world.debug = cliDebug_; // `--debug-draw` seeds the panel; the panel then owns them
        // The world editor records into the application's history rather than one of its own, and
        // registers as the context that answers Copy, Delete and the rest for world objects.
        panel_->editor.attachEdits(edits_);
        panel_->edits = &edits_;
        edits_.addContext(panel_->editor);
        panel_->canvasRenderScale =
            options_.canvasScale != 1.0f ? options_.canvasScale : settings_.canvasRenderScale;
        panel_->preview = settings_.preview;
        panel_->renderPreview.enabled = settings_.renderFramePreview || options_.renderPreview; // ADR-320
        // The flag outranks the remembered state, and only when it was given: a benchmark arm has
        // to be able to say which mode it is measuring without depending on how this machine's
        // settings file happens to be left.
        if (options_.previewMode) {
            panel_->preview.mode = *options_.previewMode;
        }
        // Never restored: a fullscreen preview closes every panel, and a session that ended in one
        // must not reopen with an empty editor and no record of what was open. The pan is dropped
        // for the same class of reason -- it only means anything against the canvas it was made at.
        panel_->preview.fullscreen = false;
        panel_->preview.panX = 0.0f;
        panel_->preview.panY = 0.0f;
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
        // **A section edit is a director input** (ADR-247, ADR-249).
        //
        // Song Mode is cut from the section timeline, and the section timeline is edited in the
        // Sequencer -- a different window from the one that holds the Enable button. So retyping a
        // section, or changing its treatment, changed the film's script and left the film alone
        // until somebody thought to press Enable again. Reported as "I set eight Build sections to
        // Rapid Multi-Shot Coverage and when I change this to anything I still get the same shot",
        // and half of that report was this: nothing had re-cut.
        //
        // The gate is here rather than in the panel because the two facts it needs are here. The
        // other two director modes fold the *audio* and never read a section type, so a re-cut for
        // them would be work with no possible effect on the picture.
        panel_->sequence.onSectionsEdited = [this] {
            if (engine_ == nullptr || cameraDirection_.settings.mode != DirectorMode::Song) {
                return;
            }
            if (!engine_->timeline().isAutomated("camera/position")) {
                return; // the camera is with the viewport; nothing to re-cut
            }
            if (auto r = directCameraFromTrack(); !r) {
                log::warn("direct: {}", r.error().message);
                if (panel_ != nullptr) {
                    panel_->setStatus(r.error().message);
                }
            } else if (renderer_ != nullptr) {
                renderer_->resetTemporalHistory();
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
        panel_->onFreeCamera = [this]() { ensureFreeCamera(/*deliberate=*/true); };
        // ADR-391. Choosing what the canvas shows is the host's state, because the host is what
        // knows the two things that overrule the choice (see `effectiveViewportView`).
        panel_->onViewportView = [this](scene::ViewportView view) {
            viewportView_ = view;
            applyViewportView();
        };
        // Navigation's one seam, handed to the panel so "go to camera" cannot become a second way
        // to write the film's camera.
        panel_->onMoveViewport = [this](glm::vec3 eye, glm::vec3 target) {
            CameraPose pose;
            pose.eye = eye;
            pose.target = target;
            setViewportPose(pose);
        };
        panel_->cameraLocked = &cameraLocked_;
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
        auto saveTo = [this](const std::filesystem::path& path) { saveProjectTo(path); };
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
            // ADR-440: File > New discards the open project as completely as File > Open does.
            requestClose(ui::CloseIntent::NewProject, [this] {
                engine_->newProject();
                edits_.clearHistory();
                if (panel_ != nullptr) {
                    // The history describes a document that is gone (ADR-092). `openAny` does this
                    // for every other close; File > New is the one path that never did, so undo
                    // after a New would have edited the new project with the old one's commands.
                    panel_->editor.reset();
                    panel_->setStatus("new project");
                }
                refreshWindowTitle();
            });
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
        // Opening a lab is opening its fixture *and* selecting what to look at while it is open.
        // The second half is the whole difference from File > Examples, and it is why this is not
        // just another index entry.
        panel_->onOpenLab = [this](const labs::LabDescriptor& lab) {
            // The overlay selection is part of opening the lab, so it moves inside the gated
            // action (ADR-440). Left outside, cancelling the prompt would still have switched the
            // overlays and the status line -- a Cancel that changed something is not a Cancel.
            const std::filesystem::path fixture = labs::repositoryRoot() / std::string(lab.fixture);
            const auto overlays = labs::overlaysFor(lab.id);
            const std::string note = fmt::format("{}: {}", lab.title, lab.question);
            requestClose(ui::CloseIntent::OpenProject,
                         [this, fixture, overlays, note] {
                             beginOpen(fixture);
                             panel_->world.debug = overlays;
                             panel_->world.showDebugOptions = true;
                             panel_->setStatus(note);
                         },
                         fixture);
        };
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
        liftViewportLimits_ = options.liftViewportLimits;
        panel_->liftViewportLimits = &liftViewportLimits_;
        // ---- path tracing from the UI ----
        // The samples and bounces used to be assigned here, which is why a project could not carry
        // them: two of the eight settings were a hard-coded pair at the binding site and the other
        // six were struct defaults (ADR-366). They come from the project now, like `render` does.
        uiPathTrace_ = engine_->pathTraceSettings();
        panel_->pathTraceSettings = &uiPathTrace_;
        panel_->pathTraceDenoiseAvailable = pathtrace::denoiseAvailable();
        panel_->pathTraceSequenceProgress = [this]() -> std::optional<SequenceProgress> {
            if (!ptSequence_) {
                return std::nullopt;
            }
            SequenceProgress p = ptSequence_->progress();
            if (p.error.empty() && !ptSeqError_.empty()) {
                p.error = ptSeqError_;
            }
            p.finished = p.finished || ptSeqDone_.load();
            return p;
        };
        panel_->pathTraceSequenceSamples = [this]() -> std::pair<std::uint32_t, std::uint32_t> {
            if (!ptSequence_) {
                return {0, 0};
            }
            return {ptSequence_->frameSamplesDone(), ptSequence_->frameSamplesTotal()};
        };
        panel_->pathTraceProgress = [this]() -> pathtrace::TraceProgress {
            // While a job exists it IS the answer; once it is gone, the last thing it said.
            if (ptJob_) {
                lastPathTrace_ = ptJob_->progress();
                return lastPathTrace_;
            }
            return lastPathTrace_;
        };
        panel_->onStartPathTrace = [this] { startPathTraceFromUi(); };
        panel_->onCancelPathTrace = [this] {
            if (ptJob_) ptJob_->cancel();
            if (ptSequence_) ptSequence_->cancel();
        };
        panel_->onChoosePathTraceOutput = [this] {
            window_->saveFileDialog(platform::Window::SaveKind::Exr, [this](std::string path) {
                if (path.empty()) {
                    return;
                }
                uiPathTrace_.outputPath = std::filesystem::path(path).replace_extension(".exr");
            });
        };
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
            engine_->pathTraceSettings() = uiPathTrace_;
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
        //
        // The project's own Auto-director settings first, and the command line's over the top of
        // them. `syncDirectorSettings()` is what normally carries a loaded project's settings onto
        // this copy, and it runs *below* -- so before this line, `--project p.json --direct` on a
        // project that had saved a shot mode directed with the defaults and silently produced a
        // different film from the one the file describes. The same shape of bug as the missing
        // `settings` argument ADR-225 records, in the other direction.
        //
        // Only here, and deliberately not inside `directCameraFromTrack()`: on the interactive path
        // that function is called by the panel *while a slider is being dragged*, and the engine's
        // copy has not caught up yet -- taking it there would throw the drag away.
        cameraDirection_.settings = engine_->autoDirector();
        lastDirectorSync_ = cameraDirection_.settings;
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
    // The two halves of a lab case that are not flags: the second it is about, and the overlays
    // its lab looks at while asking its question (spec 8, 32). Both after the load, because there
    // is nothing to seek in and no panel to configure before it.
    //
    // `seekSeconds` moves the clock and `update` re-derives the scene (ADR-091); this only does the
    // first, and that is correct -- the run loop's next `update` is what derives the frame. A
    // `seekSeconds` here followed by a read here would report the frame the engine was already on,
    // which is the mistake that has produced two false results in this repository.
    if (options.labCase) {
        const labs::LabCase& c = *options.labCase;
        if (c.timeSeconds > 0.0) {
            engine_->seekSeconds(c.timeSeconds);
        }
        if (const auto id = labs::findLab(c.lab); id && panel_) {
            panel_->world.debug = labs::overlaysFor(*id);
            panel_->world.showDebugOptions = true;
        }
        log::info("{} case {}: {}", c.lab, c.number, c.title);
        log::info("  reproduce: {}", labs::reproduceCommand(c));
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
    // ADR-440. A project replaces the one that is open, so it is offered the prompt first; audio,
    // an environment map, a scene and a shader are *added to* the open project and are not a close.
    // The check is on what the open would do, not on which menu asked for it, so Open Recent, the
    // examples list, the labs and a dropped file are all covered by this one line.
    if (opensADifferentProject(path)) {
        const std::filesystem::path copy = path;
        requestClose(ui::CloseIntent::OpenProject, [this, copy] { beginOpen(copy); }, path);
        return;
    }
    beginOpen(path);
}

void Application::beginOpen(const std::filesystem::path& path) {
    pendingOpen_ = path;
    panel_->loading = ui::ControlPanel::Loading{
        .active = true, .what = path.filename().string(), .stage = {}, .index = 0, .count = 1};
    panel_->setStatus(fmt::format("Opening {}...", path.filename().string()));
}

// ---- what counts as closing the project (ADR-440) --------------------------------------------
//
// Mirrors `Engine::loadFile`'s routing rather than restating it as a list of extensions to keep in
// step: anything that is not a .json is audio, a scene, an environment or a shader, and every one
// of those modifies the open project instead of replacing it. A .json is a project unless it
// announces itself as a scene document, and a recipe builds a whole new world, which discards
// everything live just as surely as a project does.
bool Application::opensADifferentProject(const std::filesystem::path& path) {
    if (world::isRecipeFile(path)) {
        return true;
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".json") {
        return false;
    }
    std::ifstream in(path);
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) {
        return false;
    }
    return doc.value("format", std::string()) != scene::Composition::kFormatName;
}

bool Application::requestClose(ui::CloseIntent intent, std::function<void()> action,
                               const std::filesystem::path& path) {
    if (closeGate_.busy()) {
        // A modal is already up, or a save is in flight. Dropping the second request is what keeps
        // three taps on Cmd-Q from stacking three pending quits behind one dialog.
        return false;
    }
    // Measured here and nowhere else: this is the one moment the answer is needed, and it costs a
    // full serialisation (31 ms on the heaviest project in the repository), which is invisible
    // beside the two-second load it is about to gate.
    //
    // `edits_.dirty()` is asked first because it is free and it can only say yes: a world edit that
    // has not been saved is unsaved work whatever the documents say. It is not trusted to say *no*
    // -- it knows about the world editor's commands and nothing about a slider, a light or a
    // timeline -- so a clean history falls through to the comparison rather than short-circuiting
    // it. (It also gives `EditSystem::dirty()` its first production reader; before ADR-440 it had
    // neither a reader nor a writer.)
    const bool dirty = edits_.dirty() || engine_->projectDirty(touchedSinceDirtySample_);
    touchedSinceDirtySample_ = false;
    lastDirtySample_ = std::chrono::steady_clock::now();
    if (closeGate_.requestClose(intent, dirty, path)) {
        action();
        return true;
    }
    pendingClose_ = std::move(action);
    return false;
}

void Application::serviceCloseGate() {
    bool answered = false;
    const std::string name = engine_->projectPath().empty()
                                 ? std::string()
                                 : engine_->projectPath().filename().string();
    const ui::UnsavedAnswer answer = ui::drawUnsavedChangesModal(closeGate_, name, &answered);
    if (answered) {
        closeGate_.answer(answer);
        if (closeGate_.state() == ui::UnsavedChangesGate::State::AwaitingSave) {
            if (engine_->projectPath().empty()) {
                // Yes on a project with no path is Save As -- and Cancel in *that* dialog cancels
                // the whole close rather than falling through to discarding, which is the half of
                // this that is easy to get wrong.
                saveProjectAsForGate();
            } else {
                saveProjectTo(engine_->projectPath());
                closeGate_.saveFinished(!engine_->projectDirtyCached());
            }
        }
        if (!closeGate_.busy() && closeGate_.state() == ui::UnsavedChangesGate::State::Idle) {
            // Cancel, here or in the Save As dialog. The pending action is dropped without being
            // performed and the application is exactly where it was.
            pendingClose_ = nullptr;
            if (panel_ != nullptr) {
                panel_->setStatus({});
            }
        }
    }
    if (closeGate_.takeReady()) {
        std::function<void()> action = std::move(pendingClose_);
        pendingClose_ = nullptr;
        if (action) {
            action();
        }
    }
}

void Application::saveProjectAsForGate() {
    window_->saveFileDialog(platform::Window::SaveKind::Project, [this](std::string chosen) {
        if (chosen.empty()) {
            closeGate_.saveFinished(false);
            return;
        }
        saveProjectTo(std::filesystem::path(chosen));
        closeGate_.saveFinished(!engine_->projectDirtyCached());
    });
}

// ---- the window title (ADR-440) ---------------------------------------------------------------
//
// A prompt is the last line of defence, not the only signal: an artist should be able to see that
// there is something to lose without being asked. The marker is a bullet rather than an asterisk
// because macOS uses a dot in the close button for the same fact.
void Application::refreshWindowTitle() {
    if (window_ == nullptr) {
        return;
    }
    std::string title = "avgen " + std::string(app::Engine::kAppVersion) + " - ";
    title += engine_->projectPath().empty() ? std::string("Untitled")
                                            : engine_->projectPath().filename().string();
    if (engine_->projectDirtyCached()) {
        title += " \u2022";
    }
    window_->setTitle(title);
}

// ---- what counts as touching the application (ADR-440) ----------------------------------------
//
// **Not "an input event arrived".** The drift the absorption exists to swallow -- hero positions,
// a world effect's parameter writeback -- happens while *time advances*, which is exactly when a
// film is playing. If moving the pointer across the window counted as a touch, then watching a
// project play with a hand on the mouse would attribute twenty paths of simulation to the user and
// the prompt would fire on a project nobody had edited. That is the failure the brief calls worse
// than having no prompt.
//
// So the signal is `ImGui::IsAnyItemActive()` -- a slider being dragged, a field being typed in, a
// menu item under the pointer -- plus the edit history, which is what a gizmo drag in the canvas
// produces. Pointer motion over the viewport is neither, and is not a change to the project.
//
// Read before `panel_->draw`, so it reports the state the *previous* frame's widgets left behind,
// which is the window this sample is asking about.
//
// Sampled no more often than ten times the cost of the last sample -- so the 675 KB project is
// checked about every 310 ms and a small one four times a second -- and never while a widget is
// active, so a 31 ms serialisation cannot land in the middle of a drag.
void Application::sampleProjectDirtyIfIdle() {
    const bool widgetActive = ImGui::IsAnyItemActive();
    if (widgetActive || edits_.dirty()) {
        touchedSinceDirtySample_ = true;
    }
    if (widgetActive) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const double waited = std::chrono::duration<double, std::milli>(now - lastDirtySample_).count();
    if (waited < std::max(250.0, lastDirtySampleMs_ * 10.0)) {
        return;
    }
    const bool wasDirty = engine_->projectDirtyCached();
    const auto started = std::chrono::steady_clock::now();
    if (std::getenv("AVGEN_DIRTY_TRACE") != nullptr) {
        log::info("DIRTYTRACE sample touched={} dirty={}", touchedSinceDirtySample_, wasDirty);
    }
    engine_->sampleProjectDirty(touchedSinceDirtySample_);
    lastDirtySampleMs_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    touchedSinceDirtySample_ = false;
    lastDirtySample_ = now;
    if (engine_->projectDirtyCached() != wasDirty) {
        refreshWindowTitle();
        // Said once per transition, because "the title grew a dot" is the kind of claim that is
        // true of the code and false of the running application (ADR-387), and this is the only
        // place the answer changes on its own.
        log::info("project has unsaved changes (sampled in {:.1f} ms)", lastDirtySampleMs_);
    }
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
    // Whatever happens below, the attribution window starts again here: a load resets the engine's
    // baseline, and the click that asked for the load is not an edit to what just arrived.
    touchedSinceDirtySample_ = false;
    lastDirtySample_ = std::chrono::steady_clock::now();
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
    } else {
        // Something added to the open project rather than replacing it -- audio, a scene, an
        // environment. It changed the project, so the next idle sample will find it and the title
        // will grow its marker; the title is refreshed here so the *name* is right immediately.
        refreshWindowTitle();
    }
}

const rendering::DebugViewOptions& Application::debugOptions() const {
    return panel_ != nullptr ? panel_->world.debug : cliDebug_;
}

Result<void> Application::applyDebugDraw() {
    if (options_.debugDraw.empty()) {
        return {};
    }
    rendering::DebugViewOptions& d = cliDebug_;
    const std::pair<std::string_view, bool*> known[] = {
        {"beams", &d.beams},
        {"entityOrigins", &d.entityOrigins},
        {"entityBounds", &d.entityBounds},
        {"entityIds", &d.entityIds},
        {"skeletons", &d.skeletons},
        {"worldAxes", &d.worldAxes},
        {"frustum", &d.frustum},
        {"transformTrail", &d.transformTrail},
        {"points", &d.points},
        {"bounds", &d.bounds},
        {"normals", &d.normals},
        {"splines", &d.splines},
        // The three labs' own overlays. Absent until now, which meant the switches the LOD and
        // Shadow Labs added were reachable only from an ImGui checkbox -- and a headless render is
        // the one context where a checkbox does not exist. An overlay a file cannot ask for is an
        // overlay nobody diagnosing from a rendered frame can use.
        //
        // (`culling` used to be named here as a deliberate omission -- a field nothing read, whose
        // command-line name would have been a promise the application does not keep. ADR-421
        // deleted the field: the same argument applies to the checkbox that DID exist for it, and
        // what it promised to draw is what the `lod` overlay already draws, in purple.)
        {"lod", &d.lod},
        {"shadowCascades", &d.shadowCascades},
        {"shadowCascadeSlices", &d.shadowCascadeSlices},
        {"shadowCasters", &d.shadowCasters},
        {"lights", &d.lights},
        {"lightClusters", &d.lightClusters},
        {"wind", &d.wind},          // ADR-382 section 19
        {"vortex", &d.vortex},
    };
    std::string enabled;
    std::stringstream stream(options_.debugDraw);
    std::string name;
    while (std::getline(stream, name, ',')) {
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        if (name.empty()) {
            continue;
        }
        bool found = false;
        for (const auto& [key, flag] : known) {
            if (key == name) {
                *flag = true;
                found = true;
                break;
            }
        }
        if (!found) {
            std::string all;
            for (const auto& [key, flag] : known) {
                all += (all.empty() ? "" : ", ") + std::string(key);
            }
            return fail("--debug-draw: unknown overlay '{}' (have: {})", name, all);
        }
        enabled += (enabled.empty() ? "" : ", ") + name;
    }
    // Printed, because an overlay that legitimately draws nothing -- no particles in the scene, no
    // entity selected -- is indistinguishable from one that was never switched on.
    log::info("--debug-draw: {}", enabled);
    return {};
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

void Application::serviceRenderPreview() {
    if (panel_ == nullptr) {
        return;
    }
    ui::ControlPanel::RenderFramePreview& view = panel_->renderPreview;
    // A new job -- or the same panel after the last one was released -- starts from nothing. The
    // alternative is the previous render's last frame sitting under a running progress bar as
    // though it were this render's first, which is the exact failure a preview exists to not have.
    //
    // Compared by `RenderJob::id()` and not by the pointer. Running a queue starts the next job in
    // the same UI frame the last one finished, inside the `if (complete)` branch and *after* this
    // function has already recorded the old job -- so the address can be reused and the comparison
    // below would say "same job" about a different render.
    const std::uint64_t id = job_ != nullptr ? job_->id() : 0;
    if (id != renderPreviewJobId_) {
        renderPreviewJobId_ = id;
        if (job_ != nullptr) {
            view.width = 0;
            view.height = 0;
            view.index = 0;
            view.hash = 0;
            view.dropped = 0;
        }
    }
    view.live = job_ != nullptr;
    if (job_ == nullptr) {
        return;
    }
    // The toggle is pushed every frame rather than on its edge: the panel writes the bool straight
    // through (there is no hook to set a dirty bit in), and a render started while it was off has
    // to pick it up when it is turned on mid-render.
    job_->setPreviewEnabled(view.enabled);
    if (!view.enabled || !job_->takePreview(renderPreviewFrame_)) {
        return;
    }
    const RenderJob::FramePreview& f = renderPreviewFrame_;
    if (!f.valid() || f.width > kRenderPreviewAtlas || f.height > kRenderPreviewAtlas) {
        return;
    }
    if (!renderPreviewTexture_) {
        wgpu::TextureDescriptor desc{};
        desc.label = "render-frame-preview";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {kRenderPreviewAtlas, kRenderPreviewAtlas, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        renderPreviewTexture_ = context_->device().CreateTexture(&desc);
        if (!renderPreviewTexture_) {
            log::warn("render preview: cannot create the {0}x{0} preview texture; the Render panel "
                      "will not show frames", kRenderPreviewAtlas);
            view.enabled = false;
            return;
        }
        renderPreviewView_ = renderPreviewTexture_.CreateView();
    }
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = renderPreviewTexture_;
    dst.origin = {0, 0, 0};
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = f.width * 4;
    layout.rowsPerImage = f.height;
    const wgpu::Extent3D extent{f.width, f.height, 1};
    context_->queue().WriteTexture(&dst, f.rgba.data(), f.rgba.size(), &layout, &extent);
    view.texture = reinterpret_cast<std::uint64_t>(renderPreviewView_.Get());
    view.u1 = static_cast<float>(f.width) / static_cast<float>(kRenderPreviewAtlas);
    view.v1 = static_cast<float>(f.height) / static_cast<float>(kRenderPreviewAtlas);
    view.width = f.width;
    view.height = f.height;
    view.sourceWidth = f.sourceWidth;
    view.sourceHeight = f.sourceHeight;
    view.index = f.index;
    view.hash = f.hash;
    view.linearSource = f.linearSource;
    view.dropped = job_->previewDropped();
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
    engine_->pathTraceSettings() = uiPathTrace_;
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

// The one way a project reaches the disk from the editor. Cmd-S, Save As and the unsaved-changes
// modal's Yes all come through here, so "what counts as a successful save" cannot have two answers
// (ADR-440) -- and `Engine::saveProject` is what clears the dirty state, so the gate reading
// `projectDirtyCached()` afterwards is reading the save's own verdict rather than a second one.
void Application::saveProjectTo(const std::filesystem::path& path) {
    storeOutputsToProject();
    if (auto r = engine_->saveProject(path); !r) {
        log::error("save project: {}", r.error().message);
        if (panel_ != nullptr) {
            panel_->setStatus(r.error().message);
        }
        return;
    }
    if (panel_ != nullptr) {
        panel_->setStatus("saved " + path.filename().string());
    }
    // The edit history now describes the document on disk. Until this line `EditSystem::markSaved`
    // had no production caller at all -- a writer with no writer, the shape ADR-225 is about. It is
    // not what the prompt reads (the document comparison is), but leaving it permanently unset
    // would make `EditSystem::dirty()` answer a question it has the data for and get it wrong.
    edits_.markSaved();
    rememberProject(path);
    // The baseline has just moved, so the window this attributes over starts here. Without this,
    // the click that asked for the save would be attributed to whatever the engine wrote next.
    touchedSinceDirtySample_ = false;
    lastDirtySample_ = std::chrono::steady_clock::now();
}

void Application::rememberProject(const std::filesystem::path& path) {
    // The Render panel edits `uiRender_`, and `uiRender_` was copied from the engine exactly once,
    // in `init()` -- which runs *before* a `--project` on the command line is even loaded. So the
    // panel has never shown a loaded project's render settings: it shows the start-up defaults, and
    // because saving writes the panel's copy back over the project, every save replaced whatever
    // the project had with those defaults. A `supersample` set in the file was silently lost twice
    // in one afternoon that way, which is how this was found.
    //
    // Refreshed here, where a project has just *become* the current one -- the same hook the recent
    // list and the window title use. The panel's pointer is to `uiRender_` itself and stays valid.
    uiRender_ = engine_->renderSettings();
    uiPathTrace_ = engine_->pathTraceSettings();   // ADR-366, and for the same reason
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
    refreshWindowTitle();
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

// ---- what the viewport is looking through (ADR-391) ---------------------------------------------
//
// The user's choice, narrowed by what the canvas is currently being used for. Two things overrule
// it, and both are cases where the person is looking at the deliverable rather than at the world:
//
//   * **A preview mode that shows the output frame.** Workspace is the flexible canvas -- the
//     editor's window on the world -- and Output Frame and Preview are the picture that renders.
//     An editor viewpoint inside them would be a lie about what the render contains, which is the
//     one thing those modes exist to tell the truth about. So: Workspace navigates, Output Frame
//     composes, and a drag in Output Frame still moves the film's camera exactly as it used to.
//   * **An open output or share.** `presentAll` and `TextureShare::publish` are handed
//     `finalTexture_` -- the live viewport's own render target -- so whatever the canvas is showing
//     is on the projector or in the Syphon feed. While anything is watching, the canvas shows the
//     film. Flying the editor viewpoint without disturbing a live output needs a second render of
//     the film per frame, which is real GPU cost and a separate piece of work; what must not happen
//     is that it *looks* like it works and quietly re-frames somebody's show.
//
// A scene with no composition has no editor viewpoint to offer (the override lives on
// `Composition`), and answers Film -- which is what those scenes have always done.
scene::ViewportView Application::effectiveViewportView() const {
    if (engine_ == nullptr || engine_->composition() == nullptr) {
        return {};
    }
    if (ui::viewportShowsFilm(viewportView_.showsFilm(),
                              panel_ != nullptr && ui::modeShowsOutputFrame(panel_->preview.mode),
                              outputs_.openCount() > 0 || share_.isOpen(),
                              /*hasComposition=*/true)) {
        return {};
    }
    return viewportView_;
}

void Application::applyViewportView() {
    if (engine_ == nullptr) {
        return;
    }
    if (scene::Composition* comp = engine_->composition()) {
        comp->setViewportView(effectiveViewportView());
    }
}

// ---- the viewport's pose, wherever it lives -----------------------------------------------------
//
// One seam, two destinations. Which one is chosen is `effectiveViewportView`'s answer and not the
// caller's business: a drag, the wheel and frame-selected all say "here is where the view should
// be now" and none of them should have to know whether that is an edit to the film.
namespace {
// The editor's own viewpoint, when there is one to move. Null in Film mode, over a scene with no
// composition, or while an output is watching -- in which case the pose lives in `camera/*`.
[[nodiscard]] scene::Composition* editorViewpointOwner(Engine* engine, const scene::ViewportView& view) {
    if (engine == nullptr || view.showsFilm()) {
        return nullptr;
    }
    return engine->composition();
}
} // namespace

CameraPose Application::viewportPose() const {
    CameraPose pose;
    if (engine_ == nullptr) {
        return pose;
    }
    const scene::ViewportView view = effectiveViewportView();
    if (const scene::Composition* comp = engine_->composition(); comp != nullptr && !view.showsFilm()) {
        // The editor's pose, or -- if it has not been seeded yet, or the viewport is pinned to an
        // authored rig -- the frame that is actually on screen. Reading the live frame rather than
        // a default is what makes the first drag after a mode change continue from what you were
        // looking at instead of jumping to wherever the unseeded pose happened to be.
        if (view.mode == scene::ViewportCamera::Editor && comp->editorCameraSeeded()) {
            pose.eye = comp->editorCamera().position;
            pose.target = comp->editorCamera().target;
        } else {
            pose.eye = engine_->scene().camera.position;
            pose.target = engine_->scene().camera.target;
        }
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
    const scene::ViewportView view = effectiveViewportView();
    if (scene::Composition* comp = editorViewpointOwner(engine_.get(), view)) {
        // **Not `camera/*`.** This is the whole of ADR-391 in three lines: a navigation gesture
        // writes a pose the film does not own, so there is nothing for it to destroy and nothing
        // for a save to photograph. Moving the view while pinned to a rig takes the viewport off
        // that rig and onto its own viewpoint, starting from the rig's frame -- a person who drags
        // has stopped looking *through* something and started looking *around*.
        scene::CameraPose next = comp->editorCamera();
        next.position = pose.eye;
        next.target = pose.target;
        if (view.mode == scene::ViewportCamera::Through) {
            next.fovDegrees = glm::degrees(engine_->scene().camera.fovYRadians);
            next.focalLength = 0.0f;
            viewportView_.mode = scene::ViewportCamera::Editor;
            viewportView_.camera = scene::kNoCamera;
            if (panel_ != nullptr) {
                panel_->setStatus("the editor viewpoint took over from the camera you were looking "
                                  "through -- the film is unchanged");
            }
        } else if (!comp->editorCameraSeeded()) {
            next.fovDegrees = glm::degrees(engine_->scene().camera.fovYRadians);
        }
        comp->setEditorCamera(next);
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

void Application::ensureFreeCamera(bool deliberate) {
    if (engine_ == nullptr) {
        return;
    }
    // **ADR-391: off the path of an ordinary drag.**
    //
    // Everything below this guard is about taking the film's camera away from whoever is driving
    // it, and that is only what a gesture means when the frame on screen *is* the film's. When the
    // viewport is showing its own viewpoint, a drag moves a pose the film does not own: there is no
    // director to stand down, no track to remove, nothing to lose and so nothing to defend. The
    // lock ADR-386 added is still here and still correct -- it just is not in the way of navigating
    // any more, which was its entire cost.
    if (!effectiveViewportView().showsFilm()) {
        return;
    }
    // The lock, and it is FIRST, before anything below mutates.
    //
    // It sat after the free-roam block to begin with, which was a half-applied state: a locked drag
    // still copied the live pose onto `camera/position`'s base, then
    // refused to stand the director down and told the user nothing had happened. A refusal that
    // changes the project anyway is worse than no refusal, because now the message is wrong too.
    //
    // So a locked, incidental gesture is a complete no-op plus a sentence saying how to ask
    // properly. What the brief's §7 called the bargain -- "while the cut is locked, the viewport
    // cannot move the camera at all" -- is no longer the price of it, because there IS a separate
    // editor camera now (ADR-391) and the guard above sends every ordinary gesture to it. All this
    // refuses is the narrower thing it always meant to: moving the *film's* camera by hand, on a
    // project whose cut is baked, without saying so.
    //
    // And it still asks what would actually be lost rather than assuming there is something. The two
    // halves are independent: ADR-391 narrowed WHICH gestures reach this refusal, and the bake count
    // decides whether the refusal is worth making at all. On the Tree of Life -- 0 camera tracks, 0
    // aim-follow entries, 0 shot spans -- there is nothing to defend and the answer is yes either way.
    if (cameraDirection_.directed &&
        !ui::viewportMayReleaseDirector(true, cameraLocked_, deliberate,
                                        directedCameraBakeSize(*engine_) > 0)) {
        if (panel_ != nullptr) {
            panel_->setStatus("this frame is the film's and its cut is baked. Switch the canvas to "
                              "the editor viewpoint to fly without touching it, or unlock in the "
                              "Cameras panel to edit the cut (that discards it).");
        }
        if (!cameraLockAnnounced_) {
            cameraLockAnnounced_ = true;
            log::info("viewport: camera locked; a drag will not stand the director down "
                      "({} aim-follow, {} shot span(s) protected)",
                      engine_->composition() != nullptr ? engine_->composition()->aimFollow().size() : 0,
                      engine_->shotSpans().size());
        }
        return;
    }
    // **Stand the director's camera down, if one of its rigs has the frame.**
    //
    // This function's original job was to stop the *timeline* replacing `camera/position` on the
    // next frame -- correct when there was one camera and the only way to be "directed" was to have
    // tracks baked onto it. Under ADR-245 a shot can put an authored *rig* on screen instead, and
    // then removing the main camera's tracks achieves nothing at all: the viewport's drag moves a
    // camera that is not the one being displayed, so the mouse appears dead and there is no gesture
    // that recovers. Reported as "there is no reliable way to leave the director camera and freely
    // navigate the world", which is exactly right.
    //
    // Reaching for the camera is asking for it back, and now that means both halves.
    //
    // The free-roam flag that used to be set here is gone (ADR-391): it pinned the frame to the
    // MAIN camera, and the main camera *is* `camera/*`, so it never stopped a gesture writing the
    // film -- it only chose which of the film's cameras was written. What it did that is still
    // worth doing is the pose copy: the main camera picks up where the shot left off, so handing
    // the camera back does not teleport the frame. `releaseDirectedCamera` below removes the
    // director's shots, so the film resolves to the main camera on the next frame by itself.
    if (scene::Composition* comp = engine_->composition();
        comp != nullptr && comp->activeCamera().camera != scene::kMainCamera) {
        const scene::Camera& live = engine_->scene().camera;
        if (auto* p = engine_->params().find("camera/position")) {
            for (std::size_t c = 0; c < 3; ++c) {
                p->setBaseComponent(c, live.position[c]);
            }
        }
        if (auto* t = engine_->params().find("camera/target")) {
            for (std::size_t c = 0; c < 3; ++c) {
                t->setBaseComponent(c, live.target[c]);
            }
        }
        log::info("viewport: taking the film's camera over from '{}'", comp->activeCamera().name);
        if (panel_ != nullptr) {
            panel_->setStatus("the film's camera is yours -- the director's cut has been discarded");
        }
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
            // TEMPORARY (ui-responsiveness phase 2): every event, not only the motions, and the
            // oldest as well as the newest. A timeline click is a BUTTON_DOWN, so the existing
            // `input->present ms` -- motion only -- cannot see the interaction this phase is about.
            ++probe2::frame().inputEvents;
            std::uint64_t eventNs = 0;
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                eventNs = event.motion.timestamp;
                ++probe2::frame().pointerEvents;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                eventNs = event.button.timestamp;
                ++probe2::frame().pointerEvents;
            } else {
                eventNs = event.common.timestamp;
            }
            probe2::frame().note(eventNs);
            // T0 and T1 for the interaction log. SDL stamps events on its own `SDL_GetTicksNS`
            // clock and every stage downstream is on `steady_clock`; rather than carry two
            // timebases, the event's *age* is measured in SDL's clock and subtracted from the
            // handler's own `steady_clock` reading. The two clocks need only agree on durations,
            // which they do, and not on epochs, which they do not.
            if (eventNs != 0) {
                const core::Stamp receipt = core::Clock::now();
                const std::uint64_t nowNs = SDL_GetTicksNS();
                const std::uint64_t ageNs = nowNs > eventNs ? nowNs - eventNs : 0;
                core::interactions().noteInput(
                    receipt - std::chrono::duration_cast<core::Clock::duration>(
                                  std::chrono::nanoseconds(ageNs)),
                    receipt);
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
            // The transport's arrow keys never reach Dear ImGui (ADR-357 gave them the playhead;
            // `NavEnableKeyboard` gives the same keys the focus ring). Deciding here rather than
            // after `handleTransportShortcut` is the whole point: forwarding first and consuming
            // second is what let the playhead step *and* the focus ring walk the transport's own
            // buttons on one press. `ui::transportOwnsArrowKey` carries the reasoning and the three
            // cases where ImGui keeps them.
            //
            // KEY_DOWN only, and KEY_UP always forwarded. If ImGui saw the press -- because a text
            // field had focus at the time -- and the field then deactivated mid-press, swallowing
            // the release would leave ImGui believing the key is still held, and nav would repeat
            // forever. A release for a press it never saw is harmless; the reverse is a stuck key.
            const bool arrowForTransport =
                event.type == SDL_EVENT_KEY_DOWN &&
                ui::transportOwnsArrowKey(event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT ||
                                              event.key.key == SDLK_UP || event.key.key == SDLK_DOWN,
                                          (SDL_GetModState() & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0,
                                          imgui_->wantsTextInput(), imgui_->itemActive(),
                                          imgui_->popupOpen());
            if (!arrowForTransport) {
                imgui_->processEvent(event);
            }
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

// The transport's keyboard (ADR-102, and ADR-357 for the arrows). The set every editing tool uses:
// space plays, the arrows step the playhead, home and end go to the ends of the piece.
//
// **The arrows step by whatever the sequencer is snapped to, and Shift is the next unit up.** They
// used to step a frame plain and a beat with Shift, fixed, whatever grid the strip was on -- so a
// person working in beats got frames from the key they were pressing most and a person working in
// frames got beats from the other one. `ui::arrowNudge` holds the table; this reads it.
//
// Global rather than scoped to a focused Sequence panel, deliberately. The arrows already moved the
// playhead from anywhere and the playhead is one object; scoping the *distance* to which window had
// focus would make the same key travel different amounts depending on where you last clicked, and a
// second handler on the panel would have to fight this one for the event or move the playhead twice.
// What is panel-scoped is the *setting* it reads, which is where the setting lives anyway.
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
        nudgePlayhead(-1, shift);
        return true;
    case SDLK_RIGHT:
        nudgePlayhead(1, shift);
        return true;
    case SDLK_UP:
        // Up and Down stay marker jumps at every snap mode. They are the "go to the next place" keys
        // and always were; folding them into the grid would leave the piece with no way to jump
        // between its landmarks whenever somebody was working in frames.
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

// One press of Left or Right (ADR-357). `direction` is -1 or +1; `coarse` is Shift.
//
// The decision is `ui::arrowNudge`'s and the movement is the transport's existing stepping, which is
// the split that makes this testable: walking a beat grid is `Engine::beatBoundary`'s job and it
// already falls back from the analysed beats to the tempo, which no pure function handed a vector of
// beat times could reproduce. So the table lives where a test can read it and the walking stays
// where it works.
void Application::nudgePlayhead(int direction, bool coarse) {
    if (engine_ == nullptr) {
        return;
    }
    // The sequencer's grid, if there is a sequencer. Without one the piece has no strip and no snap
    // setting, and Frames is the honest default: it is what the transport did before any of this.
    const int snapMode = panel_ != nullptr ? panel_->sequence.snapMode()
                                           : static_cast<int>(seq::SnapMode::Frames);
    const double viewSpan = panel_ != nullptr ? panel_->sequence.visibleSpanSeconds() : 0.0;
    // `beatsPerBar` from the bake options rather than a literal 4, so the day time-signature
    // detection lands there is one default to change and this follows it.
    const ui::Nudge nudge =
        ui::arrowNudge(snapMode, direction, coarse, engine_->renderSettings().fps, viewSpan,
                       seq::BakeOptions{}.beatsPerBar);
    switch (nudge.unit) {
    case ui::NudgeUnit::Frames:
        engine_->stepFrames(nudge.count);
        break;
    case ui::NudgeUnit::Beats:
        engine_->stepBeats(nudge.count);
        break;
    case ui::NudgeUnit::Markers:
        engine_->stepMarkers(nudge.count, nudge.sectionsOnly);
        break;
    case ui::NudgeUnit::Seconds:
        // The only arm that computes a time rather than asking for a step, so it is the only one
        // that has to clamp for itself. `seekSeconds` clamps again through `Transport::seek`; this
        // is here so the arm with no grid is clamped by something a test can reach without an engine.
        engine_->seekSeconds(ui::nudgedTime(engine_->transport().positionSeconds(), nudge.seconds,
                                            engine_->durationSeconds()));
        break;
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
        // E is Rotate, unconditionally, and it used to depend on the selection: with nothing
        // selected it fell through to the File menu's "open environment" and put up an HDR file
        // dialog. That was a coin-toss binding -- the same key did two unrelated things depending
        // on hidden state -- and making lights and cameras selectable would have made the coin land
        // differently far more often. W/E/R/Q are the tool keys the viewport spec names, and a tool
        // key that sometimes opens a file browser is not a tool key.
        //
        // Opening an environment keeps its File menu item, which is where a file dialog belongs.
        editor.gizmoMode = ui::GizmoMode::Rotate;
        editor.mode = ui::EditorMode::Select;
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
    // The arrows nudge only while the pointer is over the viewport; otherwise they fall through to
    // the transport. See `ui::editorOwnsArrowKey` for why a selection alone is not enough -- making
    // lights selectable would otherwise have stopped the arrows scrubbing across the whole
    // application for as long as a light was selected.
    case SDLK_UP:
    case SDLK_DOWN:
    case SDLK_LEFT:
    case SDLK_RIGHT: {
        if (!ui::editorOwnsArrowKey(!editor.selection.empty(), canvas_.hovered)) {
            return false;
        }
        const auto pressed = event.key.key;
        const glm::vec3 delta = pressed == SDLK_UP     ? glm::vec3(0.0f, 0.0f, -step)
                                : pressed == SDLK_DOWN ? glm::vec3(0.0f, 0.0f, step)
                                : pressed == SDLK_LEFT ? glm::vec3(-step, 0.0f, 0.0f)
                                                       : glm::vec3(step, 0.0f, 0.0f);
        editor.nudgeSelection(*engine_, delta);
        return true;
    }
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

    // Editor helpers first, on the CPU, in screen space -- before the GPU readback and regardless of
    // what geometry is behind them.
    //
    // A light has no geometry, so it is in no identifier target and the readback below can never
    // find one. See `ui::pickProjectedPoint` for why this is screen space rather than a fifth
    // `PickSpace`: the tag has one free value and two claimants, and a helper written into the
    // identifier target would be in a pass the offline renderer runs.
    //
    // Before the readback and not after, because a helper drawn on top of the world should be
    // picked as though it is on top of the world. Clicking the star over a tree selects the light,
    // not the tree; clicking two centimetres away selects the tree.
    if (panel_ != nullptr && engine_->composition() != nullptr) {
        const scene::Composition& comp = *engine_->composition();
        std::vector<glm::vec3> positions;
        std::vector<std::string> names;
        for (const scene::Composition::AuthoredLight& a : comp.authoredLights()) {
            positions.push_back(a.light.position);
            names.push_back(a.light.name);
        }
        const glm::vec2 ndc(
            (static_cast<float>(viewportPickPixel_.x) / static_cast<float>(std::max(pw, 1u))) * 2.0f - 1.0f,
            1.0f - (static_cast<float>(viewportPickPixel_.y) / static_cast<float>(std::max(ph, 1u))) * 2.0f);
        const int hit =
            ui::pickProjectedPoint(positions, camera, aspect, ndc, ui::kHelperPickRadius);
        if (hit >= 0) {
            const ui::SelectionRef ref{ui::SelectionRef::Kind::Light, names[static_cast<std::size_t>(hit)]};
            if (viewportPickAdditive_) {
                panel_->editor.selection.toggle(ref);
            } else {
                panel_->editor.selection.set(ref);
            }
            return; // the light took the click
        }

        // Cameras, the same way and after the lights: where a camera body and a light marker
        // overlap the light wins, because a light is the thing a person is usually reaching for
        // while composing and a camera can also be chosen from its own panel.
        std::vector<glm::vec3> cameraPositions;
        std::vector<const scene::CameraRig*> rigs;
        for (const scene::CameraRig& rig : comp.cameraDirection().cameras) {
            cameraPositions.push_back(rig.position);
            rigs.push_back(&rig);
        }
        const int cameraHit =
            ui::pickProjectedPoint(cameraPositions, camera, aspect, ndc, ui::kHelperPickRadius);
        if (cameraHit >= 0) {
            const scene::CameraRig& rig = *rigs[static_cast<std::size_t>(cameraHit)];
            const ui::SelectionRef ref{ui::SelectionRef::Kind::Camera, rig.name};
            if (viewportPickAdditive_) {
                panel_->editor.selection.toggle(ref);
            } else {
                panel_->editor.selection.set(ref);
            }
            // Canvas -> panel, the other half of §4's "selection must remain synchronized in both
            // directions". The panel's own selection was a private id with no observer, which is
            // the same shape as the sequencer's selection having no reader anywhere in the tree.
            panel_->selectCamera(rig.id);
            return; // the camera took the click
        }
    }

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
        if (key == "mode") {
            // Through the same table the panel and the project file use, so one setting has one
            // spelling however it arrives.
            const auto parsed = directorModeFromName(value);
            if (!parsed) {
                return fail("--director: mode must be 'continuous', 'edited' or 'song', got '{}'",
                            value);
            }
            s.mode = *parsed;
            continue;
        }
        if (key == "autonomy") {
            const auto parsed = autonomyFromName(value);
            if (!parsed) {
                return fail("--director: autonomy must be 'locked', 'guided' or 'expressive', got "
                            "'{}'",
                            value);
            }
            s.autonomy = *parsed;
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
        } else if (key == "maxSpeed") {
            s.maxCameraSpeed = static_cast<float>(v);
        } else if (key == "maxSwing") {
            s.maxViewRate = static_cast<float>(v);
        } else if (key == "dwell") {
            s.dwellShots = static_cast<int>(v);
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
    // ADR-249. Loaded here rather than at start-up because a plan belongs to the piece that is
    // loaded, and `--project` has already run by the time this is called -- so a plan on the command
    // line replaces the project's rather than being replaced by it, which is the way round a person
    // typing one expects.
    if (options_.songPlan) {
        std::ifstream in(*options_.songPlan);
        if (!in) {
            return fail("--song-plan: cannot open {}", options_.songPlan->string());
        }
        nlohmann::json doc;
        try {
            in >> doc;
        } catch (const std::exception& e) {
            return fail("--song-plan {}: {}", options_.songPlan->string(), e.what());
        }
        auto plan = songPlanFromJson(doc);
        if (!plan) {
            return std::unexpected(plan.error());
        }
        log::info("song plan: {} section(s) from {}", plan->sections.size(),
                  options_.songPlan->string());
        engine_->songPlan() = std::move(*plan);
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
    if (options_.saveScene) {
        if (auto r = engine_->saveComposition(*options_.saveScene); !r) {
            log::error("save scene: {}", r.error().message);
            return code == 0 ? 6 : code;
        }
        log::info("scene saved to {}", options_.saveScene->string());
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
    // The interactive seek, out of `ui.build` and into a phase of its own. A cost attributed to the
    // phase that pays it is a cost somebody can budget; one hidden inside the UI build is a cost
    // that gets described as "the UI is slow".
    const int kPhSeekService = prof.phase("seek.service");
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
    // Structural counters: what the frame *caused*, not how long the machine took to do it. ADR-170
    // is the reason these are here -- a phantom 3.8x regression was nearly filed off timings while
    // the structural counters moved 3.6%, and it was the counters that indicted the machine. A
    // regeneration count cannot be inflated by somebody else's build.
    // TEMPORARY (ui-responsiveness phase 2). These are the phases the investigation added; they
    // are pure measurement and none of them is read by anything that decides what the frame does.
    const int kPhSeek = prof.phase("engine.seek ms");
    const int kPhEntitySeek = prof.phase("  entity re-sim ms");
    const int kPhSeekCount = prof.phase("# seeks/frame");
    const int kPhSimBodies = prof.phase("# resim ksteps");
    const int kPhCatchup = prof.phase("analysis.catchup ms");
    const int kPhUpdControl = prof.phase("upd.control ms");
    const int kPhUpdSignals = prof.phase("upd.signals ms");
    const int kPhUpdMod = prof.phase("upd.modulation ms");
    const int kPhUpdCtrl = prof.phase("upd.controller ms");
    const int kPhUpdOther = prof.phase("upd.other ms");
    const int kPhUiAck = prof.phase("input->ui.build ms");
    const int kPhOldestAck = prof.phase("input oldest->pres");
    const int kPhInputEvents = prof.phase("# input events");
    const int kPhGpuFrame = prof.phase("gpu.frame ms (GPU)");
    // TEMPORARY (ui-responsiveness phase 2): the renderer's own CPU stage breakdown, surfaced into
    // the frame profiler so a `render.record` spike can be attributed to a stage instead of guessed.
    const int kPhRrUploads = prof.phase("rr.uploads ms");
    const int kPhRrProc = prof.phase("rr.procedural ms");
    const int kPhRrObjects = prof.phase("rr.objects ms");
    const int kPhRrFields = prof.phase("rr.fields ms");
    const int kPhRrSdf = prof.phase("rr.sdf ms");
    const int kPhRrLights = prof.phase("rr.lights ms");
    const int kPhMeshUp = prof.phase("mesh upload ms");
    const int kPhTexUp = prof.phase("texture upload ms");
    const int kPhTexCount = prof.phase("# textures uploaded");
    const int kPhEnvMs = prof.phase("environment ms");
    const int kPhMeshCount = prof.phase("# meshes uploaded");
    const int kPhProcGen = prof.phase("# procedural regen");
    const int kPhFlatten = prof.phase("# scene flattens");
    const int kPhEnvBuild = prof.phase("# IBL builds");
    std::uint64_t lastProcGen = scene::proceduralRebuildCount();
    std::uint64_t lastEnvBuild = rendering::environmentBuildCount();
    // Seeded from the composition as it stands *after* the load, so the first frame reports the
    // flattens that frame caused rather than the ones opening the project did.
    std::uint64_t lastFlatten =
        engine_->composition() != nullptr ? engine_->composition()->flattenCount() : 0;
    // The interaction log keeps its own marks rather than sharing the profiler's: the profiler's are
    // advanced inside a block that is skipped when there is no composition, and a counter that
    // silently stops advancing attributes the next real flatten to the wrong interaction.
    std::uint64_t interactionFlattenMark_ = lastFlatten;
    std::uint64_t interactionProcGenMark_ = lastProcGen;
    for (const auto& [kindName, ms] : options_.latencyInject) {
        if (const auto kind = core::interactionFromName(kindName)) {
            core::setInjectedDelay(*kind, ms);
            log::info("latency: injecting {:.1f} ms into every '{}' (ADR-182 control; these "
                      "records are excluded from the distributions)",
                      ms, kindName);
        }
    }
    if (auto arms = parseUiScript(options_.uiScript)) {
        uiScript_ = UiScript(*arms);
    }
    if (uiScript_.active()) {
        log::info("ui-script: '{}' driving the editor", options_.uiScript);
    }
    // ADR-233, live only: above two milliseconds a sky rebuild waits for the drag to stop. The
    // same number and the same reasoning as Engine::installController's procedural budget --
    // comfortably below a frame's share, comfortably above anything worth deferring. `runHeadless`
    // never calls this, so an offline render rebuilds whenever the hash moves, as it always did.
    renderer_->setInteractiveEnvironmentBudget(2.0);
    // ---- the interleaved arms (--ui-ab) ---------------------------------------------------------
    //
    // One process, several interactions, cycled in blocks. The first `uiAbSettle` frames of a block
    // are labelled `kNoGroup` and excluded: an arm must not be charged for the deferral its
    // predecessor left running, nor credited with a pipeline its predecessor already compiled.
    const std::vector<std::string> abArms = splitList(options_.uiAb, ':');
    std::vector<std::string> abEditLog;
    const bool abRunning = !abArms.empty();
    std::vector<int> abGroups;
    if (abRunning) {
        if (abArms.size() > core::PhaseProfiler::kMaxGroups) {
            // Said, not swallowed. A run asked for nine arms and reported eight, and the missing
            // one was the arm the question was about -- a truncation nobody is told about is a
            // measurement of something other than what was asked for.
            log::warn("ui-ab: {} arms asked for, {} is the maximum; the last {} will not run",
                      abArms.size(), core::PhaseProfiler::kMaxGroups,
                      abArms.size() - core::PhaseProfiler::kMaxGroups);
        }
        for (std::size_t a = 0; a < abArms.size() && a < core::PhaseProfiler::kMaxGroups; ++a) {
            prof.nameGroup(static_cast<int>(a), abArms[a]);
            abGroups.push_back(static_cast<int>(a));
        }
        log::info("ui-ab: {} arm(s) x {} block(s) of {} frames ({} settling), interleaved in this process",
                  abGroups.size(), options_.uiAbBlocks, options_.uiAbFrames, options_.uiAbSettle);
    }

    for (;;) {
        const auto frameStart = std::chrono::steady_clock::now();
        prof.beginFrame();
        if (abRunning) {
            const int perBlock = std::max(1, options_.uiAbFrames);
            const auto arms = static_cast<int>(abGroups.size());
            const int index = framesRendered / perBlock;
            const int withinBlock = framesRendered % perBlock;
            if (index >= arms * std::max(1, options_.uiAbBlocks)) {
                break; // every arm has had every block; the report is written on the way out
            }
            const int arm = index % arms;
            if (withinBlock == 0) {
                // Whatever the outgoing arm was holding, it is not holding it now.
                releaseScriptedPointer(*window_);
                const UiAbArm spec = parseUiAbArm(abArms[static_cast<std::size_t>(arm)]);
                // What the outgoing arm proved about itself, before the script that holds it is
                // replaced. Without this the probes an arm prints -- "the box selected 9 of 16",
                // "the handle under the pointer was AxisX" -- die at the first switch, and every
                // `--ui-ab` column would be a frame time with nothing saying the arm did anything
                // at all (ADR-182).
                for (const std::string& line : uiScript_.editLog()) {
                    abEditLog.push_back(line);
                }
                if (auto parsed = parseUiScript(spec.script)) {
                    uiScript_ = UiScript(*parsed);
                }
                if (auto* comp = engine_->composition()) {
                    comp->setLegacyProceduralGeneration(spec.legacyProcGen);
                }
                renderer_->setInteractiveEnvironmentBudget(spec.eagerSky ? 0.0 : 2.0);
                // The seek deferral's before and after, as two blocks of one process. A shell loop
                // over two builds would compare two runs, which §3 of
                // docs/application-performance.md forbids, and on a machine whose load average
                // moved from 6.7 to 51.0 during the previous measurement that is a practical
                // constraint rather than a pedantic one.
                engine_->setInteractiveSeekBudget(spec.eagerSeek ? 0.0 : 2.0);
            }
            const int group = withinBlock < options_.uiAbSettle
                                  ? core::PhaseProfiler::kNoGroup
                                  : abGroups[static_cast<std::size_t>(arm)];
            prof.setFrameGroup(group);
            // The same label on the interaction records, so an A/B's two halves stay two halves.
            // The settling frames belong to no arm here for the same reason they belong to none
            // there: an arm inherits its predecessor's deferral timers, and this change is *about*
            // a deferral timer.
            core::interactions().setGroup(group);
        }
        probe2::frame().clear(); // TEMPORARY: ui-responsiveness phase 2
        // The interaction log's frame boundary. Clearing the input slot here is what stops an
        // interaction opened on a quiet frame inheriting the previous frame's event and
        // reporting a latency that spans a gesture nobody made.
        core::interactions().beginFrame(framesRendered);
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
        // ADR-440: quit is a close like any other. The `break` is deferred until the modal has
        // been answered, which needs frames to draw -- so the event raises the prompt and
        // `quitting_` carries the answer back here. A project with nothing to lose still leaves on
        // this line, because `requestClose` runs the action immediately when it is not dirty.
        if (events.quit) {
            requestClose(ui::CloseIntent::Quit, [this] { quitting_ = true; });
        }
        if (quitting_) {
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
            // The stress driver stands in for a user and reaches the engine directly, so it has to
            // say so: without this its edits are indistinguishable from the engine's own writeback
            // and are absorbed. Found by running it and watching nothing happen (ADR-387).
            touchedSinceDirtySample_ = true;
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
        // ---- the output preview (ADR-246) ----------------------------------------------------
        //
        // The one substitution the whole feature is. When the canvas is showing the output frame,
        // the extent comes from the preview's geometry instead of from the canvas's -- and because
        // that extent carries the *output's* aspect ratio, everything downstream follows: the
        // renderer's HDR target, `Camera::projection(aspect)`, the froxel grid, the terrain LOD's
        // frustum and the pixel a click resolves to. The picture in the frame is not an
        // approximation of the deliverable's framing; it is the deliverable's framing, produced by
        // the same code from the same camera at a different number of samples.
        //
        // The editor's own render scale is deliberately *not* applied on top. `canvasRenderScale`
        // is "soften the workspace to keep the frame rate up" (ADR-084); the preview's own quality
        // rung is the lever here, and two multiplying scales would make the number the toolbar
        // reports a lie.
        const ui::PreviewRender previewExtent =
            panel_ != nullptr ? panel_->previewRender() : ui::PreviewRender{};
        const bool previewFramed = previewExtent.width > 0 && previewExtent.height > 0;
        if (previewFramed) {
            cw = previewExtent.width;
            ch = previewExtent.height;
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
        // The preview's view state, when it has moved and the throttle is due. Same shape and same
        // reason as `ControlPanel::serviceLayoutStore`: ImGui writes these flags straight through,
        // so there is no hook to set a dirty bit in and the honest thing is to compare.
        if (panel_ != nullptr && !settingsPath_.empty()) {
            const bool moved = !(panel_->preview == settings_.preview) ||
                               (!options_.renderPreview &&
                                panel_->renderPreview.enabled != settings_.renderFramePreview);
            const double now = ImGui::GetTime();
            if (moved && now - lastPreviewSave_ > 2.0) {
                lastPreviewSave_ = now;
                saveSettings();
            }
        }
        if (panel_ != nullptr) {
            panel_->canvasTexture = reinterpret_cast<std::uint64_t>(finalView_.Get());
            panel_->environmentBehind = renderer_->environmentAwaitingRebuild();
            // The resolution the toolbar validates a custom size against is the adapter's, not a
            // constant (output-preview spec §5.3).
            panel_->maxTextureDimension = context_->capabilities().limits.maxTextureDimension2D;
            // The target has been asked for but not yet built: the settle above holds a new extent
            // for a few frames, and during those the picture on screen is the old size stretched.
            // Saying so is the difference between "busy" and "ignored the click" (spec §15).
            panel_->previewResizing = previewFramed && (cw != renderWidth_ || ch != renderHeight_);
            // ---- the camera indicator, and the one seam a camera system would arrive at --------
            //
            // This engine has exactly one camera: `scene::Scene::camera`, rewritten every frame by
            // `Composition::applyParameters` from the `camera/*` parameters. There is no camera
            // list, no camera entity and no selection -- the shot system bakes its cameras down to
            // keys on `camera/position` and `camera/target`, so a directed shot and a hand-flown
            // viewport differ in *what writes the parameters*, never in which camera is read.
            //
            // So the preview does not choose a camera and must not: it renders `engine_->scene()`,
            // which already holds whatever the authoritative answer is this frame. All that is left
            // to report is what is driving it, which is what this line does. If a camera-selection
            // system is added, it will publish its choice by writing those same parameters, and
            // this indicator is the only line here that will need to know its name.
            // ADR-245 published the answer this used to guess at. `cameraLooksDirected` could only
            // ever say "something is driving the camera"; `activeCamera()` says *which* camera and
            // *why*, which is the difference between "shot camera" and "UFO Watch -- abduction".
            const scene::ActiveCameraState active = engine_->activeCamera();
            switch (active.reason) {
            case scene::ActiveCameraReason::Event:
                panel_->previewCameraLabel =
                    active.eventName.empty()
                        ? active.name + " -- event"
                        : fmt::format("{} -- {}", active.name, active.eventName);
                break;
            case scene::ActiveCameraReason::Shot:
                panel_->previewCameraLabel = active.name + " -- shot";
                break;
            case scene::ActiveCameraReason::Default:
            default:
                // Nothing is directing, so the old distinction is still the useful one: is the
                // camera where the Auto-director put it, or where the person dragged it?
                panel_->previewCameraLabel =
                    cameraLooksDirected(*engine_) ? active.name + " -- directed" : "viewport camera";
                break;
            }
            if (active.blending()) {
                panel_->previewCameraLabel += fmt::format("  ({:.0f}%)", active.blend * 100.0f);
            }
            // ADR-391: the label above says what the *film* is on, which is the truth and is what
            // the Cameras panel needs. It is not necessarily what this frame was rendered through,
            // and the indicator's job is the second question. Said as a prefix rather than by
            // replacing the film's answer, because "you are flying, and the film is on UFO Watch"
            // is one fact with two halves and dropping either one is how somebody concludes the
            // director has stopped working.
            const scene::ViewportView shown = effectiveViewportView();
            if (!shown.showsFilm()) {
                if (shown.mode == scene::ViewportCamera::Editor) {
                    panel_->previewCameraLabel = "editor view  (film: " + panel_->previewCameraLabel + ")";
                } else {
                    const scene::CameraRig* rig = engine_->composition() != nullptr
                                                      ? engine_->composition()->cameraDirection().find(shown.camera)
                                                      : nullptr;
                    panel_->previewCameraLabel = fmt::format("through {}  (film: {})",
                                                             rig != nullptr ? rig->name : std::string("?"),
                                                             panel_->previewCameraLabel);
                }
            }
            // What the toolbar draws, and why it is not always what was asked for.
            panel_->viewportView = viewportView_;
            panel_->viewportViewForced = !viewportView_.showsFilm() && shown.showsFilm();
            panel_->viewportViewNote.clear();
            if (panel_->viewportViewForced) {
                if (ui::modeShowsOutputFrame(panel_->preview.mode)) {
                    panel_->viewportViewNote = "the canvas is in " +
                                               std::string(ui::previewViewModeLabel(panel_->preview.mode)) +
                                               " mode, which shows what renders";
                } else if (engine_->composition() == nullptr) {
                    panel_->viewportViewNote = "this scene has one fixed camera";
                } else {
                    panel_->viewportViewNote = "an output or share is presenting this frame";
                }
            }
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
                requestClose(ui::CloseIntent::Quit, [this] { quitting_ = true; });
            }
            if (quitting_) {
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
        // The outstanding interactive seek, honoured here and nowhere else: once per frame, in a
        // named place, *before* the update that derives the scene from it -- so the frame that pays
        // for a seek also carries its result, instead of showing the previous second's world.
        //
        // Deliberately not inside `panel_->draw`. A panel calling `seekSeconds` from inside its own
        // draw put the cost in `ui.build`, which is the phase every budget in
        // docs/application-performance.md §11 is written against, and made "the UI is slow" the
        // only available description of a world evaluation. It is a phase of its own now.
        {
            core::PhaseProfiler::Scope scope(prof, kPhSeekService);
            const bool performed = engine_->serviceSeekRequest(stats.frameIntervalMs);
            if (performed && slowPhaseMs() > 0.0) {
                log::debug("seek serviced at frame {}", framesRendered);
            }
        }
        const std::uint64_t discontinuity = engine_->transport().discontinuityRevision();
        if (discontinuity != lastTransportDiscontinuity_) {
            renderer_->resetTemporalHistory();
            lastTransportDiscontinuity_ = discontinuity;
        }
        // ADR-410: publish what the ring holds, so the panel's settling badge is the renderer
        // reporting rather than the UI guessing. Pushed every frame because the whole point is
        // that it changes as the history refills after a seek.
        {
            const auto& t = renderer_->stats().temporal;
            engine_->setTemporalHistoryReport(scene::TemporalHistoryReport{
                .framesValid = t.framesValid, .framesNeeded = t.framesNeeded, .stalled = t.stalled,
                .bytes = t.historyBytes, .width = t.historyWidth, .height = t.historyHeight});
        }
        engine_->setViewport(renderWidth_, renderHeight_);
        // ADR-391, and *before* the update: the composition decides the frame's camera inside
        // `update`, so a mode pushed after it would be one frame late -- which is exactly long
        // enough for a click to be picked against the previous frame's camera.
        applyViewportView();
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
            // No touch is recorded here, and that is checked rather than assumed: ADR-101 routes
            // every assistant edit through `EditHistoryTransactionSink` into `edits_`, so the
            // `edits_.dirty()` arm of the sampler already sees them. An assistant edit that did not
            // reach the undo history would also not be undoable, which is a louder failure than
            // this one.
        }

        // ADR-186's limits in the viewport. Set every frame rather than on the checkbox's edge:
        // loading a project rebuilds the composition, and a policy applied once at the click would
        // be lost by the next scene the editor opened -- which is the same shape of defect as the
        // render settings the panel copied once at start-up.
        engine_->setDetailLimits(liftViewportLimits_ ? scene::DetailLimits::unlimited()
                                                      : scene::DetailLimits{});

        // ADR-364: what the application is doing this frame, and therefore what the viewport is
        // allowed to cost. Computed here -- before the jobs are stepped -- so that the frame in
        // which a render *starts* is already suspended, rather than one frame late.
        const RenderActivity activity = job_             ? RenderActivity::OfflineRaster
                                        : (ptJob_ && !ptJob_->done()) ? RenderActivity::OfflinePathTrace
                                                                      : RenderActivity::Interactive;
        const ViewportPolicy viewportPolicy =
            viewportPolicyFor(activity, settings_.suspendViewportDuringRender);
        // Said once on each edge, not per frame. The log is where a scripted arm can see it --
        // `--render-in-app` is the only way to reach a mid-render state without a person at the
        // machine, and a feature whose only evidence is a tooltip is a feature no run can check.
        if (!viewportPolicy.drawWorld != viewportWasSuspended_) {
            viewportWasSuspended_ = !viewportPolicy.drawWorld;
            if (viewportWasSuspended_) {
                log::info("viewport: suspended for the {} (ADR-364)", renderActivityName(activity));
            } else {
                log::info("viewport: resumed after {} frame(s) not drawn",
                          viewportFramesSuspended_ - viewportSuspendedAtStart_);
                viewportSuspendedAtStart_ = viewportFramesSuspended_;
            }
        }
        if (panel_) {
            // The panel says which of the two it is doing, and how many frames it has not drawn.
            // That count is the evidence the feature fired: a suspension that is on and reports
            // zero skipped frames is a suspension that is not happening, and a person can see it
            // (ADR-182 -- the arm has to be able to come out the other way, in the product and not
            // only in a test).
            panel_->viewportSuspended = !viewportPolicy.drawWorld;
            panel_->viewportFramesSuspended = viewportFramesSuspended_;
        }

        // `--render-in-app`: the Render button, pressed once, on the first frame that has a
        // window, a project and a panel. Here rather than in `init()` because the job renders
        // between UI frames and there is no frame loop to render between until this one.
        if (options_.renderInApp && !renderInAppStarted_) {
            renderInAppStarted_ = true;
            uiRender_.outputPath = *options_.renderInApp;
            uiRender_.normalisePattern();
            startRenderFromUi();
        }
        // Background render: a few frames per UI frame, then the next queued job.
        if (job_) {
            core::PhaseProfiler::Scope scope(prof, kPhJob);
            const bool complete = job_->step(4, 0.010);
            // Collected before the job is released, so the final frame of a render is the one left
            // on the panel rather than whatever the last UI poll happened to catch (ADR-320).
            serviceRenderPreview();
            if (complete) {
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
        // Again, and outside the branch: the call above cannot run on a frame where there is no
        // job, so without this the panel would still be saying "live" about the last frame of a
        // render that ended, failed or was cancelled several minutes ago.
        serviceRenderPreview();
        // Input diagnostics (AVGEN_UI_SELFTEST=1): logs what ImGui and SDL each see of the
        // pointer, plus the raw event counts, so "the UI does not react to clicks" can be traced
        // to the event routing rather than the widgets.
        static const bool uiSelfTest = std::getenv("AVGEN_UI_SELFTEST") != nullptr;
        uiSelfTestEvents_ = uiSelfTest;
        {
            core::PhaseProfiler::AllocScope scope(prof, kPhUi, kPhAllocUi);
            imgui_->newFrame();
            // A panel that is closed is not drawn at all, so focusing it does nothing: the capture
            // comes back showing whichever tab happened to be in front, and it looks like a
            // successful capture of the wrong panel. Opened *before* the draw, raised after it.
            if (options_.captureUi) {
                for (const std::string& name : options_.captureUiPanels) {
                    if (bool* open = panel_->layout().slot(name); open != nullptr) {
                        *open = true;
                    }
                }
            }
            // Whether there is a baked cut to protect, refreshed each frame: the Auto-director can
            // be enabled or stood down while the panel is open, and a lock offered for a cut that
            // no longer exists is a control that guards nothing.
            panel_->cameraDirected = cameraDirection_.directed;
            // ADR-440. Before the panels rather than after, so the title and the menus agree with
            // the modal about whether there is anything to lose, and so the widget-activity signal
            // it reads is the previous frame's -- the window it is asking about.
            sampleProjectDirtyIfIdle();
            panel_->draw(*engine_, stats);
            // After the panels, so the modal is submitted last and is on top of everything it is
            // blocking (ADR-440). Inside the ImGui frame, because that is the only place a popup
            // exists.
            serviceCloseGate();
            // Raised *after* the panels are submitted, because ImGui cannot focus a window it has
            // not seen this frame. The focus lands on the next frame, which is why the capture frame
            // defaults to well after the layout settles.
            if (options_.captureUi) {
                for (const std::string& name : options_.captureUiPanels) {
                    ImGui::SetWindowFocus(name.c_str());
                }
            }
        }
        // TEMPORARY (ui-responsiveness phase 2). The frame's UI state is now decided: the playhead
        // has been drawn wherever this frame's input put it and every widget has taken its new
        // value. Everything after this point records and presents a picture of a decision already
        // made -- so this is the moment "the UI acknowledged the input", as distinct from the
        // moment the pixels reach the compositor (`input->present ms`).
        if (probe2::frame().newestInputNs != 0) {
            const std::uint64_t nowNs = SDL_GetTicksNS();
            if (nowNs > probe2::frame().newestInputNs) {
                prof.add(kPhUiAck,
                         static_cast<double>(nowNs - probe2::frame().newestInputNs) / 1.0e6);
            }
        }
        // T5 for the interaction log, and the same instant for the same reason: the frame's UI
        // state is decided here.
        core::interactions().markSubmit();
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
        if (panel_ && viewportPolicy.drawWorld) {
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
            // The shadow views are LAST frame's: this call runs before `render()` fits this
            // frame's. Drawing a cascade one frame stale is the honest option and the note is here
            // so nobody reads a lagging box as a fitting bug -- the alternative, fitting a second
            // set here to draw, is the §37 trap the span exists to avoid.
            const rendering::ProceduralLodLevels lodLevels =
                rendering::readProceduralLodLevels(renderer_->procedurals(), engine_->scene(), options);
            rendering::buildDebugGeometry(renderer_->debugDraw(), engine_->scene(), options, time.renderTime,
                                          &transformHistory_, &lodLevels, renderer_->shadows().views());
        }
        const rendering::ShaderFrameInputs shaderInputs{&engine_->shaderLayers(),
                                                        engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
        // ADR-364. `drawUi` is deliberately not consulted here: the interface is drawn
        // unconditionally a few lines below, and the field exists so that invariant is written down
        // rather than being true by accident. What this skips is the world -- the scene pass, the
        // post chain and the debug geometry -- into the same device the render is using.
        if (!viewportPolicy.drawWorld) {
            ++viewportFramesSuspended_;
        }
        if (viewportPolicy.drawWorld) {
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
            {   // TEMPORARY: phase 2
                const rendering::CpuFrameBreakdown& rr = renderer_->stats().cpu;
                prof.add(kPhRrUploads, rr.uploadsMs);
                prof.add(kPhRrProc, rr.proceduralMs);
                prof.add(kPhRrObjects, rr.objectsMs);
                prof.add(kPhRrFields, rr.fieldsMs);
                prof.add(kPhRrSdf, rr.sdfMs);
                prof.add(kPhRrLights, rr.lightsMs);
            }
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
        if (viewportPolicy.drawWorld) {
            transformHistory_.record(renderer_->diagnosticFrame());
        }
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
        // The editor, as a picture. After Submit so Dear ImGui's draw has actually happened, and
        // before `present()` because the swapchain texture is only ours until then. This is the one
        // capture that contains the interface: `--capture` re-renders the scene and shows the world
        // with nothing on top of it.
        if (options_.captureUi && framesRendered >= options_.captureUiFrame) {
            const std::filesystem::path path = *options_.captureUi;
            options_.captureUi.reset();   // once, whatever happens next
            gpu::Surface* surface = context_->primarySurface();
            const wgpu::Texture texture = surface != nullptr ? surface->currentTexture() : nullptr;
            if (texture == nullptr) {
                log::error("--capture-ui: no swapchain texture to read");
            } else if (auto image = gpu::readTexture8(
                           *context_, texture, window_->pixelWidth(), window_->pixelHeight(),
                           surface->format() == wgpu::TextureFormat::BGRA8Unorm);
                       !image) {
                log::error("--capture-ui: {}", image.error().message);
            } else if (auto r = writeCapture(*image, path); !r) {
                log::error("--capture-ui: {}", r.error().message);
            } else {
                log::info("captured the interface ({}x{}) at frame {} to {}", image->width,
                          image->height, framesRendered, path.string());
            }
            if (options_.captureUiQuit) {
                uiCaptureDone_ = true;
            }
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
        if (uiCaptureDone_) {
            break;   // --capture-ui, without --capture-ui-stay: the picture is written, we are done
        }
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
        // TEMPORARY (ui-responsiveness phase 2): how long the *oldest* event in this frame's batch
        // waited. A backlog shows up here and nowhere else -- the newest event is by construction
        // young no matter how many stale ones were drained ahead of it.
        if (probe2::frame().oldestInputNs != 0) {
            const std::uint64_t nowNs = SDL_GetTicksNS();
            if (nowNs > probe2::frame().oldestInputNs) {
                prof.add(kPhOldestAck,
                         static_cast<double>(nowNs - probe2::frame().oldestInputNs) / 1.0e6);
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
        {
            const std::uint64_t procGen = scene::proceduralRebuildCount();
            const std::uint64_t envBuild = rendering::environmentBuildCount();
            const std::uint64_t flatten =
                engine_->composition() != nullptr ? engine_->composition()->flattenCount() : lastFlatten;
            prof.count(kPhProcGen, static_cast<double>(procGen - lastProcGen));
            prof.count(kPhEnvBuild, static_cast<double>(envBuild - lastEnvBuild));
            prof.count(kPhFlatten, static_cast<double>(flatten >= lastFlatten ? flatten - lastFlatten : 0));
            lastProcGen = procGen;
            lastEnvBuild = envBuild;
            lastFlatten = flatten;
        }
        {
            // TEMPORARY (ui-responsiveness phase 2). `gpu.frame ms (GPU)` is a GPU timestamp read
            // out of gpu::FrameTimeline, not CPU wall clock around a GPU call -- it is two or three
            // frames behind, which over a 120-frame block is immaterial and inside one frame is not.
            const probe2::Frame& pr = probe2::frame();
            prof.add(kPhSeek, pr.seekMs);
            prof.add(kPhEntitySeek, pr.entitySeekMs);
            prof.count(kPhSeekCount, static_cast<double>(pr.seeks));
            prof.count(kPhSimBodies, static_cast<double>(pr.entitySimBodies) / 1000.0);
            prof.add(kPhCatchup, pr.analysisCatchupMs);
            prof.add(kPhUpdControl, pr.updControlMs);
            prof.add(kPhUpdSignals, pr.updSignalsMs);
            prof.add(kPhUpdMod, pr.updModulationMs);
            prof.add(kPhUpdCtrl, pr.updControllerMs);
            prof.add(kPhUpdOther, pr.updOtherMs);
            prof.count(kPhInputEvents, static_cast<double>(pr.inputEvents));
            prof.add(kPhMeshUp, pr.meshUploadMs);
            prof.add(kPhTexUp, pr.textureUploadMs);
            prof.count(kPhTexCount, static_cast<double>(pr.texturesUploaded));
            prof.add(kPhEnvMs, pr.environmentMs);
            prof.count(kPhMeshCount, static_cast<double>(pr.meshesUploaded));
            if (stats.gpuFrameMs >= 0.0) {
                prof.add(kPhGpuFrame, stats.gpuFrameMs);
            }
        }
        // ---- the interaction log's end of frame -------------------------------------------------
        // Everything this frame caused is attributed to whatever interaction is open, then T6 is
        // taken. The counters are the half contention cannot corrupt: "one flatten and forty-four
        // texture uploads" is the same fact on an idle machine and on one at load average forty.
        {
            const probe2::Frame& pr = probe2::frame();
            const std::uint64_t flattenNow =
                engine_->composition() != nullptr ? engine_->composition()->flattenCount() : 0;
            core::interactions().addCounters(
                flattenNow >= interactionFlattenMark_ ? flattenNow - interactionFlattenMark_ : 0,
                pr.seeks, scene::proceduralRebuildCount() >= interactionProcGenMark_
                              ? scene::proceduralRebuildCount() - interactionProcGenMark_
                              : 0,
                pr.texturesUploaded, pr.entitySimBodies);
            interactionFlattenMark_ = flattenNow;
            interactionProcGenMark_ = scene::proceduralRebuildCount();
            core::interactions().addCpuMs(stats.cpuFrameMs);
            // Named as a wait, never folded into CPU work. `gpu.acquire WAIT` grows when the GPU
            // falls behind and reading it as the CPU getting slower is the mistake the phase
            // profiler's own header exists to prevent.
            core::interactions().addBlockedMs(
                std::chrono::duration<double, std::milli>(workAfterAcquire - workBeforeAcquire)
                    .count());
            if (stats.gpuFrameMs >= 0.0) {
                core::interactions().setGpuMs(stats.gpuFrameMs);
            }
            // ...and nothing is set when it is not, so `gpu` reports unavailable rather than 0.0 on
            // the first frames, where FrameTimeline genuinely has no completed frame to report.
            core::interactions().markFrameVisible();
        }
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
        // ADR-225/ADR-246. Written on the way out as well as when a toolbar toggle moves, because
        // the in-frame check below is throttled and the last thing someone does before quitting is
        // very often the thing they most want kept.
        if (!(panel_->preview == settings_.preview)) {
            saveSettings();
        }
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
    if (!abGroups.empty()) {
        // Always printed, with or without --profile-cpu: a `--ui-ab` run whose comparison has to be
        // asked for separately is a run somebody will take without it and then quote anyway.
        std::fputs(cpuProfile_
                       .compare(fmt::format("interleaved arms, one process, {} block(s) of {} frames",
                                            options_.uiAbBlocks, options_.uiAbFrames),
                                abGroups)
                       .c_str(),
                   stderr);
        for (const int g : abGroups) {
            std::fputs(cpuProfile_.report(fmt::format("arm '{}'", cpuProfile_.groupName(g)), g).c_str(), stderr);
        }
    }
    if (options_.profileCsv) {
        std::ofstream out(*options_.profileCsv);
        out << cpuProfile_.csv();
        log::info("frame phases written to {}", options_.profileCsv->string());
    }
    if (options_.latencyReport && !abGroups.empty()) {
        // One table per arm, never one pooled table: a summary that averaged the two halves of an
        // A/B would report their mean and call it a result.
        double loads[3] = {0.0, 0.0, 0.0};
        const double loadAverage = ::getloadavg(loads, 3) > 0 ? loads[0] : -1.0;
        for (const int g : abGroups) {
            std::fputs(core::formatReport(core::summarise(core::interactions(), g), loadAverage,
                                          fmt::format("arm '{}'", cpuProfile_.groupName(g)))
                           .c_str(),
                       stderr);
        }
    }
    if (options_.latencyReport) {
        // The load average is read here rather than quoted from memory: ADR-170 requires a timing
        // claim to state the contention it was taken under, and on this machine that ranges from 3
        // to 44 within one session.
        double loads[3] = {0.0, 0.0, 0.0};
        const double loadAverage = ::getloadavg(loads, 3) > 0 ? loads[0] : -1.0;
        std::fputs(core::formatReport(core::summarise(core::interactions()), loadAverage).c_str(),
                   stderr);
        if (core::interactions().overlaps() > 0) {
            log::warn("latency: {} overlapping interaction(s) -- two were in flight at once, which "
                      "the main thread's serialisation says cannot happen. The older record of each "
                      "pair was discarded rather than merged.",
                      core::interactions().overlaps());
        }
    }
    if (options_.latencyCsv) {
        std::ofstream out(*options_.latencyCsv);
        out << core::formatCsv(core::interactions());
        log::info("interaction records written to {}", options_.latencyCsv->string());
    }
    // What the scripted editor run actually did, in order. This is the only record that survives a
    // run the machine cannot screenshot (ADR-092), so it is printed whether or not anything else is.
    for (const std::string& line : abEditLog) {
        log::info("{}", line);
    }
    for (const std::string& line : uiScript_.editLog()) {
        log::info("{}", line);
    }
    // TEMPORARY (ui-responsiveness phase 2): what the run actually rendered, at the END of it.
    // The existing line above is printed at frame 60, before a docked layout has settled, and a
    // rendering-sensitivity arm that cannot say what size its render target was is an arm that
    // cannot fail (ADR-182).
    log::info("phase2: final canvas {}x{} px ({:.2f} Mpx), {} triangles, {} draw calls",
              renderWidth_, renderHeight_,
              static_cast<double>(renderWidth_) * renderHeight_ / 1.0e6, renderer_->stats().triangles,
              renderer_->stats().drawCalls);
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
    // ADR-521: and the warm-up, for the same reason. See RenderSettings::particleWarmUpFrames.
    s.particleWarmUpFrames = options_.particleWarmUpFrames;
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
    if (options_.aovs) s.aovs = *options_.aovs;
    if (!options_.postStages.empty()) s.postStages = options_.postStages;
    return s;
}

Result<void> Application::openAny(const std::filesystem::path& path) {
    // A history that describes a world that is gone is worse than no history: pressing undo would
    // try to restore nodes into a scene that never had them, and the labels would describe edits to
    // somebody else's project (ADR-092). The nodes the commands were holding go with it.
    if (panel_ != nullptr) {
        panel_->editor.reset();
    }
    // The commands go with the document they describe, and the new project is clean because it is
    // exactly as it was stored (ADR-092, ADR-440). Without this a project opened after an edited
    // one would report itself dirty the moment it appeared.
    edits_.clearHistory();
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
    job->setDebugOptions(debugOptions());
    if (auto r = job->start(); !r) {
        return std::unexpected(r.error());
    }
    return job;
}

// The CPU path tracer (ADR-351). Unlike `--render` this needs no GPU at all: the tracer is CPU-only
// and `EngineMode::Offline` evaluates a scene without a device, so a trace runs on a machine whose
// GPU is busy with something else -- which, with several agents sharing one machine, it usually is.
// Starting a trace from the Render panel. The job runs on its own coordinator thread, so this
// returns immediately and the UI keeps its frame rate; the panel polls `progress()`.
void Application::startPathTraceFromUi() {
    if (ptJob_ && !ptJob_->done()) {
        panel_->setStatus("a path trace is already running");
        return;
    }
    if (ptSeqThread_.joinable() && !ptSeqDone_.load()) {
        panel_->setStatus("a path-traced sequence is already running");
        return;
    }
    // Same rule as a render: the job loads from the project FILE, not from live state, which is
    // what makes the result reproducible. A session with no file is snapshotted to a temporary one.
    std::filesystem::path projectFile = engine_->projectPath();
    if (projectFile.empty()) {
        projectFile = std::filesystem::temp_directory_path() / "avgen_pathtrace_session.json";
        if (auto r = engine_->saveProject(projectFile); !r) {
            panel_->setStatus(r.error().message);
            return;
        }
    }

    // The trace's OWN output path (ADR-366). It used to read the raster job's, which the Render
    // panel returns before drawing while a trace is selected -- so the field was unreachable and an
    // unset one went to $TMPDIR with only a status line to say so. Resolved against the project's
    // folder, like a render's is, rather than against whatever the process's cwd happens to be.
    std::filesystem::path out = uiPathTrace_.outputPath;
    if (out.empty()) {
        out = std::filesystem::temp_directory_path() / "avgen_pathtrace.exr";
        panel_->setStatus("no output path set; tracing to " + out.string());
    } else if (out.is_relative()) {
        out = std::filesystem::absolute(projectFile).parent_path() / out;
    }
    out.replace_extension(".exr");   // the tracer writes scene-linear EXR and nothing else

    pathtrace::TraceJobRequest request;
    request.project = projectFile;
    request.seconds = uiPathTrace_.seconds;
    request.output = out;
    request.settings = traceSettingsFrom(uiPathTrace_, uiRender_.width, uiRender_.height);
    request.denoise = uiPathTrace_.denoise;
    request.writeAovs = uiPathTrace_.writeAovs;

    if (auto ok = request.validate(); !ok) {
        panel_->setStatus(ok.error().message);
        return;
    }

    // ADR-383: a range goes to `TraceSequence` instead. Same settings object, same output field,
    // same button -- the only thing the person did differently is tick "range".
    if (uiPathTrace_.isSequence()) {
        // The decision itself is `traceSequenceRequestFrom`, which is a free function so a test can
        // reach it. This branch is wiring and nothing else.
        TraceSequenceRequest seqRequest = traceSequenceRequestFrom(
            projectFile, uiPathTrace_, uiRender_.width, uiRender_.height, out, uiRender_);
        if (auto ok = seqRequest.validate(); !ok) {
            panel_->setStatus(ok.error().message);
            return;
        }
        if (ptSeqThread_.joinable()) {
            ptSeqThread_.join();
        }
        ptSequence_ = std::make_unique<TraceSequence>(std::move(seqRequest));
        ptSeqDone_.store(false);
        ptSeqError_.clear();
        // Its own thread, for the reason `TraceJob` has one: the trace is minutes of CPU and the
        // editor has to keep its frame. The panel polls `progress()`.
        ptSeqThread_ = std::thread([this] {
            if (auto r = ptSequence_->run(); !r) {
                ptSeqError_ = r.error().message;
            }
            ptSeqDone_.store(true);
        });
        panel_->setStatus(fmt::format("path tracing {} frame(s) to {}",
                                      ptSequence_->progress().framesTotal,
                                      out.filename().string()));
        return;
    }

    lastPathTrace_ = pathtrace::TraceProgress{};
    ptJob_ = std::make_unique<pathtrace::TraceJob>(std::move(request));
    ptJob_->start();
    panel_->setStatus(fmt::format("path tracing to {}", out.filename().string()));
}

// ADR-383. A path-traced range, through `app::TraceSequence`: one project load, the shared
// `FrameSequenceDriver`, and either an EXR per frame or -- via the CPU output transform -- a movie.
int Application::runTraceSequence(const std::filesystem::path& projectFile,
                                  const PathTraceSettings& authored) {
    // The SAME decision the Render panel makes, through the same function: name it .mov and get a
    // movie, name it anything else and get a folder of scene-linear EXRs. Two spellings of that
    // rule is how a flag and a button come to disagree about what a path means.
    RenderSettings video = engine_ != nullptr ? engine_->renderSettings() : RenderSettings{};
    if (options_.codec) video.codec = *options_.codec;
    TraceSequenceRequest request =
        traceSequenceRequestFrom(projectFile, authored, options_.width, options_.height,
                                 *options_.pathtrace, video);

    if (auto ok = request.validate(); !ok) {
        log::error("pathtrace: {}", ok.error().message);
        return 2;
    }

    TraceSequence sequence(std::move(request));
    if (auto ok = sequence.start(); !ok) {
        log::error("pathtrace: {}", ok.error().message);
        return 2;
    }
    log::info("pathtrace: denoise {}", pathtrace::denoiseVersion());

    // Progress that is honest at both levels: which frame, and how far into it. A path-traced frame
    // takes long enough that a frame counter on its own looks like a hang (ADR-351's rule, one
    // level up from where it was written).
    std::uint64_t lastFrame = ~0ull;
    while (!sequence.step(1)) {
        const SequenceProgress p = sequence.progress();
        if (p.framesSubmitted != lastFrame) {
            lastFrame = p.framesSubmitted;
            const double remaining = p.estimatedRemainingSeconds;
            log::info("pathtrace: frame {}/{} ({:.0f}%), {:.1f}s elapsed{}", p.framesSubmitted,
                      p.framesTotal, p.fraction() * 100.0, p.elapsedSeconds,
                      remaining >= 0.0 ? fmt::format(", about {:.0f}s left", remaining) : std::string());
        }
    }
    const SequenceProgress p = sequence.progress();
    if (!p.error.empty()) {
        log::error("pathtrace: {}", p.error);
        return 4;
    }
    if (p.cancelled) {
        log::warn("pathtrace: cancelled after {} frame(s)", p.framesWritten);
        return 3;
    }
    log::info("pathtrace: {} frame(s) written, sequence hash {:016x}", p.framesWritten,
              p.sequenceHash);
    return 0;
}

int Application::runPathTrace() {
    // Same rule as `--render`: the job loads the project itself, so a session with no project file
    // is snapshotted to a temporary one first. Loading from the file rather than from live state is
    // what makes a trace reproducible (ADR-020's reasoning, and it applies here unchanged).
    std::filesystem::path projectFile = engine_ != nullptr ? engine_->projectPath() : std::filesystem::path{};
    if (options_.project) projectFile = *options_.project;
    if (projectFile.empty()) {
        log::error("--pathtrace needs a project: pass --project <file.json>");
        return 2;
    }

    // Start from what the project says and override with what was typed -- the shape
    // `renderSettingsFromOptions` already had, and it matters now that a project carries a
    // `pathtrace` block (ADR-366). Before this, `--pathtrace out.exr` on a project authored at 512
    // samples traced 32, because 32 was a struct default nobody had asked for.
    PathTraceSettings authored = engine_ != nullptr ? engine_->pathTraceSettings() : PathTraceSettings{};
    if (options_.ptSeconds) authored.seconds = *options_.ptSeconds;
    if (options_.ptSamples) authored.samplesPerPixel = std::max(1u, *options_.ptSamples);
    if (options_.ptDepth) authored.maxDepth = *options_.ptDepth;
    if (options_.ptSeed) authored.seed = *options_.ptSeed;
    if (options_.ptThreads) authored.threads = *options_.ptThreads;
    if (options_.ptDenoise) authored.denoise = *options_.ptDenoise;
    if (options_.ptAovs) authored.writeAovs = *options_.ptAovs;
    if (options_.ptProbe) authored.albedoProbe = *options_.ptProbe;
    // ADR-383: `--range a:b` makes it a sequence. Deliberately the SAME flag `--render` uses rather
    // than a `--pt-range`: it is the same question about the same timeline, and a second vocabulary
    // for it is a second thing to get wrong. `--fps` likewise.
    if (options_.rangeStart) authored.seconds = *options_.rangeStart;
    if (options_.rangeEnd) authored.endSeconds = *options_.rangeEnd;
    if (options_.fpsGiven && options_.offlineFps > 0.0) authored.fps = options_.offlineFps;

    if (authored.isSequence()) {
        return runTraceSequence(projectFile, authored);
    }

    pathtrace::TraceJobRequest request;
    request.project = projectFile;
    request.seconds = authored.seconds;
    request.output = *options_.pathtrace;
    request.writeAovs = authored.writeAovs;
    request.denoise = authored.denoise;
    request.settings = traceSettingsFrom(authored, options_.width, options_.height);

    log::info("pathtrace: {} at second {:.3f}, {}x{} at {} spp, depth {}, seed {}",
              projectFile.string(), request.seconds, request.settings.width, request.settings.height,
              request.settings.samplesPerPixel, request.settings.maxDepth, request.settings.seed);
    log::info("pathtrace: denoise {}", pathtrace::denoiseVersion());

    pathtrace::TraceJob job(std::move(request));
    // Started on the job's own thread rather than run inline, so this path exercises exactly the
    // code a UI would: if progress or cancellation were broken, the CLI would show it.
    job.start();

    // Poll and log real progress. The stages that cannot report say so rather than creeping.
    pathtrace::TraceJobState lastState = pathtrace::TraceJobState::Queued;
    std::uint32_t lastSamples = 0;
    while (!job.done()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const auto p = job.progress();
        if (p.state != lastState) {
            lastState = p.state;
            log::info("pathtrace: {}", p.stage);
        } else if (p.state == pathtrace::TraceJobState::Rendering && p.samplesDone != lastSamples) {
            lastSamples = p.samplesDone;
            log::info("pathtrace: {}/{} samples ({:.0f}%), {:.1f} s elapsed", p.samplesDone,
                      p.samplesTotal, p.fraction * 100.0f, p.elapsedSeconds);
        }
    }
    job.wait();

    const auto final = job.progress();
    if (final.state == pathtrace::TraceJobState::Complete) return 0;
    if (final.state == pathtrace::TraceJobState::Cancelled) {
        log::warn("pathtrace: cancelled");
        return 8;
    }
    log::error("pathtrace: {}", final.error.empty() ? "failed" : final.error);
    return 7;
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
        if (options_.aovs) settings.aovs = *options_.aovs;
        if (!options_.postStages.empty()) settings.postStages = options_.postStages;
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
    if (options_.pathtrace) {
        return runPathTrace();
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
            {
                // Not guarded on the panel any more: a headless render has no panel, and "the
                // overlays only exist in the editor" is what made them unreachable to every
                // diagnosis done from a rendered frame.
                const rendering::DebugViewOptions& options = debugOptions();
                renderer_->setDebugDepthTest(options.depthTest);
                const rendering::ProceduralLodLevels lodLevels =
                rendering::readProceduralLodLevels(renderer_->procedurals(), engine_->scene(), options);
                rendering::buildDebugGeometry(renderer_->debugDraw(), engine_->scene(), options, time.renderTime,
                                              nullptr, &lodLevels, renderer_->shadows().views());
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
                // Where the CPU frame went, by stage, in the *headless* loop.
                //
                // These numbers already existed: `probe2` has recorded them since the
                // ui-responsiveness investigation and `--profile-cpu` prints them -- but only from
                // the live editor, so every diagnosis done from a headless run has been blind to
                // them. That is how a scene whose GPU frame is 19 ms and whose CPU frame is 200 ms
                // came to be reported as a 19 ms scene: the one number that was printed was the one
                // that was fine.
                //
                // `controller` is `Composition::update` -- the scene evaluation, including any
                // flatten. `meshUp`/`texUp` carry their pass counts as well as their times, because
                // "the upload spiked" is a correlation and "it re-created 45 buffers" is an
                // attribution.
                {
                    const probe2::Frame& pr = probe2::frame();
                    log::info("             cpu(update): control={:.2f} signals={:.2f} modulation={:.2f} "
                              "controller={:.2f} other={:.2f} | meshUp={:.2f}ms/{}pass/{}buf "
                              "texUp={:.2f}ms/{} env={:.2f} analysisCatchup={:.2f}",
                              pr.updControlMs, pr.updSignalsMs, pr.updModulationMs, pr.updControllerMs,
                              pr.updOtherMs, pr.meshUploadMs, pr.meshUploadPasses, pr.meshesUploaded,
                              pr.textureUploadMs, pr.texturesUploaded, pr.environmentMs,
                              pr.analysisCatchupMs);
                }
                // The workload each measured phase was actually given. Without these an A/B that edits
                // a scene cannot prove its two arms differ, and "no effect" reads exactly like a run
                // whose edit never applied.
                // ADR-351: the entity LOD line, printed only when a scene actually has a chain. A
                // diagnostic that prints "entityLod: 0 of 0" on every frame of every scene that
                // does not use the feature is a line people learn to skip past.
                if (st.entityLod.drawables > 0) {
                    log::info("             entityLod: {}/{} demoted, tris {} -> {} ({:.1f}% of LOD0), "
                              "rungs {}/{}/{}/{}/{}, changed {} held {}",
                              st.entityLod.demoted, st.entityLod.drawables,
                              st.entityLod.sourceTriangles, st.entityLod.drawnTriangles,
                              100.0 * static_cast<double>(st.entityLod.ratio()), st.entityLod.rungs[0],
                              st.entityLod.rungs[1], st.entityLod.rungs[2], st.entityLod.rungs[3],
                              st.entityLod.rungs[4], st.entityLod.changed, st.entityLod.held);
                }
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
