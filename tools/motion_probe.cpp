// TEMPORARY (docs/design/autonomous-character-animation.md §9, §16). The alien-leg IK probe.
// Delete it with the architecture it informs.
//
// THE QUESTION. `scene::PoseLayerStack::bind` requires a foot layer's three named joints to form
// an ancestor chain -- mid below root, tip below mid -- and refuses with `LayerResolution::NoChain`
// when they do not. On `alien-scout.glb` they do not: the left leg is spread over three separate
// branches under two different parents, which `pose_layers.cpp` already says outright. So no
// Glowmere alien has ever had a foot planted, and terrain adaptation, step-over and contact
// correction are all blocked behind that one check.
//
// The question this probe answers is NOT "is the rig wrong". It is: **can the existing analytic
// solver reach and hold a target on a chain whose joints are not each other's ancestors, and can
// the result be written back into a local pose without the limb coming apart?** If yes, the fix is
// in the write-back and needs no asset change. If no, the alien needs a re-export and every one of
// its 26 clips has to be re-baked.
//
// WHAT IT DOES NOT DO. It does not touch `src/scene/*` or `src/entity/*`, it does not write a
// scene, and it does not alter the production animation runtime. It loads an asset, solves, and
// prints.
//
// HOW IT CAN FAIL (ADR-182, and the ADR-540 corollary that a synthetic fixture proves something
// about the synthetic fixture -- so every arm below runs on the real rig and the real rest pose):
//
//   * the CONTROL arm asks for the tip where it already is; a solver that cannot return zero error
//     on a null request cannot be trusted on a real one;
//   * the REACH arm asks for a target beyond the limb and must report `Clamped` rather than a
//     small error;
//   * the ANCESTOR arm runs the existing write-back rule on the same chain, and is expected to be
//     WRONG -- if it is not, the whole premise is wrong and this probe has disproved itself;
//   * every arm re-derives forward kinematics from the WRITTEN-BACK local pose rather than trusting
//     the solver's own reported tip, because the solver reports where it put a point and the
//     question is where the skeleton ends up;
//   * bone lengths are measured after the write-back: a solve that moves the tip by breaking the
//     skeleton is a failure that an end-effector error alone would score as a success.

#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/ik.hpp"
#include "scene/motion_analysis.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"

#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

// ---- small helpers ------------------------------------------------------------------------------

glm::vec3 originOf(const glm::mat4& m) { return glm::vec3(m[3]); }

// Is `joint` a descendant of `ancestor` in this skeleton? The exact predicate `PoseLayerStack::bind`
// uses, reproduced here so the probe measures the real rule rather than a paraphrase of it.
bool descends(const scene::Skeleton& skeleton, int joint, int ancestor) {
    for (int at = skeleton.joints[static_cast<std::size_t>(joint)].parent; at >= 0;
         at = skeleton.joints[static_cast<std::size_t>(at)].parent) {
        if (at == ancestor) {
            return true;
        }
    }
    return false;
}

std::string chainOf(const scene::Skeleton& skeleton, int joint) {
    std::string out;
    for (int at = joint; at >= 0; at = skeleton.joints[static_cast<std::size_t>(at)].parent) {
        if (!out.empty()) {
            out += " <- ";
        }
        out += skeleton.joints[static_cast<std::size_t>(at)].name;
    }
    return out;
}

// ---- the two write-back rules -------------------------------------------------------------------
//
// Both take the solver's two model-space pre-rotations and produce a new LOCAL pose. They differ in
// exactly one decision, and that decision is the whole of this probe.

enum class WriteBack {
    // What `PoseLayerStack::apply` does today: a joint's parent is assumed to have inherited the
    // rotation pivoting above it, so the parent's model matrix is transformed too before the local
    // is recovered through it. Correct on a nested rig. On this one, `leg_stretch.l`'s parent is
    // the armature, which inherits nothing from `root.x`.
    Ancestor,
    // The proposed rule: transform a joint's parent only when that parent actually descends from
    // the joint the rotation pivots about. Otherwise the parent did not move, so the local must be
    // recovered through the parent as it is.
    Arbitrary,
};

