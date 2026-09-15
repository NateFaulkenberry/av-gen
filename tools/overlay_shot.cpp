// Draws the viewport overlay into a PNG, with no window and no GPU (ADR-197).
//
// This repository has said in two places that it cannot screenshot an ImGui frame, and has built
// around the limitation honestly -- every decision the world editor makes lives in an ImGui-free
// translation unit so that it can be asserted instead of looked at. That is the right answer for
// the decisions. It is not an answer for the *drawing*: whether a route reads as a route, whether a
// label lands on its character or off the side of the canvas, whether a grid at low alpha is
// visible at all -- none of those are assertions, and a test that checks what went into
// `EditorVisuals` cannot tell you that the thing on screen is legible.
//
// So: build an ImGui context with no backend, run the overlay against a real scene, and rasterise
// the draw list in software. The output is exactly what the viewport would put over the frame,
// because it is the same `drawViewportOverlay` call with the same `WorldEditor` -- nothing here
// reimplements any part of the overlay, and the moment it did it would start lying.
//
//   avgen_overlay_shot --scene examples/world/glowmere-stylized.scene.json \
//                      --select wanderer --seconds 20 --nav-grid --out shot.png
//
// `--background` composites it over a frame `avgen --capture` rendered from the same camera, which
// is the picture a person is actually looking at when they use this overlay.

#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "scene/scene_types.hpp"
#include "ui/editor_layout.hpp"
#include "ui/viewport_overlay.hpp"
#include "ui/world_editor.hpp"
#include "ui/theme.hpp"
#include "ui/world_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using avgen::ui::CanvasRect;

struct Options {
    std::filesystem::path scene;
    std::filesystem::path out = "overlay.png";
    std::filesystem::path background;
    std::vector<std::string> select;
    int width = 1280;
    int height = 720;
    double seconds = 12.0;
    double fps = 60.0;
    bool route = true;
    bool grid = false;
    bool regions = false;
    bool points = false;
    float radius = 80.0f;
    bool panel = false;
    int profile = 0;
    bool haveCamera = false;
    glm::vec3 cameraPosition{0.0f};
    glm::vec3 cameraTarget{0.0f};
};

void usage() {
    std::printf(
        "avgen_overlay_shot -- the viewport overlay, rasterised without a window (ADR-197)\n\n"
        "  --scene <file>        the scene to load (required)\n"
        "  --out <file.png>      where to write the image (default overlay.png)\n"
        "  --background <png>    composite over this image, e.g. one `avgen --capture` wrote\n"
        "  --select <node>       select this node; repeatable\n"
        "  --size <WxH>          canvas size in pixels (default 1280x720)\n"
        "  --seconds <s>         how long to run the entity layer before drawing (default 12)\n"
        "  --fps <n>             the step it is run at (default 60)\n"
        "  --camera <px,py,pz,tx,ty,tz>   override the scene's camera\n"
        "  --no-route            turn the route overlay off\n"
        "  --nav-grid            draw the navigation grid\n"
        "  --nav-regions         colour walkable cells by connected region\n"
        "  --nav-points          draw the shore and vista points\n"
        "  --radius <m>          grid radius around the view target (default 80)\n"
        "  --panel               also draw the World panel's Debug tab, so its switches and the\n"
        "                        line that says what they are doing can be looked at\n"
        "  --profile <n>         repeat the collect-and-draw n times and report the median cost\n");
}

bool parseVec6(const char* text, glm::vec3& a, glm::vec3& b) {
    float v[6] = {0, 0, 0, 0, 0, 0};
    if (std::sscanf(text, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }
    a = glm::vec3(v[0], v[1], v[2]);
    b = glm::vec3(v[3], v[4], v[5]);
    return true;
}

// ---- the rasteriser ------------------------------------------------------------------------
//
// ImGui emits one thing: indexed triangles with a position, a texture coordinate and a colour,
// grouped into commands that each carry a clip rectangle and a texture. Untextured shapes -- every
// line, ring, quad and filled circle in the overlay -- point at the atlas's white pixel, so one
// sampler over the font atlas covers the whole file and there is no special case for text.
//
// Source-over blending in straight (non-premultiplied) 8-bit, which is what ImGui's own backends
// do. Bilinear sampling of the atlas, because nearest makes the default font look broken at exactly
// the sizes labels are drawn at and the point of this program is to judge legibility.
struct Canvas {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    void blend(int x, int y, float r, float g, float b, float a) {
        if (a <= 0.0f || x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)) *
                                4];
        for (int c = 0; c < 3; ++c) {
            const float source = (c == 0 ? r : c == 1 ? g : b);
            const float destination = static_cast<float>(p[c]) / 255.0f;
            p[c] = static_cast<std::uint8_t>(
                std::clamp((source * a + destination * (1.0f - a)) * 255.0f + 0.5f, 0.0f, 255.0f));
        }
        const float destinationAlpha = static_cast<float>(p[3]) / 255.0f;
        p[3] = static_cast<std::uint8_t>(
            std::clamp((a + destinationAlpha * (1.0f - a)) * 255.0f + 0.5f, 0.0f, 255.0f));
    }
};

