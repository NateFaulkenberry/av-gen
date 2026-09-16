#include "ui/editor_layout.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

namespace avgen::ui {

using nlohmann::json;

namespace {

constexpr const char* kFormatName = "avgen-editor-layout";
constexpr int kFormatVersion = 1;

// The panels, grouped the way the View menu reads them: what you build with on the left, what you
// inspect and tune on the right, what runs underneath along the bottom. Nothing is assigned to the
// centre -- see DockRegion.
constexpr std::array<EditorPanel, 20> kPanels{{
    {"World Builder", "World Builder", DockRegion::Left, true,
     "recipe, Generate World and the job monitor"},
    // ADR-092. One panel, not two: the brush and the selection are the same job seen from two
    // sides, and an artist who has to remember which of two windows holds "rotate" has been given a
    // filing system rather than a tool. It shows the controls for the mode it is in.
    {"Edit", "Edit", DockRegion::Left, true,
     "place and paint assets, select, move, group and undo"},
    {"Assets", "Assets", DockRegion::Left, false, "everything the asset scan found, by kind"},
    {"World", "World", DockRegion::Right, true,
     "layers, inspector, scene states, direction, macros and debug draw"},
    {"Parameters", "Parameters", DockRegion::Right, true, "every exposed parameter, by group"},
    {"Composition", "Composition", DockRegion::Right, true,
     "the 2D layers over the frame: text, shapes, timing and keys"},
    {"Sequence", "Sequence", DockRegion::Bottom, true,
     "the piece in time: shots, scene cuts, character cues, lyrics and markers"},
    {"Render", "Render", DockRegion::Right, false, "offline render settings, progress and the queue"},
    {"Auto-director", "Auto-director", DockRegion::Right, false,
     "shot mode, timings, lenses and the seed for the camera that cuts to the music"},
    // ADR-245. Beside the Auto-director, because the Auto-director is now one of the cameras in
    // this list rather than the only camera there is.
    {"Cameras", "Cameras", DockRegion::Right, false,
     "the camera library: what exists, which one is live, and when each is used"},
    // ADR-207. Beside the Auto-director, because the two shipped effects are gated on its cut and
    // "why is my beam not firing" is nearly always "nothing is directing the camera".
    {"World Effects", "World Effects", DockRegion::Right, false,
     "waves propagating through the world: beams, ripples, their styles and what answers the beat"},
    {"Control", "Control", DockRegion::Bottom, true, "transport, audio response and performance"},
    // §13. The data has been machine-readable since ADR-148 and had no surface: the answer to "why
    // is this frame expensive" lived in `--bench-json` and in the log. Closed by its own panel
    // rather than by another section of Control, because the question is asked while looking at a
    // frame that is too slow, and that is not the moment to go hunting inside a transport.
    {"Performance", "Performance", DockRegion::Right, false,
     "where the frame's time goes: GPU passes, CPU phases, what it contains, and the isolation arms"},
    {"Analysis", "Analysis", DockRegion::Bottom, true, "bands, spectrum, onsets and the waveform"},
    {"Modulation", "Modulation", DockRegion::Bottom, true,
     "routes, sources, presets, shaders, scene, timeline, control and outputs"},
    {"Graph", "Graph", DockRegion::Bottom, false, "the procedural graph editor"},
    {"Help", "Help", DockRegion::Right, false,
     "the documentation: categories, search, shortcuts and contextual topics"},
    {"AI", "AI Assistant", DockRegion::Right, false,
     "ask for changes in words; the assistant inspects the project and makes them"},
    {"Settings", "Settings", DockRegion::Right, false,
     "application settings: rendering, AI providers and credentials"},
    {"Dear ImGui Demo", "ImGui Demo", DockRegion::Floating, false, "the Dear ImGui widget gallery"},
}};

std::size_t regionIndex(DockRegion region) { return static_cast<std::size_t>(region); }

} // namespace

const char* dockRegionName(DockRegion region) {
    switch (region) {
    case DockRegion::Left:
        return "left";
    case DockRegion::Right:
        return "right";
    case DockRegion::Bottom:
        return "bottom";
    case DockRegion::Centre:
        return "centre";
    case DockRegion::Floating:
        return "floating";
    }
    return "floating";
}

CanvasPixel canvasPixelFor(const CanvasRect& canvas, float windowX, float windowY, float scale,
                           std::uint32_t pixelWidth, std::uint32_t pixelHeight) {
    const float localX = canvas.valid() ? canvas.localX(windowX) : windowX;
    const float localY = canvas.valid() ? canvas.localY(windowY) : windowY;
    const float safeScale = std::max(scale, 1e-3f);
    const auto pin = [](float value, std::uint32_t limit) {
        const float top = limit == 0 ? 0.0f : static_cast<float>(limit - 1);
        return static_cast<std::uint32_t>(std::clamp(value, 0.0f, top));
    };
    return {pin(localX * safeScale, pixelWidth), pin(localY * safeScale, pixelHeight)};
}

std::span<const EditorPanel> editorPanels() { return {kPanels.data(), kPanels.size()}; }

const EditorPanel* findEditorPanel(std::string_view id) {
    const auto it = std::ranges::find(kPanels, id, &EditorPanel::id);
    return it == kPanels.end() ? nullptr : &*it;
}

LayoutRatios defaultLayoutRatios() { return {}; }

float splitRatioWithin(float fractionOfWhole, float remainingFractionOfWhole) {
    if (remainingFractionOfWhole <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(fractionOfWhole / remainingFractionOfWhole, 0.0f, 0.9f);
}

EditorLayout::EditorLayout() {
    flags_.reserve(kPanels.size());
    for (const EditorPanel& panel : kPanels) {
        flags_.push_back({panel.id, panel.openByDefault});
    }
}

void EditorLayout::restoreDefaults() {
    for (Flag& flag : flags_) {
        const EditorPanel* panel = findEditorPanel(flag.id);
        flag.on = panel != nullptr && panel->openByDefault;
    }
    for (std::uint32_t& node : regionNodes_) {
        node = 0;
    }
}

const EditorLayout::Flag* EditorLayout::find(std::string_view id) const {
    const auto it = std::ranges::find(flags_, id, &Flag::id);
    return it == flags_.end() ? nullptr : &*it;
}

EditorLayout::Flag* EditorLayout::find(std::string_view id) {
    const auto it = std::ranges::find(flags_, id, &Flag::id);
    return it == flags_.end() ? nullptr : &*it;
}

bool EditorLayout::visible(std::string_view id) const {
    const Flag* flag = find(id);
    return flag != nullptr && flag->on;
}

void EditorLayout::setVisible(std::string_view id, bool on) {
    if (Flag* flag = find(id); flag != nullptr) {
        flag->on = on;
    }
}

bool* EditorLayout::slot(std::string_view id) {
    Flag* flag = find(id);
    return flag == nullptr ? nullptr : &flag->on;
}

std::uint32_t EditorLayout::regionNode(DockRegion region) const { return regionNodes_[regionIndex(region)]; }

void EditorLayout::setRegionNode(DockRegion region, std::uint32_t node) {
    regionNodes_[regionIndex(region)] = node;
}

std::uint64_t EditorLayout::signature() const {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 0x100000001b3ull;
    };
    for (const Flag& flag : flags_) {
        mix(flag.on ? 1u : 0u);
    }
    for (const std::uint32_t node : regionNodes_) {
        mix(node);
    }
    return hash;
}

Result<void> EditorLayout::load(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        return {};
    }
    std::ifstream in(file);
    if (!in) {
        return fail("cannot open '{}' for reading", file.string());
    }
    const json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail("'{}' is not a valid editor layout", file.string());
    }
    const auto format = doc.find("format");
    if (format == doc.end() || !format->is_string() || format->get<std::string>() != kFormatName) {
        return fail("'{}' is not an editor layout: expected format '{}'", file.string(), kFormatName);
    }
    const auto version = doc.find("version");
    if (version == doc.end() || !version->is_number_integer()) {
        return fail("'{}' is missing an integer 'version'", file.string());
    }
    if (version->get<int>() > kFormatVersion || version->get<int>() < 1) {
        return fail("'{}' has unsupported editor layout version {}", file.string(), version->get<int>());
    }

    // Parsed into locals first: a file that fails halfway through must not leave half of the
    // previous layout replaced, which is the state nobody can describe in a bug report.
    std::vector<Flag> flags = flags_;
    std::uint32_t nodes[5] = {0, 0, 0, 0, 0};
    if (const auto panels = doc.find("panels"); panels != doc.end()) {
        if (!panels->is_object()) {
            return fail("'{}': 'panels' must be an object", file.string());
        }
        for (const auto& [id, value] : panels->items()) {
            if (!value.is_boolean()) {
                return fail("'{}': panel '{}' must be true or false", file.string(), id);
            }
            const auto it = std::ranges::find(flags, id, &Flag::id);
            if (it != flags.end()) {
                it->on = value.get<bool>();
            }
        }
    }
    if (const auto regions = doc.find("regions"); regions != doc.end()) {
        if (!regions->is_object()) {
            return fail("'{}': 'regions' must be an object", file.string());
        }
        for (const DockRegion region : {DockRegion::Left, DockRegion::Right, DockRegion::Bottom,
                                        DockRegion::Centre}) {
            const auto node = regions->find(dockRegionName(region));
            if (node != regions->end() && node->is_number_unsigned()) {
                nodes[regionIndex(region)] = node->get<std::uint32_t>();
            }
        }
    }
    // Copied in element by element rather than assigned over: slot() hands out pointers into
    // flags_, and replacing the vector would move the buffer out from under any that are live.
    for (std::size_t i = 0; i < flags_.size(); ++i) {
        flags_[i].on = flags[i].on;
    }
    std::ranges::copy(nodes, std::begin(regionNodes_));
    return {};
}

