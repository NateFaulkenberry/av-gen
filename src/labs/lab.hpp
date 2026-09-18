#pragma once

// The Engineering Lab Suite's registry (ADR-261).
//
// A lab is not a runtime. It is four facts about a question:
//
//   1. the question it answers, and the engine decision that answers it;
//   2. what it owns, and -- the half that keeps two labs from both fixing the same bug in
//      different places -- what it does not;
//   3. a fixture it opens on, which is a scene file this repository already ships;
//   4. the overlays that are worth looking at while asking it.
//
// None of that needs a device, a window or a renderer, so none of it is here. This header is names,
// paths and strings, which is why it lives in `avgen_core` and can be checked by the CPU suite.
//
// **The registry is the ownership map, executable.** `docs/engineering-labs.md` prints the same
// table, but prose about who owns what drifts from the code that owns it. Here `decides` names the
// file and the symbol that actually makes the decision the lab is about, and
// `tests/unit/test_lab_registry.cpp` opens every one of those files. A lab whose decision site was
// renamed fails the suite rather than misleading whoever reads the document next.

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace avgen::labs {

// The fourteen labs of the suite. The order is the order they are listed in and reported in; it
// runs from the decisions nearest the scene to the ones nearest the pixel, and ends with the two
// that are about whole frames rather than one stage.
enum class LabId {
    Animation,
    Character,
    Visibility,
    Lod,
    Camera,
    Shadow,
    Lighting,
    Hdr,
    Volumetric,
    Particle,
    Temporal,
    Aov,
    Rendering,
    Integration,
    // The fifteenth, and the only one whose subject is not a pixel. Every lab above asks why the
    // renderer produced the frame it produced; this one asks how long after a person acted the
    // application answered, which is a property of the editor's architecture rather than of any
    // pass. It is in this registry rather than beside it because a lab suite that cannot hold the
    // question "why does the editor feel slow" sends people to build a harness of their own, and
    // this repository has already paid for one of those (`src/core/phase2_probe.hpp`).
    Interaction,
};

// How much of the lab exists today. This field is the reason the registry can ship complete while
// twelve of the fourteen labs are unbuilt: a launcher that silently opens an empty lab teaches
// people the suite is decorative, and one that hides the unbuilt labs cannot be used to hand work
// out. So the menu and `--labs` both show all fourteen and say which is which.
enum class LabStatus {
    // Built and in use: the lab has its own code, its own tests, and its own documentation.
    Built,
    // Claimed by an agent and being built now. The fixture and the ownership are settled; the
    // diagnostics are not.
    InProgress,
    // Designed here and not yet staffed. The fixture below is the one it should open on, chosen
    // from what the repository already ships.
    Planned,
};

struct LabDescriptor {
    LabId id;
    // The CLI token, the case directory's name, and the key a case file names its lab by. Lowercase,
    // no spaces: `avgen --labs` prints it and `--lab-case` takes it.
    std::string_view key;
    std::string_view title;
    LabStatus status;
    // The one question this lab exists to answer, in the form a person asks it.
    std::string_view question;
    // §34's boundary, both halves. `owns` is the decision; `doesNotOwn` names the lab that owns the
    // thing people will mistake it for.
    std::string_view owns;
    std::string_view doesNotOwn;
    // Where that decision is actually made: `path/to/file.cpp:symbol`, repository-relative. Not a
    // category -- the file a person opens. Checked by the registry test.
    std::string_view decides;
    // The lab's document, repository-relative. `docs/engineering-labs.md` until a lab has its own.
    std::string_view doc;
    // The fixture it opens on: a project or scene file this repository already ships, relative to
    // the repository root. Empty when the lab has no isolated fixture yet, which is a statement
    // about the lab and not a placeholder -- `--lab` refuses rather than opening something else.
    std::string_view fixture;
    // Its case file (§32), relative to the repository root, or empty when it has none yet.
    std::string_view cases;
};

// Every lab, in `LabId` order. Static storage; the span outlives any caller.
[[nodiscard]] std::span<const LabDescriptor> labs();

[[nodiscard]] const LabDescriptor& lab(LabId id);
// By `key`. Empty when no lab answers to that name -- the caller reports the name it was given
// rather than falling back to a lab the person did not ask for.
[[nodiscard]] std::optional<LabId> findLab(std::string_view key);
// Every key, comma separated, for the message that tells somebody what they may write.
[[nodiscard]] std::string_view labKeys();

[[nodiscard]] std::string_view statusName(LabStatus status);

} // namespace avgen::labs