struct Sampler {
    const ImTextureData* texture = nullptr;

    // Returns straight RGBA in 0..1. A null texture is opaque white, which is what an untextured
    // draw would be if the atlas had not been built.
    void at(float u, float v, float& r, float& g, float& b, float& a) const {
        r = g = b = a = 1.0f;
        if (texture == nullptr || texture->Pixels == nullptr) {
            return;
        }
        const float x = u * static_cast<float>(texture->Width) - 0.5f;
        const float y = v * static_cast<float>(texture->Height) - 0.5f;
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
        float accum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int j = 0; j < 2; ++j) {
            for (int i = 0; i < 2; ++i) {
                const int sx = std::clamp(x0 + i, 0, texture->Width - 1);
                const int sy = std::clamp(y0 + j, 0, texture->Height - 1);
                const float weight = (i == 0 ? 1.0f - fx : fx) * (j == 0 ? 1.0f - fy : fy);
                const std::uint8_t* texel =
                    texture->Pixels +
                    (static_cast<std::size_t>(sy) * static_cast<std::size_t>(texture->Width) +
                     static_cast<std::size_t>(sx)) *
                        static_cast<std::size_t>(texture->BytesPerPixel);
                if (texture->BytesPerPixel == 1) {
                    const float alpha = static_cast<float>(texel[0]) / 255.0f;
                    accum[0] += weight;
                    accum[1] += weight;
                    accum[2] += weight;
                    accum[3] += weight * alpha;
                } else {
                    for (int c = 0; c < 4; ++c) {
                        accum[c] += weight * static_cast<float>(texel[c]) / 255.0f;
                    }
                }
            }
        }
        r = accum[0];
        g = accum[1];
        b = accum[2];
        a = accum[3];
    }
};