Result<void> EditorLayout::save(const std::filesystem::path& file) const {
    std::error_code ec;
    if (const std::filesystem::path parent = file.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    json doc;
    doc["format"] = kFormatName;
    doc["version"] = kFormatVersion;
    json panels = json::object();
    for (const Flag& flag : flags_) {
        panels[std::string(flag.id)] = flag.on;
    }
    doc["panels"] = std::move(panels);
    json regions = json::object();
    for (const DockRegion region : {DockRegion::Left, DockRegion::Right, DockRegion::Bottom,
                                    DockRegion::Centre}) {
        regions[dockRegionName(region)] = regionNodes_[regionIndex(region)];
    }
    doc["regions"] = std::move(regions);

    std::ofstream out(file);
    if (!out) {
        return fail("cannot open '{}' for writing", file.string());
    }
    out << doc.dump(2) << '\n';
    if (!out) {
        return fail("failed to write '{}'", file.string());
    }
    return {};
}


std::size_t EditorLayout::ensureRegionsOccupied() {
    std::size_t reopened = 0;
    for (const DockRegion region : {DockRegion::Left, DockRegion::Right, DockRegion::Bottom}) {
        const EditorPanel* first = nullptr;
        bool occupied = false;
        for (const EditorPanel& panel : editorPanels()) {
            if (panel.region != region) {
                continue;
            }
            if (first == nullptr) {
                first = &panel;
            }
            if (visible(panel.id)) {
                occupied = true;
                break;
            }
        }
        if (!occupied && first != nullptr) {
            setVisible(first->id, true);
            ++reopened;
        }
    }
    return reopened;
}

} // namespace avgen::ui
