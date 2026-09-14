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

    // r0 = world position. Everything below is in metres, which is the whole point: the pattern's
    // scale is a property of the tree and not of how any one branch happens to be parameterised.
    MaterialOp world = op(MaterialOpKind::Input, 0);
    world.input = MaterialInput::WorldPosition;
    program.ops.push_back(world);

    // r4 = an anisotropic scale: full frequency horizontally, divided vertically, so features come
    // out stretched along the trunk instead of round. See the note in VeinSettings.
    MaterialOp aniso = op(MaterialOpKind::Constant, 4);
    const float vertical = s.veinScale / std::max(s.verticalStretch, 1.0f);
    aniso.constant = glm::vec4(s.veinScale, vertical, s.veinScale, 1.0f);
    program.ops.push_back(aniso);
    program.ops.push_back(op(MaterialOpKind::Multiply, 5, 0, 4));

    // r1 = the vein field, triplanar so a cylinder has no seam and no pole. The scale is already in
    // r5, so the op's own uniform factor stays at 1.
    MaterialOp veins = op(MaterialOpKind::Triplanar, 1, 5);
    veins.value = 1.0f;
    veins.constant2 = glm::vec4(s.triplanarSharpness, 0.0f, 0.0f, 0.0f);
    veins.seed = s.seed;
    program.ops.push_back(veins);

    // The cut. Smoothstep rather than Threshold: a hard step aliases badly on a cylinder seen at a
    // grazing angle, which is most of a trunk.
    MaterialOp cut = op(MaterialOpKind::Smoothstep, 1, 1);
    cut.constant = glm::vec4(s.threshold, s.threshold + std::max(s.edge, 0.01f), 0.0f, 0.0f);
    program.ops.push_back(cut);

    // r2 = world height, in x where every scalar op looks for it.
    MaterialOp height = op(MaterialOpKind::Swizzle, 2, 0);
    height.constant = glm::vec4(1.0f); // every channel takes world y
    program.ops.push_back(height);

    MaterialOp wavelength = op(MaterialOpKind::Constant, 3);
    const float k = 1.0f / std::max(s.pulseWavelength, 0.1f);
    wavelength.constant = glm::vec4(k);
    program.ops.push_back(wavelength);
    program.ops.push_back(op(MaterialOpKind::Multiply, 2, 2, 3));

    MaterialOp time = op(MaterialOpKind::Input, 3);
    time.input = MaterialInput::Time;
    program.ops.push_back(time);
    MaterialOp speed = op(MaterialOpKind::Constant, 4);
    speed.constant = glm::vec4(-s.pulseSpeed * k);
    program.ops.push_back(speed);
    program.ops.push_back(op(MaterialOpKind::Multiply, 3, 3, 4));
    program.ops.push_back(op(MaterialOpKind::Add, 2, 2, 3));

    // A cosine palette used as a scalar oscillator, because the op set has no sine. a + b*cos(...)
    // with a = 1 - depth/2 and b = depth/2 gives a pulse in [1-depth, 1].
    MaterialOp pulse = op(MaterialOpKind::Palette, 2, 2);
    const float depth = std::clamp(s.pulseDepth, 0.0f, 1.0f);
    pulse.constant = glm::vec4(1.0f - depth * 0.5f);  // a
    pulse.constant2 = glm::vec4(depth * 0.5f);        // b
    pulse.constant3 = glm::vec4(1.0f);                // c
    pulse.constant4 = glm::vec4(0.0f);                // d
    program.ops.push_back(pulse);
    program.ops.push_back(op(MaterialOpKind::Multiply, 1, 1, 2));

    // A Fresnel term, so a vein at a grazing angle reads brighter. This is the cheapest thing that
    // makes a flat glowing stripe look like something running under a surface rather than painted
    // on top of it. 0.28, not 0.55: at the larger weight it swamped the vein mask and the result
    // was a rim light -- one bright line on whichever edge faced away, a silhouette effect.
    MaterialOp fresnel = op(MaterialOpKind::Fresnel, 2);
    fresnel.value = 2.0f;
    program.ops.push_back(fresnel);
    MaterialOp rimMix = op(MaterialOpKind::Constant, 3);
    rimMix.constant = glm::vec4(0.28f);
    program.ops.push_back(rimMix);
    program.ops.push_back(op(MaterialOpKind::Multiply, 2, 2, 3));
    MaterialOp one = op(MaterialOpKind::Constant, 3);
    one.constant = glm::vec4(1.0f);
    program.ops.push_back(one);
    program.ops.push_back(op(MaterialOpKind::Add, 2, 2, 3));
    program.ops.push_back(op(MaterialOpKind::Multiply, 1, 1, 2));

    MaterialOp tint = op(MaterialOpKind::Constant, 3);
    tint.constant = glm::vec4(s.color, 1.0f);
    program.ops.push_back(tint);
    program.ops.push_back(op(MaterialOpKind::Multiply, 1, 1, 3));

    // Emission only. Every other output stays -1, so the material's own base colour, roughness and
    // metallic survive: the program asserts the one channel it is for and nothing else.
    program.emissionRegister = 1;
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
