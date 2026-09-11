// The editor shell's rules (ADR-076). What is checked here is what quietly breaks: a panel added
// to the UI and forgotten by the registry becomes unreachable, a panel assigned to the centre
// covers the world, a second dock split comes out narrower than the number that asked for it, and
// "restore default layout" turns into "delete the file and hope".

#include "ui/editor_layout.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace avgen::ui;

namespace {

std::filesystem::path scratchDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "avgen-editor-layout" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("every editor panel is named, unique and reachable from the View menu", "[ui][layout]") {
    const auto panels = editorPanels();
    REQUIRE(panels.size() >= 8);

    std::set<std::string_view> ids;
    std::set<std::string_view> labels;
    for (const EditorPanel& panel : panels) {
        INFO("panel '" << panel.id << "'");
        // The id is the ImGui window title and the key in the layout file; an empty one would dock
        // nothing and save nothing.
        REQUIRE_FALSE(panel.id.empty());
        REQUIRE_FALSE(panel.label.empty());
        REQUIRE(ids.insert(panel.id).second);
        REQUIRE(labels.insert(panel.label).second);
        REQUIRE(findEditorPanel(panel.id) == &panel);
    }
    REQUIRE(findEditorPanel("a panel nobody wrote") == nullptr);
}

TEST_CASE("the default layout leaves the centre of the dockspace to the world", "[ui][layout]") {
    for (const EditorPanel& panel : editorPanels()) {
        INFO("panel '" << panel.id << "' claims the " << dockRegionName(panel.region) << " region");
        REQUIRE(panel.region != DockRegion::Centre);
    }
    // And the three side regions are all used, or the shell has columns with nothing in them.
    bool left = false;
    bool right = false;
    bool bottom = false;
    for (const EditorPanel& panel : editorPanels()) {
        left = left || panel.region == DockRegion::Left;
        right = right || panel.region == DockRegion::Right;
        bottom = bottom || panel.region == DockRegion::Bottom;
    }
    REQUIRE(left);
    REQUIRE(right);
    REQUIRE(bottom);
}

TEST_CASE("the default layout gives the world more room than anything around it", "[ui][layout]") {
    const LayoutRatios ratios = defaultLayoutRatios();
    REQUIRE(ratios.left > 0.0f);
    REQUIRE(ratios.right > 0.0f);
    REQUIRE(ratios.bottom > 0.0f);
    // The governing principle, as arithmetic: the canvas is wider than either column and taller
    // than the strip under it, and it is more than half the window.
    REQUIRE(ratios.centreWidth() > ratios.left + ratios.right);
    REQUIRE(ratios.centreHeight() > ratios.bottom);
    REQUIRE(ratios.centreWidth() * ratios.centreHeight() > 0.4f);
}

TEST_CASE("a split ratio is restated against the node that is actually left", "[ui][layout]") {
    const LayoutRatios ratios = defaultLayoutRatios();
    // The right-hand column is cut from what survives the left-hand one. Handing ImGui the raw
    // fraction there is the mistake this function exists to stop, and it costs the column about a
    // fifth of its width without anything looking obviously wrong.
    const float remaining = 1.0f - ratios.left;
    const float ratio = splitRatioWithin(ratios.right, remaining);
    REQUIRE_THAT(ratio * remaining,
                 Catch::Matchers::WithinAbs(static_cast<double>(ratios.right), 1e-5));
    REQUIRE(ratio > ratios.right);
    // Degenerate inputs must not produce a NaN that ImGui would then divide a node by.
    REQUIRE(splitRatioWithin(0.2f, 0.0f) == 0.0f);
    REQUIRE(splitRatioWithin(2.0f, 1.0f) <= 0.9f);
}

TEST_CASE("a saved layout comes back panel for panel", "[ui][layout]") {
    const std::filesystem::path file = scratchDir("roundtrip") / "editor-layout.json";

    EditorLayout saved;
    // Move every panel off its default, so a store that silently wrote defaults would be caught.
    for (const EditorPanel& panel : editorPanels()) {
        saved.setVisible(panel.id, !panel.openByDefault);
    }
    saved.setRegionNode(DockRegion::Left, 0x0Au);
    saved.setRegionNode(DockRegion::Right, 0x0Bu);
    saved.setRegionNode(DockRegion::Bottom, 0x0Cu);
    saved.setRegionNode(DockRegion::Centre, 0x0Du);
    REQUIRE(saved.save(file));

    EditorLayout loaded;
    REQUIRE(loaded.load(file));
    for (const EditorPanel& panel : editorPanels()) {
        INFO("panel '" << panel.id << "'");
        REQUIRE(loaded.visible(panel.id) == !panel.openByDefault);
    }
    REQUIRE(loaded.regionNode(DockRegion::Left) == 0x0Au);
    REQUIRE(loaded.regionNode(DockRegion::Right) == 0x0Bu);
    REQUIRE(loaded.regionNode(DockRegion::Bottom) == 0x0Cu);
    REQUIRE(loaded.regionNode(DockRegion::Centre) == 0x0Du);
    REQUIRE(loaded.signature() == saved.signature());
}

TEST_CASE("restoring the default layout undoes a loaded one", "[ui][layout]") {
    const std::filesystem::path file = scratchDir("restore") / "editor-layout.json";

    EditorLayout fresh;
    const std::uint64_t defaults = fresh.signature();

    EditorLayout mangled;
    for (const EditorPanel& panel : editorPanels()) {
        mangled.setVisible(panel.id, !panel.openByDefault);
    }
    mangled.setRegionNode(DockRegion::Left, 99u);
    REQUIRE(mangled.save(file));

    EditorLayout live;
    REQUIRE(live.load(file));
    REQUIRE(live.signature() != defaults);

    // The real requirement: restoring rebuilds the state in this process, rather than deleting a
    // file and leaving the running editor exactly as it was until somebody restarts it.
    live.restoreDefaults();
    REQUIRE(live.signature() == defaults);
    for (const EditorPanel& panel : editorPanels()) {
        INFO("panel '" << panel.id << "'");
        REQUIRE(live.visible(panel.id) == panel.openByDefault);
    }
    // The remembered dock nodes go too: the caller rebuilds the tree, which mints new ones, and a
    // stale id would dock the next panel opened into a node that no longer exists.
    REQUIRE(live.regionNode(DockRegion::Left) == 0u);
}

TEST_CASE("a layout file from another build does not break this one", "[ui][layout]") {
    const std::filesystem::path dir = scratchDir("foreign");

    const std::filesystem::path unknownPanel = dir / "unknown.json";
    {
        std::ofstream out(unknownPanel);
        out << R"({"format":"avgen-editor-layout","version":1,)"
               R"("panels":{"Sequencer":true,"Parameters":false}})";
    }
    EditorLayout layout;
    const std::size_t panelCount = layout.size();
    REQUIRE(layout.load(unknownPanel));
    REQUIRE(layout.size() == panelCount); // a name we do not know does not become a panel
    REQUIRE_FALSE(layout.visible("Sequencer"));
    REQUIRE_FALSE(layout.visible("Parameters"));

    // A missing file is a first run, not a failure.
    EditorLayout absent;
    REQUIRE(absent.load(dir / "not-written-yet.json"));
    REQUIRE(absent.signature() == EditorLayout{}.signature());
}

TEST_CASE("a corrupt layout file is refused and changes nothing", "[ui][layout]") {
    const std::filesystem::path dir = scratchDir("corrupt");

    EditorLayout layout;
    layout.setVisible("Parameters", false);
    const std::uint64_t before = layout.signature();

    const std::filesystem::path truncated = dir / "truncated.json";
    {
        std::ofstream out(truncated);
        out << R"({"format":"avgen-editor-layout","version":1,"panels":{"Para)";
    }
    REQUIRE_FALSE(layout.load(truncated));
    REQUIRE(layout.signature() == before);

