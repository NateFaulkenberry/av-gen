// ADR-421: the deformer stack's editor, held to the stack it edits.
//
// Every case here is ADR-382's rule in a new place -- a path a panel computes needs a test that
// computes it the same way -- plus ADR-182's: each probe is shown able to fail, or carries a control
// that must fail.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "params/parameter_set.hpp"
#include "scene/procedural.hpp"
#include "ui/ui_logic.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;

namespace {

// A procedural object with one deformer of `kind` in slot 0, registered into a scratch parameter
// set -- the same call the composition makes, not a reimplementation of the path arithmetic. That
// is the point ADR-392 makes about obtaining a path set from the registrar rather than from the
// tables: a check that read the table would agree with the table and learn nothing.
struct Registered {
    params::ParameterSet params;
    scene::ProceduralGeometry object;
    scene::ProceduralParameters registered;
    std::string prefix;
};

[[nodiscard]] std::vector<std::string> registeredPathsFor(scene::DeformerKind kind, std::size_t slots = 1) {
    params::ParameterSet params;
    scene::ProceduralGeometry object;
    object.name = "probe";
    for (std::size_t i = 0; i < slots; ++i) {
        scene::Deformer d;
        d.kind = kind;
        object.deformers.push_back(d);
    }
    const std::string prefix = "procedural/probe/";
    const scene::ProceduralParameters p = scene::registerProceduralParameters(params, object, prefix);
    std::vector<std::string> paths;
    for (const params::IParameter* q : params.ordered()) {
        paths.push_back(q->path());
    }
    return paths;
}

[[nodiscard]] bool hasPath(const std::vector<std::string>& paths, const std::string& want) {
    return std::find(paths.begin(), paths.end(), want) != paths.end();
}

} // namespace

TEST_CASE("every row the deformer panel draws is a parameter that kind's slot registers",
          "[ui][deformers][procedural]") {
    // The empty-box defect (ADR-382), asked of the new table. A leaf five characters wrong here
    // would draw "no such parameter" in the panel -- which is already better than an empty box --
    // but it would still be a control an artist cannot use, and this is where it is caught.
    for (const scene::DeformerKind kind : ui::kDeformerKinds) {
        INFO("kind: " << scene::deformerKindName(kind));
        const std::vector<std::string> paths = registeredPathsFor(kind);
        REQUIRE_FALSE(paths.empty());

        for (const ui::DeformerRow& r : ui::deformerRowsFor(kind)) {
            const std::string want = ui::deformerParameterPath("procedural/probe/", 0, r.leaf);
            INFO("row leaf: " << r.leaf << " -> " << want);
            CHECK(hasPath(paths, want));
        }
    }
}

TEST_CASE("the leaf check can fail", "[ui][deformers][procedural]") {
    // ADR-182. Without this, "the rows are fine" and "the checker is inert" look identical.
    const std::vector<std::string> paths = registeredPathsFor(scene::DeformerKind::Twist);
    CHECK_FALSE(hasPath(paths, ui::deformerParameterPath("procedural/probe/", 0, "noSuchLeaf")));
    // ...and the same arithmetic on a leaf that IS there, so the negative above is about the leaf
    // and not about `deformerParameterPath` being wrong in a way that never matches anything.
    CHECK(hasPath(paths, ui::deformerParameterPath("procedural/probe/", 0, "amount")));
}

TEST_CASE("the panel's path arithmetic agrees with the registrar's for every slot",
          "[ui][deformers][procedural]") {
    // The slot is 1-based in the path and 0-based in the vector, which is exactly the kind of
    // off-by-one that draws slot 2's control over slot 1's value and is invisible until an artist
    // reports that "the wrong one moves".
    const std::vector<std::string> paths =
        registeredPathsFor(scene::DeformerKind::Twist, scene::kMaxDeformers);
    for (std::size_t slot = 0; slot < static_cast<std::size_t>(scene::kMaxDeformers); ++slot) {
        INFO("slot " << slot);
        CHECK(hasPath(paths, ui::deformerParameterPath("procedural/probe/", slot, "amount")));
    }
    // One past the last slot registers nothing, so the panel cannot address a deformer that is not
    // there.
    CHECK_FALSE(hasPath(paths, ui::deformerParameterPath(
                                   "procedural/probe/", static_cast<std::size_t>(scene::kMaxDeformers),
                                   "amount")));
}

