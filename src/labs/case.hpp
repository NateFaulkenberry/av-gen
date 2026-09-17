#pragma once

// A lab case: one reproducible question (ADR-260, spec §32 and §10).
//
// §32 asks that "Reproduce Visibility Lab case 37" be a sentence with a mechanical answer. This is
// the answer, and the shape of it was decided by two constraints the repository already imposes:
//
// **A case references a fixture; it never contains one.** This project's scenarios are frequently
// duplicated data rather than shared data -- `glowmere-valley-2`, `-multicam`, `-song` and
// `glowmere-atmospherics` each carry a copy of the same authored blocks, and a nested scene's
// `sha256` has to be refreshed by hand when the file it names changes. A case format that embedded
// scene content would multiply that by the number of cases. So a case is a *path* plus the handful
// of numbers that make a frame of that path reproducible.
//
// **The numbers are the ones the engine actually reads.** `fixture` goes to `Engine::loadFile`,
// which routes on extension; `timeSeconds` to `Engine::seekSeconds`; `disable` to
// `SceneRenderer::setPassArm`; `qualityArms` to `SceneRenderer::setQualityArm`; `tier` to the same
// string `--tier` takes. Nothing here is a name the lab invented for itself, so a case cannot drift
// from the flags that reproduce it -- and `reproduceCommand` prints exactly those flags.
//
// **What a case deliberately does not carry**: a seed for anything but its own record, and any
// expectation expressed as a number. The expectation is a sentence a person reads; the assertion
// lives in a test file, where it can fail. A case that carried its own pass criterion would be a
// probe grading itself.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::labs {

struct LabCase {
    // The lab's `key`. A case file whose entries name a different lab than the file does is
    // rejected rather than filed under the file's lab: a case that answers to two names is a case
    // somebody will reproduce in the wrong place.
    std::string lab;
    int number = 0;
    std::string title;
    // §5's first two questions, written down where the case is. Not decoration -- a case whose
    // `question` is empty is one nobody can tell is still worth running.
    std::string question;
    std::string expectation;

    // Repository-relative. A project (`.json`), a composition (`.scene.json`) or a glTF; whatever
    // `Engine::loadFile` routes.
    std::string fixture;

    double timeSeconds = 0.0;
    std::uint32_t width = 640;
    std::uint32_t height = 360;
    double fps = 60.0;
    // Recorded so a randomized failure can be replayed (§10). Zero means the case did not seed
    // anything, which is different from seeding with zero and is why this is not an optional the
    // JSON omits.
    std::uint64_t seed = 0;

    // A camera the case overrides to, because a lab question is usually about one view. When
    // `hasCamera` is false the fixture's own camera is used and nothing is written.
    bool hasCamera = false;
    glm::vec3 eye{0.0f};
    glm::vec3 aim{0.0f};

    // `--supersample`. 1.0 is off. Here because the Rendering Lab's reference arm is defined by
    // it and nothing else: the reference and the candidate are the same scene at the same second,
    // and the supersample factor is the whole difference between them.
    double supersample = 1.0;
    // `--tier`: preview | realtime | high | offline. Empty leaves the tier alone.
    std::string tier;
    // `--disable`: `SceneRenderer::passArms()` names.
    std::vector<std::string> disable;
    // `--quality-arm`: `SceneRenderer::qualityArms()` names.
    std::vector<std::string> qualityArms;
    // `--aov`: `RenderSettings::aovNames()` -- normal, emission, depth, velocity, id.
    std::vector<std::string> aovs;

    std::string notes;
};

// The file a lab's cases live in, relative to the repository root:
// `examples/labs/<key>/cases.json`.
[[nodiscard]] std::filesystem::path caseFilePath(std::string_view labKey);

[[nodiscard]] nlohmann::json toJson(const LabCase& c);
[[nodiscard]] Result<LabCase> caseFromJson(const nlohmann::json& doc);

// `{"format": "avgen-lab-cases", "version": 1, "lab": "<key>", "cases": [...]}`.
[[nodiscard]] Result<std::vector<LabCase>> loadCases(const std::filesystem::path& file);
[[nodiscard]] Result<void> saveCases(const std::filesystem::path& file, std::string_view labKey,
                                     const std::vector<LabCase>& cases);

// The case numbered `number`, or an error naming the numbers that do exist -- because "case 37 not
// found" is half an answer when the file holds 1 through 6.
[[nodiscard]] Result<LabCase> findCase(const std::vector<LabCase>& cases, int number);

// The repository this process can see `examples/` in: the working directory when it holds one,
// otherwise the source tree the binary was built from. A case names its fixture by a
// repository-relative path, so something has to say what the repository is, and guessing from the
// executable's location is wrong for an installed build and right for none of the cases here.
[[nodiscard]] std::filesystem::path repositoryRoot();

// Resolves `"<lab>:<number>"` -- what `--lab-case` takes -- against `root`. Errors name the lab,
// the file it looked in and the numbers that do exist, because every one of those is a different
// mistake and "case not found" does not distinguish them.
[[nodiscard]] Result<LabCase> resolveCaseSpec(std::string_view spec,
                                              const std::filesystem::path& root);

// The `avgen` invocation that reproduces this case, as a single line a person can paste. §32's
// whole point: the case is the recipe, and the recipe is executable.
[[nodiscard]] std::string reproduceCommand(const LabCase& c);

} // namespace avgen::labs
