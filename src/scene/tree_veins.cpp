#include "scene/tree_veins.hpp"

#include <algorithm>

namespace avgen::scene {
namespace {

MaterialOp op(MaterialOpKind kind, int dst, int srcA = 0, int srcB = 0) {
    MaterialOp o;
    o.kind = kind;
    o.dst = dst;
    o.srcA = srcA;
    o.srcB = srcB;
    return o;
}

} // namespace

MaterialProgram makeVeinProgram(const std::string& name, const VeinSettings& s) {
    MaterialProgram program;
    program.name = name;

    // r0 = uv. u runs around the tube, v runs along it: makeTube's own parameterisation, which is
    // why the pattern follows the branch without needing to know anything about the branch.
    MaterialOp uv = op(MaterialOpKind::Input, 0);
    uv.input = MaterialInput::Uv;
    program.ops.push_back(uv);

    // r1 = (aroundFrequency, alongFrequency, 0, 0), and r2 = uv scaled by it. Sampling noise at a
    // high frequency around and a low one along is what turns a blob field into a streak.
    MaterialOp freq = op(MaterialOpKind::Constant, 1);
    freq.constant = glm::vec4(s.aroundFrequency, s.alongFrequency, 0.0f, 0.0f);
    program.ops.push_back(freq);
    program.ops.push_back(op(MaterialOpKind::Multiply, 2, 0, 1));

    MaterialOp veinNoise = op(MaterialOpKind::Noise, 2, 2);
    veinNoise.value = 1.0f;
    veinNoise.seed = s.seed;
    program.ops.push_back(veinNoise);

    // The cut. Smoothstep rather than Threshold: a hard step aliases badly on a cylinder seen at a
    // grazing angle, which is most of a trunk.
    MaterialOp cut = op(MaterialOpKind::Smoothstep, 2, 2);
    cut.constant = glm::vec4(s.threshold, s.threshold + std::max(s.edge, 0.01f), 0.0f, 0.0f);
    program.ops.push_back(cut);

    // The travelling pulse. r3 takes v into x, where every scalar op looks for it.
    MaterialOp alongAxis = op(MaterialOpKind::Swizzle, 3, 0);
    alongAxis.constant = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // every channel takes uv.y
    program.ops.push_back(alongAxis);

    MaterialOp wavelength = op(MaterialOpKind::Constant, 4);
    const float k = 1.0f / std::max(s.pulseWavelength, 0.05f);
    wavelength.constant = glm::vec4(k, k, k, k);
    program.ops.push_back(wavelength);
    program.ops.push_back(op(MaterialOpKind::Multiply, 3, 3, 4));

    MaterialOp time = op(MaterialOpKind::Input, 4);
    time.input = MaterialInput::Time;
    program.ops.push_back(time);
    MaterialOp speed = op(MaterialOpKind::Constant, 5);
    speed.constant = glm::vec4(-s.pulseSpeed);
    program.ops.push_back(speed);
    program.ops.push_back(op(MaterialOpKind::Multiply, 4, 4, 5));
    // v/wavelength - time*speed: the wave travels up the branch rather than flashing in place.
    program.ops.push_back(op(MaterialOpKind::Add, 3, 3, 4));

    // A cosine palette used as a scalar oscillator, because the op set has no sine. a + b*cos(...)
    // with a = 1 - depth/2 and b = depth/2 gives a pulse in [1-depth, 1].
    MaterialOp pulse = op(MaterialOpKind::Palette, 3, 3);
    const float depth = std::clamp(s.pulseDepth, 0.0f, 1.0f);
    pulse.constant = glm::vec4(1.0f - depth * 0.5f);  // a
    pulse.constant2 = glm::vec4(depth * 0.5f);        // b
    pulse.constant3 = glm::vec4(1.0f);                // c
    pulse.constant4 = glm::vec4(0.0f);                // d
    program.ops.push_back(pulse);
    program.ops.push_back(op(MaterialOpKind::Multiply, 2, 2, 3));

    // A Fresnel term, so a vein at a grazing angle reads brighter. This is the cheapest thing that
    // makes a flat glowing stripe look like something running under a surface rather than painted
    // on top of it.
    MaterialOp fresnel = op(MaterialOpKind::Fresnel, 4);
    fresnel.value = 2.0f;
    program.ops.push_back(fresnel);
    // 0.28, not 0.55. At the larger weight the Fresnel term swamped the vein mask and the result
    // was a rim light: one bright line on whichever edge of the trunk faced away, which is a
    // silhouette effect rather than a vein.
    MaterialOp rimMix = op(MaterialOpKind::Constant, 5);
    rimMix.constant = glm::vec4(0.28f);
    program.ops.push_back(rimMix);
    program.ops.push_back(op(MaterialOpKind::Multiply, 4, 4, 5));
    MaterialOp one = op(MaterialOpKind::Constant, 5);
    one.constant = glm::vec4(1.0f);
    program.ops.push_back(one);
    program.ops.push_back(op(MaterialOpKind::Add, 4, 4, 5));
    program.ops.push_back(op(MaterialOpKind::Multiply, 2, 2, 4));

    MaterialOp tint = op(MaterialOpKind::Constant, 5);
    tint.constant = glm::vec4(s.color, 1.0f);
    program.ops.push_back(tint);
    program.ops.push_back(op(MaterialOpKind::Multiply, 2, 2, 5));

    // Emission only. Every other output stays -1, so the material's own base colour, roughness and
    // metallic survive: the program asserts the one channel it is for and nothing else.
    program.emissionRegister = 2;
    program.emissionIntensity = s.intensity;
    return program;
}

Result<void> verifyEmissionOwnership(const Scene& scene) {
    for (const Entity& entity : scene.entities) {
        if (entity.material.program.empty()) {
            continue;
        }
        const MaterialProgram* found = nullptr;
        for (const MaterialProgram& program : scene.materialPrograms) {
            if (program.name == entity.material.program) {
                found = &program;
                break;
            }
        }
        if (found == nullptr) {
            return fail("entity '{}' names material program '{}', which the scene does not carry",
                        entity.name, entity.material.program);
        }
        if (found->emissionRegister < 0) {
            continue; // the program leaves emission alone, so the material still owns it
        }
        // The program asserts emission, so the material's own emissive is dead weight that will be
        // silently discarded. Authoring one is how an art-direction rule survives review and then
        // does nothing -- which is precisely the regression this check exists for.
        if (entity.material.emissiveIntensity > 1e-4f) {
            return fail("entity '{}' carries program '{}', which asserts emission, AND a material "
                        "emissiveIntensity of {} that nothing will ever read",
                        entity.name, entity.material.program, entity.material.emissiveIntensity);
        }
    }
    return {};
}

} // namespace avgen::scene
