// ADR-422: the deformation contract, and the claim about the existing stack that it rests on.

#include <catch2/catch_test_macros.hpp>

#include "scene/deformation_contract.hpp"
#include "scene/procedural.hpp"
#include "ui/ui_logic.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>

using namespace avgen;

TEST_CASE("an operator cannot be described without answering the velocity question",
          "[scene][deformation][contract]") {
    // The whole design in one assertion. A default-constructible traits struct would let somebody
    // add a fragmentation operator without saying it breaks correspondence, and the consequence
    // would appear in the temporal agent's motion blur rather than in their operator.
    //
    // This repository's recurring defect is a field nobody filled in behaving exactly like a field
    // somebody filled in with the wrong answer; a constructor that will not compile without the
    // answer is the cheapest place to break that chain.
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<scene::DeformationOperatorTraits>);
    // And not implicitly constructible from the enum either: `f(VertexCorrespondence::Broken)` at a
    // call site expecting traits would read as an answer, but `f({})` would not, and `explicit` is
    // what stops the second.
    STATIC_REQUIRE_FALSE(
        std::is_convertible_v<scene::VertexCorrespondence, scene::DeformationOperatorTraits>);
    STATIC_REQUIRE(
        std::is_constructible_v<scene::DeformationOperatorTraits, scene::VertexCorrespondence>);
}

TEST_CASE("the two obligations are exact negations of each other", "[scene][deformation][contract]") {
    constexpr scene::DeformationOperatorTraits preserving{scene::VertexCorrespondence::Preserved};
    constexpr scene::DeformationOperatorTraits breaking{scene::VertexCorrespondence::Broken};

    STATIC_REQUIRE(preserving.velocityFromDoubleEvaluation());
    STATIC_REQUIRE_FALSE(preserving.mustAuthorVelocity());
    STATIC_REQUIRE_FALSE(breaking.velocityFromDoubleEvaluation());
    STATIC_REQUIRE(breaking.mustAuthorVelocity());

    // A control: the two are not simply the same answer twice, which is how a predicate pair stops
    // distinguishing anything.
    STATIC_REQUIRE(preserving.velocityFromDoubleEvaluation() != breaking.velocityFromDoubleEvaluation());
    CHECK(scene::vertexCorrespondenceName(scene::VertexCorrespondence::Preserved) !=
          scene::vertexCorrespondenceName(scene::VertexCorrespondence::Broken));
}

TEST_CASE("the existing deformers really do preserve vertex correspondence",
          "[scene][deformation][contract]") {
    // `kExistingDeformerTraits` is a claim about seven operators, and a claim in a header is
    // ADR-385's stated reason that is not evidence. So it is checked the only way it can be on the
    // CPU: a deformer applied to a point returns A point, for every kind -- it is a map from one
    // position to one position, which is what "preserved" means. `deformPoint`'s signature is
    // itself the proof (`vec3 -> vec3`), and this asserts that every kind goes through it without
    // producing something that is not a position.
    STATIC_REQUIRE(scene::kExistingDeformerTraits.velocityFromDoubleEvaluation());

    const glm::mat4 identity(1.0f);
    const glm::vec3 p(0.8f, 1.4f, -0.3f);
    for (const scene::DeformerKind kind : ui::kDeformerKinds) {
        INFO("kind: " << scene::deformerKindName(kind));
        scene::Deformer d;
        d.kind = kind;
        d.amount = 0.3f;
        d.frequency = 1.0f;
        d.scale = 0.5f;
        const glm::vec3 moved = scene::deformPoint({d}, p, identity, 0.0);
        CHECK(std::isfinite(moved.x));
        CHECK(std::isfinite(moved.y));
        CHECK(std::isfinite(moved.z));
    }
}

TEST_CASE("the double evaluation the contract rests on is really in the shader",
          "[scene][deformation][contract]") {
    // The contract's first paragraph quotes `shaders/procedural.wgsl` and its whole argument rests
    // on that quotation being true. ADR-385: a stated reason is not evidence, including when the
    // reason is a quotation. So the file is read and the two load-bearing facts are checked -- that
    // the previous position comes from the SOURCE vertex, and that it is evaluated at the previous
    // frame's time and through the previous frame's object matrix.
    //
    // If somebody rewrites that shader so velocity is derived some other way, this fails and the
    // contract gets re-read rather than silently becoming fiction.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path shader =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "shaders" / "procedural.wgsl";
    std::ifstream in(shader);
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    CHECK(text.find("deformChain(srcPos") != std::string::npos);
    CHECK(text.find("proc.prevInfo.x") != std::string::npos);
    CHECK(text.find("object.prevModel") != std::string::npos);
    CHECK(text.find("out.prevClip") != std::string::npos);
    // The control: a string that is certainly not in that shader, so the four checks above are not
    // passing because `find` always succeeds on something.
    CHECK(text.find("deformChain(noSuchVertex") == std::string::npos);
#endif
}
