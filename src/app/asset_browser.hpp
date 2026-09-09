#pragma once

// Asset browser (ADR-031 follow-up): a catalogue of the things a world is built from — example
// projects, scene files, graph library entries, presets on disk, glTF assets, HDR environments and
// shader layers — scanned from a set of directories with metadata and an optional thumbnail. The
// thumbnail is a PNG next to the file (same stem); `thumbnailPathFor` names it so the host can
// render one offline with a RenderJob and drop it there.

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::app {

enum class AssetKind : std::uint8_t { Project, Scene, Graph, Preset, Model, Environment, Shader, Audio, Unknown };
[[nodiscard]] const char* assetKindName(AssetKind kind);
[[nodiscard]] AssetKind assetKindForFile(const std::filesystem::path& path); // by extension and JSON "format"

struct AssetEntry {
    AssetKind kind = AssetKind::Unknown;
    std::string name;          // display name (JSON "name" when present, else the stem)
    std::string category;      // JSON "category" or the containing directory
    std::string description;
    std::filesystem::path path;
    std::filesystem::path thumbnail; // empty when none exists
    std::uintmax_t bytes = 0;
    [[nodiscard]] bool operator<(const AssetEntry& other) const; // category, then name
};

// Scans `dirs` recursively (depth <= 4, skipping dot-directories and `build`), classifying files.
// Unreadable files are skipped; the scan never throws.
[[nodiscard]] std::vector<AssetEntry> scanAssets(const std::vector<std::filesystem::path>& dirs);
// The conventional thumbnail path for an asset: "<stem>.thumb.png" beside it.
[[nodiscard]] std::filesystem::path thumbnailPathFor(const std::filesystem::path& asset);
// Filters by kind and a case-insensitive substring over name/category/description.
[[nodiscard]] std::vector<const AssetEntry*> filterAssets(const std::vector<AssetEntry>& assets, AssetKind kind,
                                                          std::string_view search);

} // namespace avgen::app
