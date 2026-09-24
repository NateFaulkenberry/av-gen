#pragma once

// Where a render started from the editor reads the session from, and where its output lands
// (Director program 0.4).
//
// A render builds its own offline `Engine` and loads a project FILE, which is what makes it
// reproducible. It used to get that file by saving the person's own project over itself
// (`startRenderFromUi`, the render queue): every unsaved edit went into their file, and the
// "unsaved changes" prompt that would have let them decline was cleared by the same save. So a render
// -- and anything built like one, such as a Director preview -- now reads a scratch copy written by
// `Engine::writeProjectCopy`, and the person's file is never touched.
//
// The output still resolves against the person's project folder, so a relative output path lands
// where it always did rather than in the temp directory the scratch copy sits in.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace avgen::app {

struct RenderSource {
    std::filesystem::path scratch;    // the file the render loads; never the person's project
    std::filesystem::path outputBase; // resolves a relative output path
};

// `projectPath` is the session's project (empty when it was never saved); `scratchDir` is where
// scratch copies go; `tag` and `serial` keep two renders in one session from sharing a file, and the
// process id keeps two sessions from sharing one.
[[nodiscard]] inline RenderSource renderSourceFor(const std::filesystem::path& projectPath,
                                                  const std::filesystem::path& scratchDir,
                                                  std::string_view tag, std::uint64_t serial,
                                                  long long processId) {
    RenderSource source;
    source.scratch = scratchDir / ("avgen_" + std::string(tag) + "_" + std::to_string(processId) + "_" +
                                   std::to_string(serial) + ".json");
    source.outputBase = projectPath.empty() ? scratchDir
                                            : std::filesystem::absolute(projectPath).parent_path();
    return source;
}

} // namespace avgen::app