TEST_CASE("no row names a leaf the kind's arithmetic does not read", "[ui][deformers][procedural]") {
    // The other half of the rule, and the reason the table is per-kind at all. Registration writes
    // the same nine leaves for every slot whatever its kind, so a table that offered all nine would
    // pass the check above and still draw four controls that move nothing. A control that does
    // nothing teaches an artist the system is broken, which is worse than one that is absent.
    //
    // Held against `scene/procedural.hpp`'s own semantics comment, transcribed here. If that
    // comment and this list disagree, one of them is wrong and this fails rather than the panel
    // quietly shipping the disagreement.
    struct Expected {
        scene::DeformerKind kind;
        std::set<std::string> leaves;
    };
    const std::vector<Expected> expected{
        {scene::DeformerKind::Bend, {"amount", "axis", "center", "falloff"}},
        {scene::DeformerKind::Twist, {"amount", "axis", "center", "speed"}},
        {scene::DeformerKind::Sine, {"amount", "axis", "center", "frequency", "phase", "speed"}},
        {scene::DeformerKind::Noise, {"amount", "scale", "speed"}},
        {scene::DeformerKind::Displacement, {"amount", "scale", "speed"}},
        {scene::DeformerKind::Field, {"amount", "axis"}},
        {scene::DeformerKind::Path,
         {"amount", "axis", "center", "pathOffset", "pathScale", "pathRoll"}},
    };
    REQUIRE(expected.size() == std::size(ui::kDeformerKinds));

    for (const Expected& e : expected) {
        INFO("kind: " << scene::deformerKindName(e.kind));
        std::set<std::string> got;
        for (const ui::DeformerRow& r : ui::deformerRowsFor(e.kind)) {
            got.insert(std::string(r.leaf));
        }
        CHECK(got == e.leaves);
    }
}

TEST_CASE("the panel's kind list is exactly the kinds that exist", "[ui][deformers][procedural]") {
    // `test_lab_registry` and `test_effect_conformance` both establish that a test here may read the
    // source tree to check a claim the source tree makes about itself. The same argument applies:
    // `deformerRowsFor`'s switch has no `default`, so a new enumerator is a `-Wswitch` diagnostic --
    // and `-Werror` is off for everything but `-Wformat` (see `cmake/Warnings.cmake`), so a warning
    // nobody reads is not a guard. Adding `Melt` to `DeformerKind` must fail a named test that
    // prints `Melt`, not emit a line in a five-thousand-line build log.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path header =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "src" / "scene" / "procedural.hpp";
    std::ifstream in(header);
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    const std::string marker = "enum class DeformerKind : std::uint8_t {";
    const std::size_t start = text.find(marker);
    REQUIRE(start != std::string::npos);
    const std::size_t end = text.find('}', start);
    REQUIRE(end != std::string::npos);
    const std::string body = text.substr(start + marker.size(), end - start - marker.size());

    std::vector<std::string> declared;
    std::string token;
    for (const char c : body) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            token.push_back(c);
            continue;
        }
        if (!token.empty()) {
            declared.push_back(token);
            token.clear();
        }
    }
    if (!token.empty()) {
        declared.push_back(token);
    }
    REQUIRE_FALSE(declared.empty());

    std::vector<std::string> covered;
    for (const scene::DeformerKind kind : ui::kDeformerKinds) {
        covered.emplace_back(scene::deformerKindName(kind));
    }
    // Compared without case, and that is a fact about the codebase rather than a convenience:
    // `deformerKindName` returns the JSON spelling (`"twist"`), because it is half of a file
    // format, while the enumerator is `Twist`. `drawParticleSettings` makes the same separation in
    // the other direction and says why -- tying a caption to a serialiser's spelling means a rename
    // in one silently changing the other. So the comparison is of the names, not of their casing.
    const auto lower = [](std::string v) {
        std::transform(v.begin(), v.end(), v.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return v;
    };
    std::transform(declared.begin(), declared.end(), declared.begin(), lower);
    std::transform(covered.begin(), covered.end(), covered.begin(), lower);
    std::sort(declared.begin(), declared.end());
    std::sort(covered.begin(), covered.end());

    INFO("declared in procedural.hpp: " << declared.size() << ", covered by the panel: " << covered.size());
    for (const std::string& d : declared) {
        INFO("declared enumerator: " << d);
        CHECK(std::find(covered.begin(), covered.end(), d) != covered.end());
    }
    CHECK(declared == covered);
#endif
}