struct SolveReport {
    scene::IkStatus status = scene::IkStatus::Solved;
    float requested = 0.0f;   // |target - hip|
    float solverTip = 0.0f;   // |solver's reported tip - target|
    float actualTip = 0.0f;   // |FK(written-back pose) tip - target|   <- the number that matters
    float upperBefore = 0.0f; // bone lengths, measured by FK, before and after
    float upperAfter = 0.0f;
    float lowerBefore = 0.0f;
    float lowerAfter = 0.0f;
    float kneeDegrees = 0.0f;
    float maxJointShear = 0.0f; // worst |1 - |column|| over the three written joints' rotations
};

// Solve `chain` (three joint indices, in order) toward `target` and write the result into `pose`.
// `pose` is modified in place. Every distance is re-derived from `pose` afterwards.
SolveReport solveAndWriteBack(const scene::Skeleton& skeleton, scene::Pose& pose, glm::ivec3 chain,
                              const glm::vec3& target, const glm::vec3& pole, bool hasPole,
                              WriteBack rule) {
    const auto r = static_cast<std::size_t>(chain.x);
    const auto m = static_cast<std::size_t>(chain.y);
    const auto t = static_cast<std::size_t>(chain.z);

    std::vector<glm::mat4> model;
    scene::poseToModel(skeleton, pose, model);

    SolveReport out;
    const scene::TwoBoneChain bones{originOf(model[r]), originOf(model[m]), originOf(model[t])};
    out.upperBefore = glm::length(bones.mid - bones.root);
    out.lowerBefore = glm::length(bones.tip - bones.mid);
    out.requested = glm::length(target - bones.root);

    const scene::TwoBoneSolution sol = scene::solveTwoBone(bones, target, pole, hasPole, 1.0f);
    out.status = sol.status;
    out.solverTip = glm::length(sol.tip - target);
    out.kneeDegrees = glm::degrees(sol.kneeAngle);
    if (sol.status == scene::IkStatus::DegenerateBone || sol.status == scene::IkStatus::DegenerateTarget ||
        sol.status == scene::IkStatus::DegenerateBend) {
        return out;
    }

    // The same composition `PoseLayerStack::apply` performs: two model-space pre-rotations about
    // two fixed pivots. `bendHere` is the mid rotation conjugated by the root's aim, because the
    // solver authors the bend in the pre-aim frame.
    const glm::vec3 hip = bones.root;
    const glm::quat bendHere = sol.rootDelta * sol.midBend * glm::conjugate(sol.rootDelta);
    const glm::vec3 knee = hip + (sol.rootDelta * (bones.mid - hip));
    const auto pre = [](const glm::vec3& pivot, const glm::quat& q, const glm::mat4& mm) {
        return glm::translate(glm::mat4(1.0f), pivot) * glm::mat4_cast(q) *
               glm::translate(glm::mat4(1.0f), -pivot) * mm;
    };
    const auto afterRoot = [&](const glm::mat4& mm) { return pre(hip, sol.rootDelta, mm); };
    const auto afterBend = [&](const glm::mat4& mm) { return pre(knee, bendHere, afterRoot(mm)); };

    // `pivotJoint` is the joint the accumulated rotation turns about. Under `Arbitrary` a parent is
    // only carried along when it is genuinely beneath that joint.
    const auto parentModel = [&](std::size_t joint, int pivotJoint, const auto& transform) {
        const int parent = skeleton.joints[joint].parent;
        if (parent < 0) {
            return glm::mat4(1.0f);
        }
        const glm::mat4& pm = model[static_cast<std::size_t>(parent)];
        if (rule == WriteBack::Ancestor) {
            return transform(pm);
        }
        const bool carried = parent == pivotJoint || descends(skeleton, parent, pivotJoint);
        return carried ? transform(pm) : pm;
    };

    const glm::mat4 rootWorld = afterRoot(model[r]);
    pose.local[r] = scene::Transform::fromMatrix(
        glm::inverse(parentModel(r, chain.x, [](const glm::mat4& mm) { return mm; })) * rootWorld);
    const glm::mat4 midWorld = afterBend(model[m]);
    pose.local[m] = scene::Transform::fromMatrix(glm::inverse(parentModel(m, chain.x, afterRoot)) * midWorld);
    const glm::mat4 tipWorld = afterBend(model[t]);
    pose.local[t] = scene::Transform::fromMatrix(glm::inverse(parentModel(t, chain.y, afterBend)) * tipWorld);

    // Re-derive everything from the pose that was actually written. This is the arm that makes the
    // probe able to fail: the solver reports where it put a point, and the question is where the
    // skeleton ended up.
    std::vector<glm::mat4> after;
    scene::poseToModel(skeleton, pose, after);
    const glm::vec3 newRoot = originOf(after[r]);
    const glm::vec3 newMid = originOf(after[m]);
    const glm::vec3 newTip = originOf(after[t]);
    out.actualTip = glm::length(newTip - target);
    out.upperAfter = glm::length(newMid - newRoot);
    out.lowerAfter = glm::length(newTip - newMid);
    for (const std::size_t j : {r, m, t}) {
        const glm::quat& q = pose.local[j].rotation;
        out.maxJointShear = std::max(out.maxJointShear, std::fabs(1.0f - glm::length(q)));
    }
    return out;
}

