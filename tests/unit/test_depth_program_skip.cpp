// ADR-1590: a depth-only draw runs a surface's material program only where it can move the cutout.
#include "scene/material_program.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

namespace {
scene::MaterialProgram writing(int baseColor, int opacity) {
    scene::MaterialProgram p;
    p.name = "p";
    scene::MaterialOp op;
    op.kind = scene::MaterialOpKind::Constant;
    op.dst = 0;
    op.constant = glm::vec4(0.5f);
    p.ops = {op};
    p.baseColorRegister = baseColor;
    p.opacityRegister = opacity;
    return p;
}
} // namespace

TEST_CASE("A depth draw runs the program only for a masked surface whose program writes opacity (ADR-1590)",
          "[material][depth][adr1590]") {
    const scene::MaterialProgram colourOnly = writing(0, -1);
    const scene::MaterialProgram cutout = writing(-1, 0);
    CHECK_FALSE(colourOnly.writesOpacity());
    CHECK(cutout.writesOpacity());

    CHECK(scene::depthDrawRunsProgram(true, &cutout));
    CHECK_FALSE(scene::depthDrawRunsProgram(true, &colourOnly));  // the leaf canopies: skipped
    CHECK_FALSE(scene::depthDrawRunsProgram(false, &cutout));     // opaque: the depth draw never cuts
    CHECK_FALSE(scene::depthDrawRunsProgram(true, nullptr));      // no program

    // An opacity register out of the eight is one the shader never reads (materialRegisterInRange).
    CHECK_FALSE(writing(-1, 8).writesOpacity());
    CHECK_FALSE(writing(-1, -2).writesOpacity());

    // A layer has no opacity output, so layers never make a program cut.
    scene::MaterialProgram layered = colourOnly;
    scene::MaterialLayer layer;
    layer.baseColorRegister = 0;
    layer.ops = colourOnly.ops;
    layered.layers.push_back(layer);
    CHECK_FALSE(scene::depthDrawRunsProgram(true, &layered));
}
