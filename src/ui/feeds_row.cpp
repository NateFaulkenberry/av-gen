#include "ui/feeds_row.hpp"

#include "app/engine.hpp"
#include "ui/style.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace avgen::ui {

namespace {

// The keys a feed can name now: the stems, then (while an input is open) the input sources not already listed.
std::vector<std::string> feedKeys(const app::Engine& engine) {
    std::vector<std::string> keys;
    for (const auto& stem : engine.stems()) {
        keys.push_back(stem.source.key);
    }
    if (engine.audioInput() != nullptr) {
        for (const auto& source : engine.inputSources()) {
            if (std::find(keys.begin(), keys.end(), source.key) == keys.end()) {
                keys.push_back(source.key);
            }
        }
    }
    return keys;
}

// One combo. `custom` shows "custom" as the preview (per-class hit feeds). Returns whether `key` changed.
bool feedCombo(const char* label, std::string& key, const std::vector<std::string>& keys, bool custom,
               const app::Engine& engine) {
    std::string preview = custom ? std::string("custom") : key.empty() ? std::string("Main mix") : key;
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
            if (ImGui::Selectable(k.c_str(), key == k && !custom)) {
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
        tooltip("Which source drives the beat clock (beat.*, bars, music.*) and feeds response.* -- the hits (kick, "
                "low, snare, hat, onset) and the levels (bass, level, sustain, ...). Main mix is how every project "
                "works without this. A source here gets the hits analysis. An inactive source feeds nothing; a "
                "missing one falls back to the mix. Saved in the project as \"feeds\".");
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
                        (feeds.hitSource(k).empty() ? std::string("main mix") : feeds.hitSource(k)) + "\n";
            }
            tooltip("Per class (from the project file):\n%sChoosing an entry sets all five.", list.c_str());
        } else {
            tooltip("response.kick, low, snare, hat and onset (and their envelopes) from this source.");
        }
    }
    ImGui::SameLine();
    changed |= feedCombo("Levels", feeds.levels, keys, false, engine);
    if (ImGui::IsItemHovered()) {
        tooltip("response.bass, level, levelSlow, intensity, sustain, transient and flux from this source, read "
                "against its own loudness (ADR-1510). Melodic and pitch stay on the mix.");
    }
    if (changed) {
        engine.setFeeds(std::move(feeds));
    }
}

} // namespace avgen::ui
