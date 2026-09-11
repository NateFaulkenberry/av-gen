#include "ui/settings_panel.hpp"

#include "ai/control_plane.hpp"
#include "app/engine.hpp"
#include "app/settings.hpp"
#include "ui/theme.hpp"

#include <imgui.h>

#include <algorithm>

namespace avgen::ui {
namespace {

constexpr std::array<const char*, static_cast<std::size_t>(SettingsPanel::Section::Count)>
    kSectionNames{{"General", "Rendering", "AI", "Advanced"}};

void resizableBuffer(std::string& buffer, std::size_t capacity) {
    if (buffer.size() < capacity) {
        buffer.resize(capacity, '\0');
    }
}

} // namespace

void SettingsPanel::draw(app::Engine& engine) {
    (void)engine;

    // Sections down the left, content on the right. The shape every settings window in every
    // creative tool has, for the reason they all have it: the list is the table of contents.
    const float listWidth = ImGui::CalcTextSize("Rendering").x * 2.4f;
    if (ImGui::BeginChild("settings-sections", ImVec2(listWidth, 0), ImGuiChildFlags_Borders)) {
        for (int i = 0; i < static_cast<int>(Section::Count); ++i) {
            const bool selected = static_cast<int>(section_) == i;
            if (ImGui::Selectable(kSectionNames[static_cast<std::size_t>(i)], selected)) {
                section_ = static_cast<Section>(i);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("settings-content", ImVec2(0, 0), ImGuiChildFlags_None)) {
        switch (section_) {
        case Section::General: drawGeneral(); break;
        case Section::Rendering: drawRendering(); break;
        case Section::Ai: drawAi(); break;
        case Section::Advanced: drawAdvanced(); break;
        default: break;
        }
        if (!status_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", status_.c_str());
        }
    }
    ImGui::EndChild();
}

void SettingsPanel::drawGeneral() {
    ImGui::TextDisabled("Settings that belong to this installation rather than to a project.");
    ImGui::Separator();
    if (settings != nullptr) {
        int appearance = static_cast<int>(settings->appearance);
        ImGui::TextDisabled("Appearance");
        if (ImGui::Combo("Theme", &appearance, "System\0Dark\0Light\0")) {
            settings->appearance = static_cast<app::AppearanceTheme>(std::clamp(appearance, 0, 2));
            if (onAppearanceChanged) {
                onAppearanceChanged(settings->appearance);
            }
            if (onChanged) {
                onChanged();
            }
        }
    }
    ImGui::Separator();
    ImGui::TextWrapped(
        "Panel layout is stored separately and restored automatically. Use View > Restore Default "
        "Layout to put the panels back.");
}

void SettingsPanel::drawRendering() {
    if (canvasRenderScale == nullptr) {
        ImGui::TextDisabled("No renderer is attached to this session.");
        return;
    }
    ImGui::TextDisabled("How the editor's viewport is rendered. Offline render settings are per "
                        "project and live in the Render panel.");
    ImGui::Separator();
    float scale = *canvasRenderScale;
    if (ImGui::SliderFloat("Canvas render scale", &scale, 0.25f, 1.0f, "%.2f")) {
        *canvasRenderScale = scale;
        if (settings != nullptr) {
            settings->canvasRenderScale = scale;
        }
        if (onChanged) {
            onChanged();
        }
    }
    ImGui::TextWrapped(
        "The world is rendered at this fraction of the viewport's pixels and shown stretched to "
        "fill it. Below 1.0 trades sharpness for frame rate, which on a high-density display is "
        "often the better trade.");
}

void SettingsPanel::drawAi() {
    if (plane == nullptr) {
        ImGui::TextDisabled("This build has no AI control plane.");
        return;
    }
    auto& aiSettings = plane->settings();
    const std::vector<avgen::ai::ProviderStatus> statuses = plane->providerStatus();

    if (!fieldsSeeded_) {
        // Seeded once from settings so typing in a field is not fought by a redraw. Endpoints and
        // models only -- there is deliberately nothing to seed a secret field from.
        for (std::size_t i = 0; i < statuses.size() && i < kMaxProviders; ++i) {
            resizableBuffer(endpoints_[i], 512);
            resizableBuffer(models_[i], 128);
            resizableBuffer(secrets_[i], kSecretCapacity);
            std::copy_n(statuses[i].endpoint.c_str(),
                        std::min(statuses[i].endpoint.size() + 1, std::size_t{512}),
                        endpoints_[i].data());
            std::copy_n(statuses[i].model.c_str(),
                        std::min(statuses[i].model.size() + 1, std::size_t{128}), models_[i].data());
        }
        fieldsSeeded_ = true;
    }

    ImGui::TextWrapped(
        "AV Gen works completely normally with no provider configured; the AI panel simply says "
        "so. Configure one here to use it.");
    ImGui::Separator();

    // Which provider is in use. One radio column rather than a combo, because the rows below are
    // what a person is reading anyway.
    ImGui::TextDisabled("Active provider");
    if (ImGui::RadioButton("None", aiSettings.activeProvider.empty())) {
        aiSettings.activeProvider.clear();
        (void)plane->applySettings();
        if (onChanged) {
            onChanged();
        }
    }
    ImGui::Separator();

    for (std::size_t i = 0; i < statuses.size() && i < kMaxProviders; ++i) {
        const avgen::ai::ProviderStatus& status = statuses[i];
        avgen::ai::ProviderConfig* config = aiSettings.find(status.id);
        if (config == nullptr) {
            continue;
        }
        ImGui::PushID(static_cast<int>(i));

        const bool active = aiSettings.activeProvider == status.id;
        if (ImGui::RadioButton(status.displayName.c_str(), active)) {
            aiSettings.activeProvider = status.id;
            config->enabled = true;
            if (auto r = plane->applySettings(); !r) {
                status_ = r.error().message;
            } else {
                status_ = status.displayName + " is now the active provider.";
            }
            if (onChanged) {
                onChanged();
            }
        }
        ImGui::Indent();

        // Whether it is configured, and where the credential came from -- never what it is.
        if (status.credential.configured) {
            ImGui::TextColored(ImVec4(0.58f, 0.86f, 0.62f, 1.0f), "Credential: %s",
                               status.credential.detail.c_str());
        } else if (status.requiresCredential) {
            ImGui::TextColored(ImVec4(1.0f, 0.76f, 0.44f, 1.0f), "No credential stored");
        } else {
            ImGui::TextDisabled("No credential needed");
        }

        if (!status.credentialHint.empty()) {
            ImGui::TextWrapped("%s", status.credentialHint.c_str());
        }

        // The masked entry. Always empty on arrival: a stored secret is never read back for
        // display, so there is nothing to put in it.
        resizableBuffer(secrets_[i], kSecretCapacity);
        ImGui::SetNextItemWidth(-140.0f);
        ImGui::InputTextWithHint("##secret", "paste a key to store it", secrets_[i].data(),
                                 secrets_[i].size(), ImGuiInputTextFlags_Password);
        ImGui::SameLine();
        if (ImGui::Button("Store")) {
            const std::string secret(secrets_[i].c_str());
            if (secret.empty()) {
                status_ = "nothing to store";
            } else if (auto r = plane->credentials().store(status.id, secret); !r) {
                status_ = r.error().message;
            } else {
                status_ = fmt::format("stored a credential for {} in the {}", status.displayName,
                                      plane->credentials().backend());
                (void)plane->applySettings();
            }
            // Cleared immediately either way: a key left in a UI buffer is a key on screen the
            // next time somebody opens this panel.
            std::fill(secrets_[i].begin(), secrets_[i].end(), '\0');
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!status.credential.configured ||
                             status.credential.source != avgen::ai::CredentialSource::Keystore);
        if (ImGui::Button("Forget")) {
            if (auto r = plane->credentials().erase(status.id); !r) {
                status_ = r.error().message;
            } else {
                status_ = "removed the stored credential for " + status.displayName;
                (void)plane->applySettings();
            }
        }
        ImGui::EndDisabled();

        // Endpoint: required for the compatible and local providers, optional elsewhere.
        resizableBuffer(endpoints_[i], 512);
        ImGui::SetNextItemWidth(-140.0f);
        if (ImGui::InputText("Endpoint", endpoints_[i].data(), endpoints_[i].size())) {
            config->endpoint = std::string(endpoints_[i].c_str());
            if (onChanged) {
                onChanged();
            }
        }
        resizableBuffer(models_[i], 128);
        ImGui::SetNextItemWidth(-140.0f);
        if (ImGui::InputTextWithHint("Model", status.defaultModel.empty() ? "model id"
                                                                          : status.defaultModel.c_str(),
                                     models_[i].data(), models_[i].size())) {
            config->model = std::string(models_[i].c_str());
            if (onChanged) {
                onChanged();
            }
        }

        // A round trip that proves the credential, the endpoint, the model and the network all at
        // once -- which is the only check worth offering.
        if (ImGui::Button("Test connection")) {
            // Synchronous, and deliberately so: this is a single short request the user just asked
            // for and is waiting on, not background work. It is the one place in the AI subsystem
            // that blocks the UI thread, it is bounded by the HTTP timeout, and it happens only on
            // an explicit press.
            if (auto r = plane->testProvider(status.id); !r) {
                plane->recordTestResult(status.id, false, r.error().message);
                status_ = r.error().message;
            } else {
                plane->recordTestResult(status.id, true, *r);
                status_ = *r;
            }
        }
        if (!status.lastTestResult.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(status.lastTestOk ? ImVec4(0.58f, 0.86f, 0.62f, 1.0f)
                                                 : ImVec4(1.0f, 0.62f, 0.52f, 1.0f),
                               "%s", status.lastTestResult.c_str());
        }

        ImGui::Unindent();
        ImGui::Separator();
        ImGui::PopID();
    }

    // Budgets (§55). Exposed because a person paying for tokens should be able to see and change
    // the ceiling, not because the agent needs it configurable.
    ImGui::TextDisabled("Limits for one request");
    bool changed = false;
    changed |= ImGui::SliderInt("Model turns", &aiSettings.limits.maxIterations, 1, 40);
    changed |= ImGui::SliderInt("Tool calls", &aiSettings.limits.maxToolCalls, 1, 200);
    float seconds = static_cast<float>(aiSettings.limits.maxSeconds);
    if (ImGui::SliderFloat("Time limit (s)", &seconds, 10.0f, 900.0f, "%.0f")) {
        aiSettings.limits.maxSeconds = static_cast<double>(seconds);
        changed = true;
    }
    if (changed) {
        (void)plane->applySettings();
        if (onChanged) {
            onChanged();
        }
    }
}

void SettingsPanel::drawAdvanced() {
    ImGui::TextDisabled("Where things are stored.");
    ImGui::Separator();
    if (settingsFile.empty()) {
        ImGui::TextWrapped("This session has no preferences directory, so settings are not saved.");
    } else {
        ImGui::TextWrapped("Settings: %s", settingsFile.c_str());
    }
    if (plane != nullptr) {
        ImGui::Spacing();
        ImGui::TextWrapped("AI credentials: %s, under the service \"com.avgen.ai\".",
                           std::string(plane->credentials().backend()).c_str());
        // Said accurately, because the measured behaviour does not support a stronger claim. See
        // src/ai/credentials.hpp.
        ImGui::TextWrapped(
            "Keys are never written into settings, projects, scenes, logs or conversation history, "
            "and are never shown again after entry. On macOS they are stored in the login keychain, "
            "where you can review, rotate or delete them in Keychain Access. That keeps them out of "
            "files you might share or commit; it does not sandbox them from other programs running "
            "as you.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "An AVGEN_AI_<PROVIDER>_KEY environment variable overrides the stored key for that "
            "provider, which is how a headless render or a CI run is configured.");
    }
}

} // namespace avgen::ui