void rasterise(Canvas& canvas, const ImDrawData& data) {
    for (const ImDrawList* list : data.CmdLists) {
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback != nullptr || cmd.ElemCount == 0) {
                continue;
            }
            Sampler sampler{cmd.TexRef._TexData};
            const int clipX0 = std::max(0, static_cast<int>(std::floor(cmd.ClipRect.x)));
            const int clipY0 = std::max(0, static_cast<int>(std::floor(cmd.ClipRect.y)));
            const int clipX1 = std::min(canvas.width, static_cast<int>(std::ceil(cmd.ClipRect.z)));
            const int clipY1 = std::min(canvas.height, static_cast<int>(std::ceil(cmd.ClipRect.w)));
            if (clipX0 >= clipX1 || clipY0 >= clipY1) {
                continue;
            }
            const ImDrawVert* vertices = list->VtxBuffer.Data + cmd.VtxOffset;
            const ImDrawIdx* indices = list->IdxBuffer.Data + cmd.IdxOffset;
            for (unsigned int e = 0; e + 3 <= cmd.ElemCount; e += 3) {
                const ImDrawVert& a = vertices[indices[e]];
                const ImDrawVert& b = vertices[indices[e + 1]];
                const ImDrawVert& c = vertices[indices[e + 2]];
                const float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) -
                                   (c.pos.x - a.pos.x) * (b.pos.y - a.pos.y);
                if (std::abs(area) < 1e-9f) {
                    continue;
                }
                const int x0 = std::max(clipX0, static_cast<int>(std::floor(
                                                    std::min({a.pos.x, b.pos.x, c.pos.x}))));
                const int x1 = std::min(clipX1, static_cast<int>(std::ceil(
                                                    std::max({a.pos.x, b.pos.x, c.pos.x}))) + 1);
                const int y0 = std::max(clipY0, static_cast<int>(std::floor(
                                                    std::min({a.pos.y, b.pos.y, c.pos.y}))));
                const int y1 = std::min(clipY1, static_cast<int>(std::ceil(
                                                    std::max({a.pos.y, b.pos.y, c.pos.y}))) + 1);
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        const float px = static_cast<float>(x) + 0.5f;
                        const float py = static_cast<float>(y) + 0.5f;
                        const float w0 =
                            ((b.pos.x - px) * (c.pos.y - py) - (c.pos.x - px) * (b.pos.y - py)) / area;
                        const float w1 =
                            ((c.pos.x - px) * (a.pos.y - py) - (a.pos.x - px) * (c.pos.y - py)) / area;
                        const float w2 = 1.0f - w0 - w1;
                        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                            continue;
                        }
                        const float u = w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x;
                        const float v = w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y;
                        float tr = 1.0f;
                        float tg = 1.0f;
                        float tb = 1.0f;
                        float ta = 1.0f;
                        sampler.at(u, v, tr, tg, tb, ta);
                        // IM_COL32 packs R in the low byte.
                        const auto channel = [](ImU32 colour, int shift) {
                            return static_cast<float>((colour >> shift) & 0xFFu) / 255.0f;
                        };
                        const float cr = w0 * channel(a.col, 0) + w1 * channel(b.col, 0) +
                                         w2 * channel(c.col, 0);
                        const float cg = w0 * channel(a.col, 8) + w1 * channel(b.col, 8) +
                                         w2 * channel(c.col, 8);
                        const float cb = w0 * channel(a.col, 16) + w1 * channel(b.col, 16) +
                                         w2 * channel(c.col, 16);
                        const float ca = w0 * channel(a.col, 24) + w1 * channel(b.col, 24) +
                                         w2 * channel(c.col, 24);
                        canvas.blend(x, y, cr * tr, cg * tg, cb * tb, ca * ta);
                    }
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    using namespace avgen;

    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s expects a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else if (arg == "--scene") {
            options.scene = next("--scene");
        } else if (arg == "--out") {
            options.out = next("--out");
        } else if (arg == "--background") {
            options.background = next("--background");
        } else if (arg == "--select") {
            options.select.emplace_back(next("--select"));
        } else if (arg == "--size") {
            const char* value = next("--size");
            if (std::sscanf(value, "%dx%d", &options.width, &options.height) != 2) {
                std::fprintf(stderr, "--size expects <width>x<height>\n");
                return 2;
            }
        } else if (arg == "--seconds") {
            options.seconds = std::atof(next("--seconds"));
        } else if (arg == "--fps") {
            options.fps = std::atof(next("--fps"));
        } else if (arg == "--camera") {
            if (!parseVec6(next("--camera"), options.cameraPosition, options.cameraTarget)) {
                std::fprintf(stderr, "--camera expects px,py,pz,tx,ty,tz\n");
                return 2;
            }
            options.haveCamera = true;
        } else if (arg == "--no-route") {
            options.route = false;
        } else if (arg == "--nav-grid") {
            options.grid = true;
        } else if (arg == "--nav-regions") {
            options.regions = true;
        } else if (arg == "--nav-points") {
            options.points = true;
        } else if (arg == "--panel") {
            options.panel = true;
        } else if (arg == "--profile") {
            options.profile = std::atoi(next("--profile"));
        } else if (arg == "--radius") {
            options.radius = static_cast<float>(std::atof(next("--radius")));
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            usage();
            return 2;
        }
    }
    if (options.scene.empty()) {
        usage();
        return 2;
    }
    log::init(log::Level::Info);

    app::Engine engine(app::EngineMode::Offline);
    // A `.scene.json` is a composition, not a glTF file; `loadScene` is for the latter. Routed by
    // extension rather than tried in turn, so a malformed composition reports its own parse error
    // instead of the glTF loader's.
    const std::string name = options.scene.filename().string();
    const bool composition = name.size() > 11 && name.rfind(".scene.json") == name.size() - 11;
    auto loaded = composition ? engine.loadComposition(options.scene) : engine.loadScene(options.scene);
    if (!loaded) {
        std::fprintf(stderr, "scene: %s\n", loaded.error().message.c_str());
        return 1;
    }
    // Run the entity layer, so a walker has actually chosen somewhere to go and planned a route to
    // it. Drawing frame zero would show an overlay that is correct and empty, which is precisely
    // the picture this program exists to tell apart from a broken one.
    const double step = options.fps > 0.0 ? 1.0 / options.fps : 1.0 / 60.0;
    const auto frames = static_cast<std::uint64_t>(std::max(options.seconds / step, 1.0));
    for (std::uint64_t i = 0; i < frames; ++i) {
        FrameTime time;
        time.frameIndex = i;
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        engine.update(time);
    }

    scene::Camera camera = engine.scene().camera;
    if (options.haveCamera) {
        camera.position = options.cameraPosition;
        camera.target = options.cameraTarget;
    }

    app::EditSystem edits;
    ui::WorldEditor editor;
    editor.attachEdits(edits);
    editor.selection.set(options.select);
    editor.showNavRoute = options.route;
    editor.showNavGrid = options.grid;
    editor.navGridRegions = options.regions;
    editor.showNavPoints = options.points;
    editor.navGridRadius = options.radius;

    Canvas canvas;
    canvas.width = options.width;
    canvas.height = options.height;
    canvas.rgba.assign(static_cast<std::size_t>(options.width) *
                           static_cast<std::size_t>(options.height) * 4,
                       0);
    // A flat charcoal by default, so a translucent grid cell has something to sit on and an alpha
    // of 46/255 is visible as the faint thing it is meant to be.
    for (std::size_t p = 0; p < canvas.rgba.size(); p += 4) {
        canvas.rgba[p + 0] = 22;
        canvas.rgba[p + 1] = 24;
        canvas.rgba[p + 2] = 28;
        canvas.rgba[p + 3] = 255;
    }
    if (!options.background.empty()) {
        auto image = assets::loadImage(options.background, false);
        if (!image) {
            std::fprintf(stderr, "background: %s\n", image.error().message.c_str());
            return 1;
        }
        if (static_cast<int>(image->width) != options.width ||
            static_cast<int>(image->height) != options.height) {
            std::fprintf(stderr,
                         "background is %ux%u and the canvas is %dx%d; render it at the same size\n",
                         image->width, image->height, options.width, options.height);
            return 1;
        }
        std::memcpy(canvas.rgba.data(), image->data.data(),
                    std::min(canvas.rgba.size(), image->data.size()));
        for (std::size_t p = 3; p < canvas.rgba.size(); p += 4) {
            canvas.rgba[p] = 255;
        }
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(options.width), static_cast<float>(options.height));
    io.DeltaTime = static_cast<float>(step);
    io.IniFilename = nullptr;
    // We service ImGui 1.92's texture requests ourselves, below: the atlas is built into CPU memory
    // and read straight out of `ImTextureData`, so there is no upload and no backend.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    // The application's own palette and metrics, not Dear ImGui's stock dark theme.
    //
    // This tool is the one instrument in the repository that can show what ImGui actually draws --
    // the standing rule here is that a UI cannot be certified by reading its source -- and until
    // now it rasterised in `StyleColorsDark` while the editor ran in something else entirely. A
    // picture of a panel in the wrong colours cannot answer a question about contrast, about a
    // selected row, or about whether a disabled item reads as disabled, which are most of the
    // questions worth taking a picture to answer.
    //
    // `Dark` rather than `System`: a screenshot has to be the same screenshot on a machine whose
    // appearance is set the other way.
    ui::applyTheme(app::AppearanceTheme::Dark);

    ui::WorldPanel panel;

    // What the overlay costs, measured the only way that is honest: the same two calls the viewport
    // makes, repeated, with the grid in whatever state the flags put it in. `editor.update` is the
    // collection -- walking the grid and building `EditorVisuals` -- and `drawViewportOverlay` is
    // the projection and the draw list. They are reported apart because they scale differently and
    // a single number would hide which of them the radius knob is actually buying.
    if (options.profile > 0) {
        std::vector<double> collect;
        std::vector<double> draw;
        collect.reserve(static_cast<std::size_t>(options.profile));
        draw.reserve(static_cast<std::size_t>(options.profile));
        for (int i = 0; i < options.profile; ++i) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("profile", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            CanvasRect probe;
            probe.width = static_cast<float>(options.width);
            probe.height = static_cast<float>(options.height);
            const float probeAspect = probe.width / std::max(probe.height, 1.0f);
            auto start = std::chrono::steady_clock::now();
            editor.update(engine, nullptr, camera, probeAspect, ui::EditorInput{});
            const auto middle = std::chrono::steady_clock::now();
            ui::drawViewportOverlay(editor, camera, probe, probeAspect);
            const auto end = std::chrono::steady_clock::now();
            collect.push_back(std::chrono::duration<double, std::milli>(middle - start).count());
            draw.push_back(std::chrono::duration<double, std::milli>(end - middle).count());
            ImGui::End();
            ImGui::Render();
        }
        std::sort(collect.begin(), collect.end());
        std::sort(draw.begin(), draw.end());
        const std::size_t middle = collect.size() / 2;
        std::fprintf(stderr,
                     "profile over %d frames: collect %.3f ms, draw %.3f ms, %zu nav cell(s)\n",
                     options.profile, collect[middle], draw[middle], editor.visuals().navCells.size());
    }

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("viewport", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoDecoration);
    CanvasRect rect;
    rect.x = 0.0f;
    rect.y = 0.0f;
    rect.width = static_cast<float>(options.width);
    rect.height = static_cast<float>(options.height);
    const float aspect = rect.width / std::max(rect.height, 1.0f);
    editor.update(engine, nullptr, camera, aspect, ui::EditorInput{});
    ui::drawViewportOverlay(editor, camera, rect, aspect);
    ui::drawViewportHud(editor, rect);
    ImGui::End();
    // The panel that owns the switches, drawn over the corner of the canvas. It is ImGui too, and
    // a switch nobody could look at is exactly what this whole exercise is about.
    if (options.panel) {
        ImGui::SetNextWindowPos(ImVec2(rect.width * 0.42f, 40.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(rect.width * 0.56f, rect.height * 0.9f), ImGuiCond_Always);
        ImGui::Begin("World", nullptr, ImGuiWindowFlags_NoSavedSettings);
        panel.drawDebugOptions(engine, &editor);
        ImGui::End();
    }
    ImGui::Render();

    ImDrawData* data = ImGui::GetDrawData();
    if (data == nullptr) {
        std::fprintf(stderr, "ImGui produced no draw data\n");
        return 1;
    }
    if (data->Textures != nullptr) {
        for (ImTextureData* texture : *data->Textures) {
            if (texture->Status != ImTextureStatus_OK) {
                texture->SetTexID(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)));
                texture->SetStatus(ImTextureStatus_OK);
            }
        }
    }
    rasterise(canvas, *data);

    if (auto r = assets::writePng(options.out, static_cast<std::uint32_t>(options.width),
                                  static_cast<std::uint32_t>(options.height), canvas.rgba);
        !r) {
        std::fprintf(stderr, "write: %s\n", r.error().message.c_str());
        return 1;
    }
    // Everything the overlay decided, on stderr, so a run that produced an empty image says why
    // without anybody having to open the image to find out that there was nothing to see.
    const ui::EditorVisuals& visuals = editor.visuals();
    std::fprintf(stderr,
                 "overlay: %zu selection box(es), %zu route(s), %zu nav cell(s), %zu shore, "
                 "%zu vista, %d vertices in %d list(s)\n",
                 visuals.selectionBoxes.size(), visuals.navRoutes.size(), visuals.navCells.size(),
                 visuals.navShore.size(), visuals.navVistas.size(), data->TotalVtxCount,
                 data->CmdLists.Size);
    for (const ui::EditorVisuals::NavRoute& route : visuals.navRoutes) {
        std::fprintf(stderr, "  route: %s\n    at (%.1f %.1f %.1f)", route.label.c_str(),
                     static_cast<double>(route.position.x), static_cast<double>(route.position.y),
                     static_cast<double>(route.position.z));
        for (const glm::vec3& waypoint : route.waypoints) {
            std::fprintf(stderr, " -> (%.1f %.1f %.1f)", static_cast<double>(waypoint.x),
                         static_cast<double>(waypoint.y), static_cast<double>(waypoint.z));
        }
        std::fprintf(stderr, "\n");
    }
    std::fprintf(stderr, "nav: %s\n", editor.navStatus().c_str());
    std::printf("%s\n", options.out.string().c_str());

    ImGui::DestroyContext();
    return 0;
}