void printReport(const char* label, const SolveReport& rep) {
    fmt::print("  {:<34} status={:<17} requested={:.4f}  solverTip={:.6f}  ACTUAL tip err={:.6f}\n", label,
               scene::ikStatusName(rep.status), rep.requested, rep.solverTip, rep.actualTip);
    fmt::print("  {:<34} upper {:.4f}->{:.4f} ({:+.6f})  lower {:.4f}->{:.4f} ({:+.6f})  knee={:.1f} deg  "
               "quat drift={:.2e}\n",
               "", rep.upperBefore, rep.upperAfter, rep.upperAfter - rep.upperBefore, rep.lowerBefore,
               rep.lowerAfter, rep.lowerAfter - rep.lowerBefore, rep.kneeDegrees, rep.maxJointShear);
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path assetPath =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("assets/aliens/alien-scout.glb");

    scene::Scene scene;
    assets::GltfLoadOptions options;
    options.loadImages = false; // the probe wants a skeleton, not 128x128 atlases
    const auto loaded = assets::loadGltf(assetPath, scene, options);
    if (!loaded) {
        fmt::print("FAILED to load {}: {}\n", assetPath.string(), loaded.error().message);
        return 1;
    }
    if (scene.rigs.empty()) {
        fmt::print("PROBE BROKEN: {} produced no rig\n", assetPath.string());
        return 1;
    }
    const scene::SkinnedRig& rig = scene.rigs.front();
    const scene::Skeleton& skeleton = rig.skeleton;
    fmt::print("asset   : {}\n", assetPath.string());
    fmt::print("rig     : '{}'  joints={}  palette={}  clips={}\n\n", rig.name, skeleton.jointCount(),
               skeleton.paletteSize(), rig.clips.size());

    // ---- 1. the left leg, as the rig actually has it --------------------------------------------
    scene::Pose rest = scene::restPose(skeleton);
    std::vector<glm::mat4> restModel;
    scene::poseToModel(skeleton, rest, restModel);

    fmt::print("LEFT LEG, rest pose, model space (the joints an author would reach for)\n");
    static const char* kLegNames[] = {"thigh_twist.l",  "thigh_stretch.l", "thigh_twist_2.l",
                                      "leg_stretch.l",  "leg_twist.l",     "leg_twist_2.l",
                                      "foot.l",         "toes_01.l",       "root.x"};
    for (const char* name : kLegNames) {
        const int j = skeleton.find(name);
        if (j < 0) {
            fmt::print("  {:<18} ABSENT\n", name);
            continue;
        }
        const glm::vec3 p = originOf(restModel[static_cast<std::size_t>(j)]);
        fmt::print("  {:<18} idx={:<3} y={:+.4f}  pos=({:+.4f},{:+.4f},{:+.4f})  {}\n", name, j, p.y, p.x,
                   p.y, p.z, chainOf(skeleton, j));
    }

    // ---- 2. the ancestor check, run exactly as bind() runs it -----------------------------------
    struct Candidate {
        const char* label;
        const char* root;
        const char* mid;
        const char* tip;
    };
    static const Candidate kCandidates[] = {
        {"author's first guess", "thigh_stretch.l", "leg_stretch.l", "foot.l"},
        {"topmost thigh",        "thigh_twist.l",   "leg_stretch.l", "foot.l"},
        {"lowest thigh joint",   "thigh_twist_2.l", "leg_twist_2.l", "foot.l"},
        {"hip at root.x",        "root.x",          "leg_stretch.l", "foot.l"},
    };
    fmt::print("\nANCESTOR CHECK -- the rule at src/scene/pose_layers.cpp:280-300\n");
    for (const Candidate& c : kCandidates) {
        const int a = skeleton.find(c.root);
        const int b = skeleton.find(c.mid);
        const int d = skeleton.find(c.tip);
        if (a < 0 || b < 0 || d < 0) {
            fmt::print("  {:<22} a name this rig does not carry\n", c.label);
            continue;
        }
        const bool midOk = descends(skeleton, b, a);
        const bool tipOk = descends(skeleton, d, b);
        fmt::print("  {:<22} {} / {} / {}  ->  mid-below-root={}  tip-below-mid={}  bind() says: {}\n",
                   c.label, c.root, c.mid, c.tip, midOk ? "yes" : "NO", tipOk ? "yes" : "NO",
                   (midOk && tipOk) ? "chain accepted" : "NoChain (refused)");
    }

    // ---- 3. pick the chain geometrically, not by name -------------------------------------------
    // Which joint is the knee is a question about where the joints are, not about what they are
    // called. Take the hip and the foot, and call the knee whichever candidate sits nearest the
    // midpoint of the two while being the one the leg visibly bends at.
    const int hipJ = skeleton.find("thigh_twist.l");
    const int tipJ = skeleton.find("foot.l");
    if (hipJ < 0 || tipJ < 0) {
        fmt::print("\nPROBE BROKEN: this rig has no thigh_twist.l / foot.l\n");
        return 1;
    }
    const glm::vec3 hipP = originOf(restModel[static_cast<std::size_t>(hipJ)]);
    const glm::vec3 tipP = originOf(restModel[static_cast<std::size_t>(tipJ)]);
    int kneeJ = -1;
    float bestScore = 1e30f;
    for (const char* name : {"thigh_stretch.l", "thigh_twist_2.l", "leg_stretch.l", "leg_twist.l",
                             "leg_twist_2.l"}) {
        const int j = skeleton.find(name);
        if (j < 0) {
            continue;
        }
        const glm::vec3 p = originOf(restModel[static_cast<std::size_t>(j)]);
        // A knee is the joint whose two segments are most nearly equal and which is not on the
        // straight line between hip and foot -- but this rig is nearly straight at rest, so use the
        // simplest defensible rule: nearest the midpoint in height.
        const float score = std::fabs(p.y - 0.5f * (hipP.y + tipP.y));
        if (score < bestScore) {
            bestScore = score;
            kneeJ = j;
        }
    }
    fmt::print("\nGEOMETRIC CHAIN (picked from rest-pose positions, not from names)\n");
    fmt::print("  hip  = {:<18} y={:+.4f}\n", skeleton.joints[static_cast<std::size_t>(hipJ)].name, hipP.y);
    fmt::print("  knee = {:<18} y={:+.4f}   (nearest the hip/foot midpoint)\n",
               skeleton.joints[static_cast<std::size_t>(kneeJ)].name,
               originOf(restModel[static_cast<std::size_t>(kneeJ)]).y);
    fmt::print("  foot = {:<18} y={:+.4f}\n", skeleton.joints[static_cast<std::size_t>(tipJ)].name, tipP.y);
    const glm::ivec3 chain(hipJ, kneeJ, tipJ);
    const glm::vec3 kneeP = originOf(restModel[static_cast<std::size_t>(kneeJ)]);
    const float upper = glm::length(kneeP - hipP);
    const float lower = glm::length(tipP - kneeP);
    fmt::print("  upper={:.4f}  lower={:.4f}  span={:.4f}  hip-to-foot at rest={:.4f}\n", upper, lower,
               upper + lower, glm::length(tipP - hipP));
    fmt::print("  mid-below-root={}  tip-below-mid={}  -> this chain is {}\n",
               descends(skeleton, kneeJ, hipJ) ? "yes" : "NO", descends(skeleton, tipJ, kneeJ) ? "yes" : "NO",
               (descends(skeleton, kneeJ, hipJ) && descends(skeleton, tipJ, kneeJ)) ? "an ancestor chain"
                                                                                   : "NOT an ancestor chain");

    // The pole: push the knee forward, in the rig's own model space (+Z is this engine's forward).
    const glm::vec3 poleDir(0.0f, 0.0f, 1.0f);
    const glm::vec3 pole = 0.5f * (hipP + tipP) + poleDir * (upper + lower);

    // ---- 4. the arms ----------------------------------------------------------------------------
    //
    // An arm is classified by MEASUREMENT, not by my expectation: a target farther from the hip
    // than the limb is long MUST come back `Clamped`, and one within reach MUST come back `Solved`.
    // The first version of this probe hardcoded which arms should solve, and it was wrong about
    // four of six -- see the slack figure below, which is the reason.
    const float maxReach = upper + lower;
    const float restDist = glm::length(tipP - hipP);
    const float slack = maxReach - restDist;
    fmt::print("\n  REACH: maxReach={:.4f}  rest hip-to-foot={:.4f}  SLACK={:.4f} ({:.1f}% extended at rest)\n",
               maxReach, restDist, slack, 100.0f * restDist / maxReach);
    fmt::print("  -> this leg may be lengthened by at most {:.4f} model units before the knee locks.\n",
               slack);

    struct Arm {
        const char* label;
        glm::vec3 target;
    };
    const std::vector<Arm> arms = {
        {"CONTROL: the foot where it is", tipP},
        {"drop 0.005 (inside the slack)", tipP - glm::vec3(0.0f, 0.005f, 0.0f)},
        {"raise 0.05", tipP + glm::vec3(0.0f, 0.05f, 0.0f)},
        {"raise 0.15 (step onto a rock)", tipP + glm::vec3(0.0f, 0.15f, 0.0f)},
        {"forward 0.20, raise 0.05", tipP + glm::vec3(0.0f, 0.05f, 0.20f)},
        {"lateral 0.15, raise 0.05", tipP + glm::vec3(0.15f, 0.05f, 0.0f)},
        {"drop 0.10 (a shallow step down)", tipP - glm::vec3(0.0f, 0.10f, 0.0f)},
        {"REACH: 3x the limb's length", hipP - glm::vec3(0.0f, 3.0f * maxReach, 0.0f)},
    };

    int failures = 0;
    float worstFidelity = 0.0f;
    float ancestorWorst = 0.0f;
    for (int pass = 0; pass < 2; ++pass) {
        const WriteBack rule = pass == 0 ? WriteBack::Arbitrary : WriteBack::Ancestor;
        fmt::print("\n{} WRITE-BACK {}\n", pass == 0 ? "ARBITRARY" : "ANCESTOR (what ships today)",
                   pass == 0 ? "-- the proposed rule" : "-- the control: expected to be WRONG here");
        for (const Arm& arm : arms) {
            scene::Pose pose = rest; // every arm starts from rest: the solver has no seed
            const SolveReport rep = solveAndWriteBack(skeleton, pose, chain, arm.target, pole, true, rule);
            printReport(arm.label, rep);
            const float reachable = glm::length(arm.target - hipP);
            // FIDELITY is the question this probe exists to answer: does the pose that was written
            // put the tip where the solver said it did? Everything else is the solver's business.
            const float fidelity = std::fabs(rep.actualTip - rep.solverTip);
            if (pass == 0) {
                worstFidelity = std::max(worstFidelity, fidelity);
            } else {
                ancestorWorst = std::max(ancestorWorst, fidelity);
                continue;
            }
            if (fidelity > 1e-5f) {
                fmt::print("    -> FAIL: the written pose disagrees with the solver by {:.6f}\n", fidelity);
                ++failures;
            }
            const float boneErr = std::max(std::fabs(rep.upperAfter - rep.upperBefore),
                                           std::fabs(rep.lowerAfter - rep.lowerBefore));
            if (boneErr > 1e-4f) {
                fmt::print("    -> FAIL: the write-back changed a bone length by {:.6f}\n", boneErr);
                ++failures;
            }
            const bool shouldClamp = reachable > maxReach + 1e-5f;
            if (shouldClamp && rep.status != scene::IkStatus::Clamped) {
                fmt::print("    -> FAIL: {:.4f} is beyond the limb's {:.4f} but reported {}\n", reachable,
                           maxReach, scene::ikStatusName(rep.status));
                ++failures;
            }
            if (!shouldClamp && rep.status != scene::IkStatus::Solved) {
                fmt::print("    -> FAIL: {:.4f} is within the limb's {:.4f} but reported {}\n", reachable,
                           maxReach, scene::ikStatusName(rep.status));
                ++failures;
            }
        }
    }

    // ---- 4b. the residual on a null request ------------------------------------------------------
    //
    // The CONTROL arm asks for the tip where the tip already is and `solveTwoBone` answers `Solved`
    // -- which its header defines as "the tip is on the target, to floating-point" -- with a
    // residual of 3.15e-4. On a 1.66 m character that is 0.3 mm, and Glowmere draws these bodies at
    // 1.94x, so it is 0.6 mm on screen. Small, but `Solved` is a promise and this is not
    // floating-point. Isolate whether it is the pole or the near-straight chain.
    fmt::print("\nSOLVER RESIDUAL on a null request (target = the tip's own position)\n");
    {
        std::vector<glm::mat4> mm;
        scene::poseToModel(skeleton, rest, mm);
        const scene::TwoBoneChain bones{originOf(mm[static_cast<std::size_t>(chain.x)]),
                                        originOf(mm[static_cast<std::size_t>(chain.y)]),
                                        originOf(mm[static_cast<std::size_t>(chain.z)])};
        for (const bool usePole : {false, true}) {
            const scene::TwoBoneSolution sol =
                scene::solveTwoBone(bones, bones.tip, pole, usePole, 1.0f);
            fmt::print("  pole={:<5} status={:<10} |tip-target|={:.9f}  knee={:.4f} deg  "
                       "achieved={:.6f} requested={:.6f}\n",
                       usePole ? "on" : "off", scene::ikStatusName(sol.status),
                       glm::length(sol.tip - bones.tip), glm::degrees(sol.kneeAngle), sol.achieved,
                       sol.requested);
        }
        // And the same on a deliberately bent chain, to see whether the residual is a property of
        // being near-straight rather than of the solve itself.
        scene::TwoBoneChain bent = bones;
        bent.mid += glm::vec3(0.0f, 0.0f, 0.12f); // push the knee forward; lengths change, so re-measure
        const scene::TwoBoneSolution s2 = scene::solveTwoBone(bent, bent.tip, pole, true, 1.0f);
        fmt::print("  bent chain (knee pushed 0.12 forward): status={:<10} |tip-target|={:.9f}  "
                   "knee={:.4f} deg\n",
                   scene::ikStatusName(s2.status), glm::length(s2.tip - bent.tip),
                   glm::degrees(s2.kneeAngle));
    }

    // ---- 5. what a clamped foot costs, and what fixes it ----------------------------------------
    //
    // The interesting arms above are the ones that clamp. A foot that cannot be lowered 10 cm
    // cannot adapt to terrain, and the fix is not in the solver: it is to let the hip come down.
    // This arm measures how much hip drop each clamped target needs, which is the whole argument
    // for a pelvis stage in the IK architecture.
    fmt::print("\nHIP LOWERING -- what an unreachable target actually needs\n");
    for (const Arm& arm : arms) {
        const float need = glm::length(arm.target - hipP);
        if (need <= maxReach + 1e-5f) {
            continue;
        }
        // Lower the hip straight down until the target is exactly at the limb's limit.
        float drop = 0.0f;
        for (int iter = 0; iter < 64; ++iter) {
            const glm::vec3 h = hipP - glm::vec3(0.0f, drop, 0.0f);
            const float d = glm::length(arm.target - h);
            if (d <= maxReach) {
                break;
            }
            drop += (d - maxReach) * 0.9f;
        }
        const glm::vec3 lowered = hipP - glm::vec3(0.0f, drop, 0.0f);
        const float after = glm::length(arm.target - lowered);
        fmt::print("  {:<34} needs {:.4f} of hip drop  (hip-to-target {:.4f} -> {:.4f}, limb {:.4f}){}\n",
                   arm.label, drop, need, after, maxReach,
                   after <= maxReach + 1e-4f ? "" : "  <- STILL out of reach");
    }

    fmt::print("\nWRITE-BACK FIDELITY: arbitrary rule worst |FK - solver| = {:.8f}\n", worstFidelity);
    fmt::print("                     ancestor  rule worst |FK - solver| = {:.8f}  (the control)\n",
               ancestorWorst);
    if (ancestorWorst <= worstFidelity) {
        fmt::print("PROBE BROKEN: the ancestor rule was supposed to be wrong on this rig and was not. "
                   "The premise of this probe is unproven.\n");
        ++failures;
    }

    // ---- 6b. the two locomotion clips in detail --------------------------------------------------
    fmt::print("\nCONTACT DETAIL -- the clips locomotion actually uses\n");
    {
        const std::array<scene::ContactJoint, 2> feetD{{{"foot.l", scene::ContactKind::Foot},
                                                        {"foot.r", scene::ContactKind::Foot}}};
        for (const char* want : {"Walking", "Running", "Idle"}) {
            const int ci = rig.findClip(want);
            if (ci < 0) {
                continue;
            }
            const scene::AnimationClip& clip = rig.clips[static_cast<std::size_t>(ci)];
            const scene::ClipAnalysis a = scene::analyseClip(skeleton, clip, feetD, 0, {});
            fmt::print("  {} (len {:.3f}, rootTravel {:.4f}, groundSpeed {:.4f})\n", want, a.length,
                       glm::length(glm::vec3(a.rootTravel.x, 0.0f, a.rootTravel.z)), a.groundSpeed);
            for (const scene::ContactTrack& t : a.contacts) {
                fmt::print("    {:<8} lowest={:+.4f} duty={:.2f}  spans:", t.joint, t.lowest, t.dutyCycle);
                for (const scene::ContactSpan& sp : t.spans) {
                    fmt::print(" [{:.3f}..{:.3f}]", sp.start, sp.end);
                }
                fmt::print("\n");
            }
            // The foot height trace, so a human can see where the thresholds are landing.
            scene::Pose pose;
            std::vector<glm::mat4> mm;
            const int lf = skeleton.find("foot.l");
            fmt::print("    foot.l height: ");
            for (int s = 0; s <= static_cast<int>(a.length * 30.0f); ++s) {
                const float t = clip.start + static_cast<float>(s) / 30.0f;
                scene::setRestPose(skeleton, pose);
                scene::sampleClip(clip, std::min(t, clip.duration), pose);
                scene::poseToModel(skeleton, pose, mm);
                fmt::print("{:.3f} ", mm[static_cast<std::size_t>(lf)][3].y);
            }
            fmt::print("\n");
        }
    }

    // ---- 6. contacts and phase, over every clip in the pack (Phase A steps 4 and 5) --------------
    fmt::print("\nCONTACTS AND PHASE -- all {} clips, 30 Hz, ground frame\n", rig.clips.size());
    const std::array<scene::ContactJoint, 2> feet{{{"foot.l", scene::ContactKind::Foot},
                                                   {"foot.r", scene::ContactKind::Foot}}};
    scene::ContactSettings settings;
    fmt::print("  {:<22} {:>6} {:>5} {:>5} {:>6} {:>6} {:>8} {:>7} {:>9}\n", "clip", "len", "L", "R",
               "dutyL", "dutyR", "cyclic", "cycle", "variance");
    for (const scene::AnimationClip& clip : rig.clips) {
        const scene::ClipAnalysis a = scene::analyseClip(skeleton, clip, feet, 0, settings);
        const auto spans = [&](std::size_t i) {
            return i < a.contacts.size() ? a.contacts[i].spans.size() : 0;
        };
        const auto duty = [&](std::size_t i) {
            return i < a.contacts.size() ? a.contacts[i].dutyCycle : 0.0f;
        };
        fmt::print("  {:<22} {:6.3f} {:5d} {:5d} {:6.2f} {:6.2f} {:>8} {:7.3f} {:9.3f}\n", clip.name,
                   a.length, static_cast<int>(spans(0)), static_cast<int>(spans(1)), duty(0), duty(1),
                   a.phase.cyclic ? "yes" : "no", a.phase.cycleSeconds, a.phase.cycleVariance);
    }

    fmt::print("\n{}\n", failures == 0 ? "ARBITRARY-CHAIN SOLVE: every arm passed."
                                       : fmt::format("ARBITRARY-CHAIN SOLVE: {} arm(s) FAILED.", failures));
    return failures == 0 ? 0 : 2;
}
