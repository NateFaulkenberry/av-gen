#include "ui/feeds_row.hpp"

#include "app/engine.hpp"
#include "ui/style.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace avgen::ui {

namespace {

// The keys a feed can name now: the roles the sources have (ADR-1630: "role.<role>"), then the stems, then (while an
// input is open) the input sources not already listed.
std::vector<std::string> feedKeys(const app::Engine& engine) {
    std::vector<std::string> keys;
    const auto add = [&](const std::string& k) {
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) {
            keys.push_back(k);
        }
    };
    for (const auto& stem : engine.stems()) {
        add(audio::roleFeedKey(stem.source.role));
    }
    if (engine.audioInput() != nullptr) {
        for (const auto& source : engine.inputSources()) {
            add(audio::roleFeedKey(source.role));
        }
    }
    std::sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b) {
        return *audio::feedRole(a) < *audio::feedRole(b);
    });
    for (const auto& stem : engine.stems()) {
        add(stem.source.key);
    }
    if (engine.audioInput() != nullptr) {
        for (const auto& source : engine.inputSources()) {
            add(source.key);
        }
    }
    return keys;
}

// How a feed key reads in a combo: a key as it is, a role reference as "role: drums".
std::string display(const std::string& key) {
    if (const auto role = audio::feedRole(key)) {
        return std::string("role: ") + audio::sourceRoleName(*role);
    }
    return key;
}

// One combo. `custom` shows "custom" as the preview (per-class hit feeds). Returns whether `key` changed.
bool feedCombo(const char* label, std::string& key, const std::vector<std::string>& keys, bool custom,
               const app::Engine& engine) {
    std::string preview = custom ? std::string("custom") : key.empty() ? std::string("Main mix") : display(key);
    if (!key.empty() && !custom) {
        const app::Engine::FeedState st = engine.feedState(key);
        if (!st.resolved) {
            preview += " (missing)";
        } else if (!st.active) {
            preview += " (inactive)";
        }
    }
    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.5f);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable("Main mix", key.empty() && !custom)) {
            changed = !key.empty() || custom;
            key.clear();
        }
        for (const std::string& k : keys) {
            if (ImGui::Selectable(display(k).c_str(), key == k && !custom)) {
                changed = key != k || custom;
                key = k;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

void drawFeedsRow(app::Engine& engine) {
    const std::vector<std::string> keys = feedKeys(engine);
    audio::Feeds feeds = engine.feeds();
    if (keys.empty() && feeds.empty()) {
        return;
    }
    bool changed = false;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Feeds");
    if (ImGui::IsItemHovered()) {
        tooltip("Which source drives the beat clock (beat.*, bars, music.*) and feeds response.* -- the hits (low, "
                "onset) and the levels (bass, level, sustain, ...). Main mix is how every project works without this. "
                "A role names the first source of that role, whatever its key (ADR-1630). A source here gets the hits "
                "analysis. An inactive source feeds nothing; a missing one falls back to the mix. Saved in the project "
                "as \"feeds\".");
    }
    ImGui::SameLine();
    changed |= feedCombo("Beat", feeds.beat, keys, false, engine);
    if (ImGui::IsItemHovered()) {
        tooltip("The beat clock from this source: a clean kick or drum bus gives a steadier grid than a full mix. A "
                "running MIDI clock still wins.");
    }
    ImGui::SameLine();
    const bool custom = std::any_of(feeds.hit.begin(), feeds.hit.end(), [](const auto& h) { return h.has_value(); });
    if (feedCombo("Hits", feeds.hits, keys, custom, engine)) {
        feeds.hit = {};
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        if (custom) {
            std::string list;
            for (std::size_t k = 0; k < feeds.hit.size(); ++k) {
                list += std::string(audio::kFeedHitNames[k]) + ": " +
                        (feeds.hitSource(k).empty() ? std::string("main mix") : display(feeds.hitSource(k))) +
                        "\n";
            }
            tooltip("Per class (from the project file):\n%sChoosing an entry sets both.", list.c_str());
        } else {
            tooltip("response.low and response.onset (their envelopes and the onset's register) from this source.");
        }
    }
    ImGui::SameLine();
    changed |= feedCombo("Levels", feeds.levels, keys, false, engine);
    if (ImGui::IsItemHovered()) {
        tooltip("response.bass, level, levelSlow, intensity, sustain, transient and flux from this source, read "
                "against its own loudness (ADR-1510). Melodic and pitch stay on the mix.");
    }
    ImGui::SameLine();
    // ADR-1631: the audio history spectrum and onset fields hear (Resonator, the generative fields).
    const bool historyCustom = feeds.historySpectrum.has_value() || feeds.historyOnsets.has_value();
    if (feedCombo("History", feeds.history, keys, historyCustom, engine)) {
        feeds.historySpectrum.reset();
        feeds.historyOnsets.reset();
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        if (historyCustom) {
            tooltip("Spectrum: %s\nOnsets: %s\n(from the project file). Choosing an entry sets both.",
                    feeds.spectrumSource().empty() ? "main mix" : display(feeds.spectrumSource()).c_str(),
                    feeds.onsetsSource().empty() ? "main mix" : display(feeds.onsetsSource()).c_str());
        } else {
            tooltip("What spectrum and onset fields hear (Resonator, the generative fields): this source's spectrogram "
                    "and onsets. A drums source's kicks, snares and hats are the low, mid and high onsets; the mix's and "
                    "any other source's onsets land on the row of their register. The beat stays the mix's.");
        }
    }
    if (changed) {
        engine.setFeeds(std::move(feeds));
    }
}

bool roleCombo(const char* label, audio::SourceRole& role) {
    bool changed = false;
    if (ImGui::BeginCombo(label, audio::sourceRoleName(role))) {
        for (std::size_t i = 0; i < audio::kSourceRoleCount; ++i) {
            const auto r = static_cast<audio::SourceRole>(i);
            if (ImGui::Selectable(audio::sourceRoleName(r), r == role)) {
                changed = r != role;
                role = r;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace avgen::ui