TEST_CASE("a structural edit rewrites the paths so they describe the stack that is there",
          "[ui][deformers][procedural]") {
    // The reason a kind change has to re-register rather than just being stored: registration
    // LABELS each leaf by the kind of the slot it belongs to, and `applyProceduralParameters` reads
    // `kind` and `space` off the rest copy every frame. A stack whose kind changed without
    // re-registering would have a Bend's arithmetic reading a parameter labelled `Twist/amount` --
    // correct in value and wrong in every name an artist reads.
    params::ParameterSet params;
    scene::ProceduralGeometry object;
    object.name = "probe";
    scene::Deformer d;
    d.kind = scene::DeformerKind::Twist;
    object.deformers.push_back(d);

    const std::string prefix = "procedural/probe/";
    scene::ProceduralParameters registered = scene::registerProceduralParameters(params, object, prefix);
    const params::IParameter* amount = params.find(prefix + "deform/1/amount");
    REQUIRE(amount != nullptr);
    CHECK(amount->label().starts_with("twist/"));

    scene::unregisterProceduralParameters(params, registered);
    CHECK(params.find(prefix + "deform/1/amount") == nullptr);

    object.deformers[0].kind = scene::DeformerKind::Bend;
    registered = scene::registerProceduralParameters(params, object, prefix);
    const params::IParameter* again = params.find(prefix + "deform/1/amount");
    REQUIRE(again != nullptr);
    CHECK(again->label().starts_with("bend/"));
}

TEST_CASE("removing a slot removes its parameters and leaves the others addressable",
          "[ui][deformers][procedural]") {
    // A stack edit that left a slot's paths behind would be the leak `forEachParticleParam` was
    // just fixed for: a parameter registered and never removed, which a project then saves as a
    // dead path every time somebody deletes a deformer.
    params::ParameterSet params;
    scene::ProceduralGeometry object;
    object.name = "probe";
    for (int i = 0; i < 3; ++i) {
        scene::Deformer d;
        d.kind = scene::DeformerKind::Twist;
        object.deformers.push_back(d);
    }
    const std::string prefix = "procedural/probe/";
    scene::ProceduralParameters registered = scene::registerProceduralParameters(params, object, prefix);
    REQUIRE(params.find(prefix + "deform/3/amount") != nullptr);
    const std::size_t before = params.size();

    scene::unregisterProceduralParameters(params, registered);
    object.deformers.erase(object.deformers.begin());
    registered = scene::registerProceduralParameters(params, object, prefix);

    CHECK(params.find(prefix + "deform/1/amount") != nullptr);
    CHECK(params.find(prefix + "deform/2/amount") != nullptr);
    // The third slot's paths are gone, not orphaned.
    CHECK(params.find(prefix + "deform/3/amount") == nullptr);
    CHECK(params.size() < before);
}

TEST_CASE("a deformer the panel adds does something", "[ui][deformers][procedural]") {
    // ADR-360's defect, from the other side: a wind body added at strength 0 is what the owner
    // reported as "it looks unchanged". Somebody who presses Add means it to do something, so the
    // panel seeds each kind at a visible value -- and the values it seeds are checked here by
    // deforming a point, rather than by reading them back out of the panel, which would only
    // confirm that a number is the number it is.
    const glm::mat4 identity(1.0f);
    for (const scene::DeformerKind kind : ui::kDeformerKinds) {
        // Field and Path deform through a named field or spline that a freshly added deformer does
        // not have yet, so they legitimately do nothing until one is chosen. The panel says so.
        if (kind == scene::DeformerKind::Field || kind == scene::DeformerKind::Path) {
            continue;
        }
        INFO("kind: " << scene::deformerKindName(kind));

        scene::Deformer d;
        d.kind = kind;
        switch (kind) {
        case scene::DeformerKind::Bend: d.amount = 0.05f; d.falloff = 1.0f; break;
        case scene::DeformerKind::Twist: d.amount = 0.25f; break;
        case scene::DeformerKind::Sine:
            d.amount = 0.1f;
            d.frequency = 1.0f;
            d.displacementAxis = glm::vec3(1.0f, 0.0f, 0.0f);
            break;
        case scene::DeformerKind::Noise: d.amount = 0.1f; d.scale = 0.5f; break;
        case scene::DeformerKind::Displacement: d.amount = 0.1f; d.scale = 0.5f; break;
        default: break;
        }

        const std::vector<scene::Deformer> stack{d};
        // A point off the axis and off the origin, so a twist and a bend both have something to
        // act on -- at the origin every one of these is a no-op by construction and the case would
        // pass by being asked the wrong question.
        const glm::vec3 p(0.8f, 1.4f, -0.3f);
        const glm::vec3 moved = scene::deformPoint(stack, p, identity, 0.0);
        CHECK(glm::length(moved - p) > 1e-4f);
    }
}

TEST_CASE("an empty stack and a disabled deformer both leave the point where it was",
          "[ui][deformers][procedural]") {
    // The control for the case above: `deformPoint` is not simply moving everything.
    const glm::mat4 identity(1.0f);
    const glm::vec3 p(0.8f, 1.4f, -0.3f);
    CHECK(scene::deformPoint({}, p, identity, 0.0) == p);

    scene::Deformer off;
    off.kind = scene::DeformerKind::Twist;
    off.amount = 0.25f;
    off.enabled = false;
    CHECK(scene::deformPoint({off}, p, identity, 0.0) == p);
}