    const std::filesystem::path wrongFormat = dir / "recent.json";
    {
        std::ofstream out(wrongFormat);
        out << R"({"format":"avgen-recent","version":1,"entries":[]})";
    }
    REQUIRE_FALSE(layout.load(wrongFormat));
    REQUIRE(layout.signature() == before);

    // Half a good file must not leave half a layout behind: 'Assets' is legal, 'World' is not, and
    // neither may be applied.
    const std::filesystem::path halfGood = dir / "half.json";
    {
        std::ofstream out(halfGood);
        out << R"({"format":"avgen-editor-layout","version":1,)"
               R"("panels":{"Assets":true,"World":"yes"}})";
    }
    REQUIRE_FALSE(layout.load(halfGood));
    REQUIRE(layout.signature() == before);
    REQUIRE_FALSE(layout.visible("Assets"));
}

TEST_CASE("the visibility flag ImGui writes through is the one the layout reads", "[ui][layout]") {
    EditorLayout layout;
    bool* slot = layout.slot("Graph");
    REQUIRE(slot != nullptr);
    REQUIRE(layout.slot("no such panel") == nullptr);

    const std::uint64_t before = layout.signature();
    *slot = !*slot; // what ImGui::MenuItem and a window's close box do
    REQUIRE(layout.visible("Graph") == *slot);
    // The signature has to move, or a toggle made through the pointer never reaches the disk.
    REQUIRE(layout.signature() != before);
}

