#include "ui/world_builder_panel.hpp"

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
        ImGui::SetTooltip("%s", tooltip);
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
        }
    }

    if (ImGui::CollapsingHeader("Recipe", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawRecipe();
    }

    ImGui::Separator();
    if (libraryCount_ > 0) {
        ImGui::TextDisabled("Library: %zu assets (%s)", libraryCount_, libraryLabel_.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", libraryPath_.string().c_str());
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
        }
    }
}

} // namespace avgen::ui
