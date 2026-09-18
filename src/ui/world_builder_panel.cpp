#include "ui/world_builder_panel.hpp"

#include "ui/style.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace avgen::ui {
namespace {

// mm:ss, or a dash. Elapsed times are always known; remaining ones frequently are not, and the
// whole point of the job system's honest ETA is undone by a UI that prints "0:00" for "no idea".
std::string clockText(double seconds) {
    if (!(seconds >= 0.0)) {
        return "--:--";
    }
    const int total = static_cast<int>(seconds);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
    return buffer;
}

void weightSlider(const char* label, float& value, const char* tooltip) {
    ImGui::SliderFloat(label, &value, 0.0f, 1.0f, "%.2f");
    if (tooltip != nullptr && ImGui::IsItemHovered()) {
        tooltipUnformatted(tooltip);
    }
}
} // namespace

Result<assets::AssetLibrary> WorldBuilderPanel::resolveLibrary() const {
    if (!libraryPath_.empty()) {
        return assets::AssetLibrary::loadFile(libraryPath_);
    }
    return fail("no asset library found; set one in the World Builder");
}

void WorldBuilderPanel::draw(app::Engine& engine, app::JobSystem& jobs, app::WorldBuilder& builder) {
    // The library is found once and remembered. Scanning for it every frame would be file IO in
    // the UI loop for a value that changes when somebody says so.
    if (!librarySearched_) {
        librarySearched_ = true;
        for (const char* candidate : {"assets/manifest.json", "../assets/manifest.json",
                                      "../../assets/manifest.json"}) {
            if (std::filesystem::exists(candidate)) {
                libraryPath_ = std::filesystem::absolute(candidate).lexically_normal();
                break;
            }
        }
        if (auto lib = resolveLibrary()) {
            libraryCount_ = lib->size();
            libraryLabel_ = lib->license().empty() ? std::string("unknown licence") : lib->license();
            library_ = std::move(*lib);
        }
    }

    if (ImGui::CollapsingHeader("Recipe", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawRecipe();
    }

    ImGui::Separator();
    if (libraryCount_ > 0) {
        ImGui::TextDisabled("Library: %zu assets (%s)", libraryCount_, libraryLabel_.c_str());
        if (ImGui::IsItemHovered()) {
            tooltip("%s", libraryPath_.string().c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "No asset library found");
    }

    const bool busy = builder.busy();
    ImGui::BeginDisabled(busy || libraryCount_ == 0);
    if (ImGui::Button("Generate World", ImVec2(-1.0f, 0.0f))) {
        auto library = resolveLibrary();
        if (!library) {
            status_ = library.error().message;
        } else {
            lastJob_ = builder.generate(recipe, std::move(*library));
            status_ = "generating '" + recipe.world + "'...";
        }
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("working...");
    }
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }

    ImGui::Separator();
    if (lastWorld && ImGui::CollapsingHeader("World contents", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawWorldContents(engine);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Place assets")) {
        drawPlacement();
    }

    ImGui::Separator();
    drawJobs(jobs);
    (void)engine;
}

void WorldBuilderPanel::drawRecipe() {
    char name[128];
    std::snprintf(name, sizeof(name), "%s", recipe.world.c_str());
    if (ImGui::InputText("Name", name, sizeof(name))) {
        recipe.world = name;
    }
    int seed = static_cast<int>(recipe.seed);
    if (ImGui::InputInt("Seed", &seed)) {
        recipe.seed = static_cast<std::uint32_t>(std::max(seed, 0));
    }
    ImGui::SliderFloat("Extent (m)", &recipe.extent, 50.0f, 2000.0f, "%.0f");

    if (ImGui::TreeNodeEx("Composition", ImGuiTreeNodeFlags_DefaultOpen)) {
        weightSlider("Foreground", recipe.composition.foreground, "How dense the near ground is");
        weightSlider("Midground", recipe.composition.midground, nullptr);
        weightSlider("Background", recipe.composition.background,
                     "Silhouettes on the ridge line, seen from far away");
        weightSlider("Negative space", recipe.composition.negativeSpace,
                     "How much of the world is deliberately left empty. This is a positive "
                     "instruction, not the absence of the others.");
        weightSlider("Landmark emphasis", recipe.composition.focalStrength,
                     "How far the focal subject outranks everything around it");
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Ecology", ImGuiTreeNodeFlags_DefaultOpen)) {
        weightSlider("Flora", recipe.ecology.flora, nullptr);
        weightSlider("Fungi", recipe.ecology.fungi, nullptr);
        weightSlider("Rock", recipe.ecology.rock, nullptr);
        weightSlider("Crystal", recipe.ecology.crystal, nullptr);
        weightSlider("Creature", recipe.ecology.creature, nullptr);
        weightSlider("Structure", recipe.ecology.structure, nullptr);
        ImGui::TextDisabled("A weight of 0 means absent, not rare.");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Atmosphere")) {
        weightSlider("Fog", recipe.atmosphere.fog, nullptr);
        weightSlider("Spores", recipe.atmosphere.spores, nullptr);
        weightSlider("Floating", recipe.atmosphere.floating, nullptr);
        weightSlider("Depth haze", recipe.atmosphere.depthHaze, nullptr);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Lighting")) {
        weightSlider("Key (moon/sun)", recipe.lighting.key, nullptr);
        weightSlider("Bioluminescence", recipe.lighting.bioluminescence,
                     "How much of the world's light the world itself provides");
        weightSlider("Volumetric", recipe.lighting.volumetric, nullptr);
        weightSlider("Bounce", recipe.lighting.bounce, nullptr);
        ImGui::TreePop();
    }
}


void WorldBuilderPanel::drawPlacement() {
    // The brush moved to the Edit panel (ADR-092), where it can show a live ghost and where it sits
    // beside the selection and the history it shares an undo stack with. Two windows holding two
    // halves of one job is the "collection of engineering panels" §43 warns about, and the brush
    // had the worse half: it could arm an asset but could not say where it would land.
    if (!library_ || library_->size() == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "No asset library found");
        return;
    }
    ImGui::TextDisabled("%zu assets loaded.", library_->size());
    ImGui::TextWrapped("Painting, selecting, grouping and undo are in the Edit panel.");
}

void WorldBuilderPanel::drawWorldContents(app::Engine& engine) {
    if (!lastWorld) {
        return;
    }
    const app::GeneratedWorld& world = *lastWorld;
    ImGui::TextDisabled("%zu layer(s), %zu hero(es), %zu zone(s), %zu region(s)",
                        world.composed.layers.size(), world.composed.plan.heroes.size(),
                        world.composed.plan.zones.size(), world.composed.clearances.size());

    if (ImGui::TreeNodeEx("Heroes", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Listed in the order the composer ranked them, because that order *is* the composition:
        // it is what a camera director sorts by, so seeing it is seeing what the shot will be about.
        for (const world::HeroPoint& hero : world.composed.plan.heroes) {
            ImGui::PushID(hero.name.c_str());
            const bool open = ImGui::TreeNodeEx(hero.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth,
                                                "%s  (%.2f)", hero.name.c_str(),
                                                static_cast<double>(hero.importance));
            if (open) {
                ImGui::Text("%.0f m tall, stand-off %.0f m, active within %.0f m",
                            static_cast<double>(hero.height),
                            static_cast<double>(hero.preferredCameraDistance),
                            static_cast<double>(hero.activationRadius));
                ImGui::Text("reacts as: %s",
                            hero.reactionProfile.empty() ? "nothing" : hero.reactionProfile.c_str());
                // The node this hero became is an ordinary node, so its transform is edited through
                // the parameters every other node uses. Duplicating a transform editor here would be
                // a second place that moves things, and the two would disagree.
                const std::string prefix = "nodes/" + hero.name + "/";
                if (engine.params().find(prefix + "position") != nullptr) {
                    ImGui::TextDisabled("edit as '%s*' in the inspector", prefix.c_str());
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "not placed in the scene");
                }
                if (ImGui::SmallButton("Frame it")) {
                    // Handled by the viewport, which owns the camera. The panel only asks.
                    focusRequest = hero;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Zones")) {
        for (const world::EcologicalZone& zone : world.composed.plan.zones) {
            ImGui::PushID(zone.name.c_str());
            if (ImGui::TreeNodeEx(zone.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%.0f m)",
                                  zone.name.c_str(), static_cast<double>(zone.radius))) {
                for (const auto& [category, scale] : zone.emphasis) {
                    ImGui::Text("%s x%.2f", category.c_str(), static_cast<double>(scale));
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Art direction")) {
        const world::ArtDirectionProfile& p = world.composed.profile;
        ImGui::Text("%s", p.name.c_str());
        ImGui::TextWrapped("%s", p.description.c_str());
        ImGui::Text("ladder %.3f .. %.2f (gap %.1fx)", static_cast<double>(p.emission.silhouette),
                    static_cast<double>(p.emission.brightest), static_cast<double>(p.emission.gap()));
        ImGui::Text("key %.1f : ambient %.1f  (%.1f:1)", static_cast<double>(p.lighting.keyIntensity),
                    static_cast<double>(p.lighting.ambientIntensity),
                    static_cast<double>(p.lighting.keyToAmbient()));
        ImGui::ColorButton("accent", ImVec4(p.heroAccent.r, p.heroAccent.g, p.heroAccent.b, 1.0f));
        ImGui::SameLine();
        ImGui::TextDisabled("reserved accent%s", p.reserveAccent ? "" : " (released)");
        ImGui::TreePop();
    }
}

void WorldBuilderPanel::drawJobs(app::JobSystem& jobs) {
    const auto statuses = jobs.statuses();
    ImGui::Text("Jobs (%zu)", statuses.size());
    if (statuses.empty()) {
        ImGui::TextDisabled("nothing running");
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear finished")) {
        jobs.clearFinished();
    }

    for (const auto& job : statuses) {
        ImGui::PushID(static_cast<int>(job.id));
        const bool open = ImGui::TreeNodeEx(
            job.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s  [%s]", job.name.c_str(), app::jobStateName(job.state));
        if (open) {
            if (job.stageCount > 1 || !job.stageName.empty()) {
                ImGui::Text("Stage %d / %d: %s", job.stageIndex + 1, job.stageCount,
                            job.stageName.c_str());
            }
            if (!job.currentOperation.empty()) {
                ImGui::TextDisabled("%s", job.currentOperation.c_str());
            }
            // The honest bit. A job whose stages cannot measure themselves gets a striped bar and
            // the word indeterminate, not a number somebody might act on.
            if (job.progressKnown) {
                ImGui::ProgressBar(job.progress, ImVec2(-1.0f, 0.0f));
            } else {
                ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()),
                                   ImVec2(-1.0f, 0.0f), "indeterminate");
            }
            ImGui::Text("Elapsed %s", clockText(job.elapsedSeconds).c_str());
            ImGui::SameLine();
            if (job.estimatedRemainingSeconds >= 0.0) {
                ImGui::Text("| Remaining %s", clockText(job.estimatedRemainingSeconds).c_str());
            } else {
                ImGui::TextDisabled("| Remaining: calculating...");
            }
            if (!job.error.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", job.error.c_str());
            }
            if (job.cancellable && !app::jobStateIsTerminal(job.state)) {
                if (ImGui::SmallButton("Cancel")) {
                    static_cast<void>(jobs.cancel(job.id));
                }
            }
            if (!job.logs.empty() && ImGui::TreeNode("Details")) {
                for (const auto& line : job.logs) {
                    ImGui::TextWrapped("%s", line.c_str());
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void WorldBuilderPanel::applyFinished(app::Engine& engine, app::WorldBuilder& builder) {
    for (const auto& world : builder.collect()) {
        if (engine.composition() == nullptr) {
            engine.newComposition();
        }
        if (auto installed = app::installWorld(engine, world); !installed) {
            status_ = installed.error().message;
            log::warn("world builder: {}", status_);
        } else {
            status_ = "'" + world.recipe.world + "': " +
                      std::to_string(world.composed.layers.size()) + " layer(s) in " +
                      std::to_string(world.composeSeconds) + " s";
            lastWorld = world;
        }
    }
}

} // namespace avgen::ui