TEST_CASE("the canvas is not a panel and owns the centre alone", "[ui][layout]") {
    // It has no menu entry and no close box: an editor whose world can be switched off is an
    // editor with a bug report coming.
    REQUIRE(findEditorPanel(kCanvasWindow) == nullptr);
    for (const EditorPanel& panel : editorPanels()) {
        INFO("panel '" << panel.id << "'");
        REQUIRE(panel.id != kCanvasWindow);
    }
}

TEST_CASE("a click is measured from the canvas, not from the window", "[ui][layout]") {
    // A canvas inset by a left column and a menu bar, on a retina display.
    CanvasRect canvas;
    canvas.x = 330.0f;
    canvas.y = 88.0f;
    canvas.width = 1034.0f;
    canvas.height = 734.0f;
    const float scale = 2.0f;
    const std::uint32_t w = 2068;
    const std::uint32_t h = 1468;

    // The top-left corner of the canvas is the first pixel of the render target. Forgetting to
    // subtract the origin sends it to (660, 176) instead -- still inside the target, still picking
    // something, wrong by exactly the width of the left-hand panel.
    const CanvasPixel corner = canvasPixelFor(canvas, canvas.x, canvas.y, scale, w, h);
    REQUIRE(corner.x == 0);
    REQUIRE(corner.y == 0);

    const CanvasPixel middle = canvasPixelFor(canvas, canvas.x + canvas.width * 0.5f,
                                              canvas.y + canvas.height * 0.5f, scale, w, h);
    REQUIRE(middle.x == w / 2);
    REQUIRE(middle.y == h / 2);

    // A drag released over a panel still names a pixel inside the target rather than wrapping to
    // an enormous unsigned number.
    const CanvasPixel outside = canvasPixelFor(canvas, 10.0f, 10.0f, scale, w, h);
    REQUIRE(outside.x == 0);
    REQUIRE(outside.y == 0);
    const CanvasPixel past = canvasPixelFor(canvas, 5000.0f, 5000.0f, scale, w, h);
    REQUIRE(past.x == w - 1);
    REQUIRE(past.y == h - 1);

    // Before the first frame is laid out there is no canvas, and the window is the render target.
    const CanvasPixel noCanvas = canvasPixelFor(CanvasRect{}, 100.0f, 50.0f, scale, w, h);
    REQUIRE(noCanvas.x == 200);
    REQUIRE(noCanvas.y == 100);

    REQUIRE(canvas.contains(canvas.x + 1.0f, canvas.y + 1.0f));
    REQUIRE_FALSE(canvas.contains(canvas.x - 1.0f, canvas.y + 1.0f));
    REQUIRE_FALSE(canvas.contains(canvas.x + canvas.width, canvas.y));
}
