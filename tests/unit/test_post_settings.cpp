#include "params/parameter_set.hpp"
#include "scene/post_settings.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

TEST_CASE("Post settings register as parameters and apply finals", "[scene][post]") {
    params::ParameterSet params;
    scene::PostSettings defaults;
    defaults.bloomIntensity = 0.7f;
    auto p = scene::registerPostParameters(params, defaults);
    REQUIRE(p.bloomIntensity != nullptr);
    CHECK(params.size() == 39);
    CHECK(params.find("post/tonemap/operator")->kind() == params::ParamKind::Int);
    CHECK(params.find("post/halation/tint")->componentCount() == 3);
    CHECK(params.find("post/output/sharpen") != nullptr);
    CHECK(params.find("post/grade/lift")->componentCount() == 3);
    CHECK(p.bloomIntensity->value() == 0.7f);

    p.tonemap->setBase(1);
    p.saturation->setBase(0.0f);
    p.dofEnabled->setBase(true);
    p.vignette->setBase(0.5f);
    params.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(p, live);
    CHECK(live.tonemap == scene::TonemapOperator::AgX);
    CHECK(live.saturation == 0.0f);
    CHECK(live.dofEnabled);
    CHECK(live.vignette == 0.5f);
    CHECK(std::string(scene::tonemapOperatorName(live.tonemap)) == "agx");
    // ADR-039: AgX is the default operator now.
    CHECK(scene::PostSettings{}.tonemap == scene::TonemapOperator::AgX);
    // Re-registering returns the same parameters.
    auto again = scene::registerPostParameters(params, defaults);
    CHECK(again.bloomIntensity == p.bloomIntensity);
    CHECK(params.size() == 39);
}
